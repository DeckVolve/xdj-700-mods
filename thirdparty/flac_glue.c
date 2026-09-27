/*
 * Freestanding FLAC glue for the SH-4 target.
 *
 * The codec is intentionally kept behind a small callback interface.  This
 * lets the eventual file-task/PCM-task adapter match the stock ABI once it
 * has been demonstrated, while this part remains an ordinary independent
 * native-FLAC decoder and is testable without Pioneer code.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include "FLAC/stream_decoder.h"
#include "flac_mod.h"

#if defined(XDJ700_TARGET_RUNTIME_PROVIDER) && XDJ700_TARGET_RUNTIME_PROVIDER
#include "dual_format_runtime.h"
#define FLAC_MOD_TARGET_RUNTIME 1
#else
#define FLAC_MOD_TARGET_RUNTIME 0
#endif

/* libFLAC's largest normal decode allocations are the per-channel output,
 * residual, and bit-reader buffers.  Ordinary standalone builds retain the
 * historical linked arena.  An integration build can define
 * FLAC_MOD_REQUIRE_CALLER_ARENA and install allocator-owned RAM before its
 * first decode, avoiding the linked 128 KiB BSS reservation. */
#if FLAC_MOD_TARGET_RUNTIME
#define heap_begin (dual_format_runtime_current()->flac_heap_begin)
#define heap_end (dual_format_runtime_current()->flac_heap_end)
#define heap_ptr (dual_format_runtime_current()->flac_heap_ptr)
#define heap_active (dual_format_runtime_current()->flac_heap_active)
#define flac_active_session (dual_format_runtime_current()->flac_active_session)
#define flac_mod_last_error (dual_format_runtime_current()->flac_mod_last_error)

static int flac_runtime_available(void)
{
    return dual_format_runtime_current() != 0;
}

static int flac_provider_state_conflict(const void *output, unsigned count)
{
    dual_format_runtime_state *runtime = dual_format_runtime_current();
    uintptr_t output_address = (uintptr_t)output;
    uintptr_t runtime_address = (uintptr_t)runtime;

    if (!output || count == 0u || !runtime)
        return 0;
    if (output_address > UINTPTR_MAX - (uintptr_t)count)
        return 1;
    if (output_address <= runtime_address)
        return runtime_address - output_address < count;
    return output_address - runtime_address < sizeof(*runtime);
}

static int flac_provider_pcm_conflict(const short *output,
                                      unsigned frame_count)
{
    dual_format_runtime_state *runtime = dual_format_runtime_current();
    uintptr_t output_address = (uintptr_t)output;
    uintptr_t runtime_address = (uintptr_t)runtime;
    uint64_t byte_count = (uint64_t)frame_count * 2u * sizeof(*output);

    if (!output || frame_count == 0u || !runtime)
        return 0;
    if (byte_count > (uint64_t)UINTPTR_MAX - output_address)
        return 1;
    if (output_address <= runtime_address)
        return (uint64_t)(runtime_address - output_address) < byte_count;
    return output_address - runtime_address < sizeof(*runtime);
}

static int flac_provider_arena_span_unsafe(void *arena)
{
    return flac_provider_state_conflict(arena, FLAC_MOD_ARENA_SIZE);
}
#else
#if !defined(FLAC_MOD_REQUIRE_CALLER_ARENA) || \
    !(FLAC_MOD_REQUIRE_CALLER_ARENA)
static unsigned char __attribute__((aligned(FLAC_MOD_ARENA_ALIGNMENT)))
    flac_default_arena[FLAC_MOD_ARENA_SIZE];
static unsigned char *heap_begin = flac_default_arena;
static unsigned char *heap_end = flac_default_arena + FLAC_MOD_ARENA_SIZE;
#else
static unsigned char *heap_begin;
static unsigned char *heap_end;
#endif
static unsigned char *heap_ptr;
static unsigned heap_active;

volatile unsigned flac_mod_last_error;

static int flac_runtime_available(void)
{
    return 1;
}

static int flac_provider_arena_span_unsafe(void *arena)
{
    (void)arena;
    return 0;
}

static int flac_provider_state_conflict(const void *output, unsigned count)
{
    (void)output;
    (void)count;
    return 0;
}

static int flac_provider_pcm_conflict(const short *output,
                                      unsigned frame_count)
{
    (void)output;
    (void)frame_count;
    return 0;
}
#endif

typedef struct {
    unsigned span_flags;
    unsigned requested_stamp;
} heap_header;

typedef char heap_header_must_preserve_alignment[
    sizeof(heap_header) == FLAC_MOD_ARENA_ALIGNMENT ? 1 : -1];
typedef char heap_alignment_must_be_power_of_two[
    (FLAC_MOD_ARENA_ALIGNMENT != 0u &&
     (FLAC_MOD_ARENA_ALIGNMENT & (FLAC_MOD_ARENA_ALIGNMENT - 1u)) == 0u)
        ? 1 : -1];

#define HEAP_USED_FLAG 0x80000000u
#define HEAP_FREE_FLAG 0x40000000u
#define HEAP_FLAG_MASK 0xC0000000u
#define HEAP_SPAN_MASK 0x3FFFFFFFu
#define HEAP_STAMP 0x464C4143u /* "FLAC" */

#if defined(__GNUC__) && \
    !(defined(FLAC_MOD_ALLOCATOR_TEST) && FLAC_MOD_ALLOCATOR_TEST)
/* Sanitizer runtimes cannot safely interpose these freestanding definitions
 * of the C allocation/memory primitives.  Keep the production primitives
 * uninstrumented; the allocator-specific test build renames the symbols and
 * deliberately leaves every helper instrumented by ASan/UBSan. */
#define FLAC_RUNTIME_NOSAN \
    __attribute__((no_sanitize_address, no_sanitize_undefined))
#else
#define FLAC_RUNTIME_NOSAN
#endif

void *memcpy(void *d, const void *s, size_t n);
void free(void *p);

static FLAC_RUNTIME_NOSAN unsigned heap_span_for_size(size_t size)
{
    unsigned need;

    if (size > (size_t)HEAP_SPAN_MASK -
                   (sizeof(heap_header) + FLAC_MOD_ARENA_ALIGNMENT - 1u))
        return 0;
    need = (unsigned)size + (unsigned)sizeof(heap_header);
    need = (need + FLAC_MOD_ARENA_ALIGNMENT - 1u) &
           ~(FLAC_MOD_ARENA_ALIGNMENT - 1u);
    return need;
}

static FLAC_RUNTIME_NOSAN void heap_write_header(
    heap_header *header, unsigned span, unsigned flag, unsigned requested)
{
    header->span_flags = span | flag;
    header->requested_stamp =
        HEAP_STAMP ^ header->span_flags ^ requested;
}

static FLAC_RUNTIME_NOSAN int heap_read_header(
    heap_header *header, unsigned char *limit, unsigned *span_out,
    unsigned *flag_out, unsigned *requested_out)
{
    unsigned span_flags;
    unsigned span;
    unsigned flag;
    unsigned requested;
    uintptr_t address = (uintptr_t)header;
    uintptr_t end = (uintptr_t)limit;

    if ((address & (FLAC_MOD_ARENA_ALIGNMENT - 1u)) != 0 ||
        address > end || end - address < sizeof(*header))
        return 0;
    span_flags = header->span_flags;
    span = span_flags & HEAP_SPAN_MASK;
    flag = span_flags & HEAP_FLAG_MASK;
    requested = header->requested_stamp ^ HEAP_STAMP ^ span_flags;
    if ((flag != HEAP_USED_FLAG && flag != HEAP_FREE_FLAG) ||
        span < sizeof(*header) ||
        (span & (FLAC_MOD_ARENA_ALIGNMENT - 1u)) != 0 ||
        (uintptr_t)span > end - address ||
        (flag == HEAP_FREE_FLAG && requested != 0) ||
        (flag == HEAP_USED_FLAG && requested > span - sizeof(*header)))
        return 0;
    *span_out = span;
    *flag_out = flag;
    *requested_out = requested;
    return 1;
}

/* Validate the complete initialized part of the arena before accepting a
 * caller-provided pointer.  This makes invalid/double frees harmless and
 * prevents an interior pointer from being mistaken for a block header. */
static FLAC_RUNTIME_NOSAN heap_header *heap_find_used(
    void *p, unsigned *span_out, unsigned *requested_out)
{
    unsigned char *cursor;
    heap_header *found = 0;
    unsigned found_span = 0;
    unsigned found_requested = 0;

    if (!flac_runtime_available() ||
        !p || !heap_active || !heap_begin || !heap_end || !heap_ptr ||
        ((uintptr_t)p & (FLAC_MOD_ARENA_ALIGNMENT - 1u)) != 0 ||
        (uintptr_t)p < (uintptr_t)heap_begin + sizeof(heap_header) ||
        (uintptr_t)p > (uintptr_t)heap_ptr)
        return 0;

    cursor = heap_begin;
    while (cursor < heap_ptr) {
        heap_header *header = (heap_header *)cursor;
        unsigned span;
        unsigned flag;
        unsigned requested;

        if (!heap_read_header(header, heap_ptr, &span, &flag, &requested))
            return 0;
        if ((void *)(header + 1) == p) {
            if (flag != HEAP_USED_FLAG)
                return 0;
            found = header;
            found_span = span;
            found_requested = requested;
        }
        cursor += span;
    }
    if (cursor != heap_ptr || !found)
        return 0;
    *span_out = found_span;
    *requested_out = found_requested;
    return found;
}

/* Merge neighboring free blocks and return a free tail to the uninitialized
 * arena.  No free-list pointers are stored, keeping the integration BSS
 * unchanged and avoiding trust in caller-writable link fields. */
static FLAC_RUNTIME_NOSAN int heap_coalesce(void)
{
    unsigned char *cursor;

    if (!flac_runtime_available())
        return 0;
    cursor = heap_begin;
    while (cursor < heap_ptr) {
        heap_header *header = (heap_header *)cursor;
        unsigned span;
        unsigned flag;
        unsigned requested;

        if (!heap_read_header(header, heap_ptr, &span, &flag, &requested))
            return 0;
        if (flag == HEAP_FREE_FLAG) {
            unsigned char *next = cursor + span;

            while (next < heap_ptr) {
                unsigned next_span;
                unsigned next_flag;
                unsigned next_requested;

                if (!heap_read_header((heap_header *)next, heap_ptr,
                                      &next_span, &next_flag,
                                      &next_requested))
                    return 0;
                if (next_flag != HEAP_FREE_FLAG)
                    break;
                span += next_span;
                next += next_span;
            }
            if (next == heap_ptr) {
                heap_ptr = cursor;
                return 1;
            }
            heap_write_header(header, span, HEAP_FREE_FLAG, 0);
        }
        cursor += span;
    }
    return cursor == heap_ptr;
}

static FLAC_RUNTIME_NOSAN void *heap_alloc(size_t size)
{
    unsigned need;
    unsigned char *cursor;
    uintptr_t current;
    uintptr_t end;

    if (!flac_runtime_available() ||
        !heap_active || !heap_begin || !heap_end || !heap_ptr)
        return 0;
    need = heap_span_for_size(size);
    if (!need)
        return 0;

    cursor = heap_begin;
    while (cursor < heap_ptr) {
        heap_header *header = (heap_header *)cursor;
        unsigned span;
        unsigned flag;
        unsigned requested;

        if (!heap_read_header(header, heap_ptr, &span, &flag, &requested))
            return 0;
        if (flag == HEAP_FREE_FLAG && span >= need) {
            unsigned remainder = span - need;

            heap_write_header(header, need, HEAP_USED_FLAG, (unsigned)size);
            if (remainder != 0)
                heap_write_header((heap_header *)(cursor + need), remainder,
                                  HEAP_FREE_FLAG, 0);
            return (void *)(header + 1);
        }
        cursor += span;
    }
    if (cursor != heap_ptr)
        return 0;

    current = (uintptr_t)heap_ptr;
    end = (uintptr_t)heap_end;
    if (current > end || (uintptr_t)need > end - current)
        return 0;

    heap_write_header((heap_header *)heap_ptr, need, HEAP_USED_FLAG,
                      (unsigned)size);
    heap_ptr += need;
    return (void *)(((heap_header *)(heap_ptr - need)) + 1);
}

FLAC_RUNTIME_NOSAN void *calloc(size_t count, size_t size)
{
    size_t total;
    void *p;

    if (size != 0 && count > (size_t)-1 / size)
        return 0;
    total = count * size;
    p = heap_alloc(total);
    if (p) {
        unsigned char *d = (unsigned char *)p;
        size_t n = total;
        while (n--)
            *d++ = 0;
    }
    return p;
}

FLAC_RUNTIME_NOSAN void *malloc(size_t size)
{
    return heap_alloc(size);
}

FLAC_RUNTIME_NOSAN void *realloc(void *p, size_t size)
{
    heap_header *old;
    unsigned old_span;
    unsigned old_requested;
    unsigned need;
    void *new_p;

    if (!p)
        return malloc(size);
    old = heap_find_used(p, &old_span, &old_requested);
    if (!old)
        return 0;
    if (size == 0) {
        free(p);
        return 0;
    }
    need = heap_span_for_size(size);
    if (!need)
        return 0;

    if (need <= old_span) {
        unsigned remainder = old_span - need;

        heap_write_header(old, need, HEAP_USED_FLAG, (unsigned)size);
        if (remainder != 0) {
            heap_write_header((heap_header *)((unsigned char *)old + need),
                              remainder, HEAP_FREE_FLAG, 0);
            (void)heap_coalesce();
        }
        return p;
    }

    /* Grow into an adjacent free block and, when that block is the current
     * tail, into the remaining arena.  This covers repeated decoder-buffer
     * growth without copying or consuming a fresh block each time. */
    {
        unsigned char *old_address = (unsigned char *)old;
        unsigned char *next = old_address + old_span;
        unsigned initialized = old_span;
        unsigned available = old_span;

        if (next < heap_ptr) {
            unsigned next_span;
            unsigned next_flag;
            unsigned next_requested;

            if (!heap_read_header((heap_header *)next, heap_ptr, &next_span,
                                  &next_flag, &next_requested))
                return 0;
            if (next_flag == HEAP_FREE_FLAG) {
                initialized += next_span;
                available += next_span;
                next += next_span;
            }
        }
        if (next == heap_ptr && (uintptr_t)available <=
                                    (uintptr_t)heap_end -
                                        (uintptr_t)old_address)
            available = (unsigned)((uintptr_t)heap_end -
                                   (uintptr_t)old_address);

        if (available >= need) {
            unsigned char *new_end = old_address + need;

            heap_write_header(old, need, HEAP_USED_FLAG, (unsigned)size);
            if (new_end > heap_ptr)
                heap_ptr = new_end;
            else if (initialized > need)
                heap_write_header((heap_header *)new_end,
                                  initialized - need, HEAP_FREE_FLAG, 0);
            if (initialized > need)
                (void)heap_coalesce();
            return p;
        }
    }

    new_p = heap_alloc(size);
    if (!new_p)
        return 0;
    memcpy(new_p, p, (size_t)old_requested);
    free(p);
    return new_p;
}

FLAC_RUNTIME_NOSAN void free(void *p)
{
    heap_header *header;
    unsigned span;
    unsigned requested;

    if (!p)
        return;
    header = heap_find_used(p, &span, &requested);
    if (!header)
        return;
    heap_write_header(header, span, HEAP_FREE_FLAG, 0);
    (void)heap_coalesce();
}

FLAC_RUNTIME_NOSAN void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

FLAC_RUNTIME_NOSAN void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *pd = (unsigned char *)d;
    const unsigned char *ps = (const unsigned char *)s;
    while (n--)
        *pd++ = *ps++;
    return d;
}

FLAC_RUNTIME_NOSAN int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}

FLAC_RUNTIME_NOSAN void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *pd = (unsigned char *)d;
    const unsigned char *ps = (const unsigned char *)s;
    if (pd < ps) {
        while (n--)
            *pd++ = *ps++;
    } else if (pd > ps) {
        pd += n;
        ps += n;
        while (n--)
            *--pd = *--ps;
    }
    return d;
}

int __clzsi2(unsigned x)
{
    int k = 0;
    if (!x)
        return 32;
    while (!(x & 0x80000000u)) {
        x <<= 1;
        k++;
    }
    return k;
}

int __clzdi2(unsigned long long x)
{
    unsigned hi = (unsigned)(x >> 32);
    unsigned lo = (unsigned)x;
    if (hi)
        return __clzsi2(hi);
    if (lo)
        return 32 + __clzsi2(lo);
    return 64;
}

int abs(int value)
{
    return value < 0 ? -value : value;
}

int flac_mod_init_arena(void *arena, unsigned arena_size)
{
    uintptr_t address;

    if (!flac_runtime_available())
        return FLAC_MOD_ERR_INIT;
    if (heap_active) {
        flac_mod_last_error = FLAC_MOD_ERR_INIT;
        return FLAC_MOD_ERR_INIT;
    }

    /* Provider state owns the allocator pointers, owner, and diagnostics.
     * Reject an arena that could overwrite that state before changing even
     * the diagnostic field or invalidating an existing arena. Standalone
     * builds deliberately retain their historical initialization semantics. */
    if (flac_provider_arena_span_unsafe(arena))
        return FLAC_MOD_ERR_ARGUMENT;

    /* Invalidate first so every rejected initialization leaves no stale
     * arena to fall back to. */
    heap_begin = 0;
    heap_end = 0;
    heap_ptr = 0;
    address = (uintptr_t)arena;
    if (!arena || arena_size < FLAC_MOD_ARENA_SIZE ||
        (address & (FLAC_MOD_ARENA_ALIGNMENT - 1u)) != 0 ||
        address > UINTPTR_MAX - (uintptr_t)FLAC_MOD_ARENA_SIZE) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }

    heap_begin = (unsigned char *)arena;
    heap_end = heap_begin + FLAC_MOD_ARENA_SIZE;
    flac_mod_last_error = FLAC_MOD_OK;
    return FLAC_MOD_OK;
}

static int heap_begin_decode(void)
{
    uintptr_t begin;
    uintptr_t end;

    if (!flac_runtime_available())
        return 0;
    begin = (uintptr_t)heap_begin;
    end = (uintptr_t)heap_end;
    if (heap_active || !heap_begin || !heap_end ||
        (begin & (FLAC_MOD_ARENA_ALIGNMENT - 1u)) != 0 ||
        end < begin || end - begin != FLAC_MOD_ARENA_SIZE)
        return 0;
    heap_ptr = heap_begin;
    heap_active = 1u;
    return 1;
}

static void heap_end_decode(void)
{
    if (!flac_runtime_available())
        return;
    heap_ptr = 0;
    heap_active = 0u;
}

#if defined(FLAC_MOD_ALLOCATOR_TEST) && FLAC_MOD_ALLOCATOR_TEST
int flac_mod_allocator_test_begin(void)
{
    return heap_begin_decode();
}

void flac_mod_allocator_test_end(void)
{
    heap_end_decode();
}
#endif

typedef flac_mod_session_io_state flac_io;

static void set_error(flac_io *io, unsigned error)
{
    if (io->error == FLAC_MOD_OK)
        io->error = error;
}

static FLAC__StreamDecoderReadStatus read_callback(
    const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes,
    void *client_data)
{
    flac_io *io = (flac_io *)client_data;
    size_t want;
    int got;

    (void)decoder;
    if (!io || !bytes) {
        if (bytes)
            *bytes = 0;
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }
    want = *bytes;
    if (want > (size_t)0xFFFFFFFFu || !io->input.read) {
        *bytes = 0;
        set_error(io, FLAC_MOD_ERR_STREAM);
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }

    got = io->input.read(io->input.user, buffer, (unsigned)want);
    if (got < 0) {
        *bytes = 0;
        /* Keep the pull-reader's explicit I/O failure distinct from a
         * malformed byte count or libFLAC's stream/corruption statuses. */
        set_error(io, FLAC_MOD_ERR_IO);
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }
    if ((unsigned)got > (unsigned)want) {
        *bytes = 0;
        set_error(io, FLAC_MOD_ERR_STREAM);
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }
    if (got != 0) {
        *bytes = (size_t)(unsigned)got;
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    /* libFLAC deliberately treats EOF as a normal end-of-stream even when
     * it arrives while read_frame_() is still consuming a frame.  That is
     * useful for some streaming callers, but it would make an unknown-length
     * FLAC with a truncated final frame look successful to this all-input
     * adapter.  Searching for the next sync word at EOF is clean; EOF while
     * the decoder is in READ_FRAME is an incomplete frame. */
    if (FLAC__stream_decoder_get_state(decoder) ==
        FLAC__STREAM_DECODER_READ_FRAME)
        io->eof_during_frame = 1;
    *bytes = 0;
    return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
}

static void metadata_callback(const FLAC__StreamDecoder *decoder,
                              const FLAC__StreamMetadata *metadata,
                              void *client_data)
{
    flac_io *io = (flac_io *)client_data;
    const FLAC__StreamMetadata_StreamInfo *stream_info;

    (void)decoder;
    if (!io || !metadata || metadata->type != FLAC__METADATA_TYPE_STREAMINFO)
        return;

    stream_info = &metadata->data.stream_info;
    io->streaminfo_seen = 1;
    io->info->sample_rate = stream_info->sample_rate;
    io->info->channels = stream_info->channels;
    io->info->bits_per_sample = stream_info->bits_per_sample;
    io->info->total_samples = (unsigned)stream_info->total_samples;

    /* These are the practical XDJ-compatible limits.  A later adapter can
     * choose a stricter device policy without changing the FLAC parser. */
    if (stream_info->channels == 0 || stream_info->channels > 2 ||
        stream_info->bits_per_sample < 4 || stream_info->bits_per_sample > 24 ||
        stream_info->sample_rate == 0 || stream_info->sample_rate > 48000)
        set_error(io, FLAC_MOD_ERR_UNSUPPORTED_FORMAT);
    /* Reject an oversized stream before the first frame can make libFLAC
     * grow its per-channel output and residual arrays.  The frame callback
     * repeats this guard because variable-blocksize streams can advertise a
     * broad STREAMINFO range. */
    if (stream_info->min_blocksize == 0 ||
        stream_info->min_blocksize > stream_info->max_blocksize ||
        stream_info->max_blocksize > FLAC_MOD_MAX_BLOCKSIZE)
        set_error(io, FLAC_MOD_ERR_UNSUPPORTED_FORMAT);
    if (stream_info->total_samples > (FLAC__uint64)0xFFFFFFFFu)
        set_error(io, FLAC_MOD_ERR_UNSUPPORTED_FORMAT);
}

static FLAC__StreamDecoderWriteStatus write_callback(
    const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame,
    const FLAC__int32 * const buffer[], void *client_data)
{
    flac_io *io = (flac_io *)client_data;
    unsigned blocksize;

    (void)decoder;
    if (!io || !frame || !buffer || !io->streaminfo_seen) {
        if (io)
            set_error(io, FLAC_MOD_ERR_DECODER);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (io->error != FLAC_MOD_OK)
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    /* FLAC repeats these fields in every frame header.  A conforming stream
     * must keep them equal to STREAMINFO; libFLAC intentionally accepts some
     * such mismatches so callers can decide their own policy. */
    if (frame->header.channels != io->info->channels ||
        frame->header.sample_rate != io->info->sample_rate ||
        frame->header.bits_per_sample != io->info->bits_per_sample ||
        frame->header.blocksize == 0 ||
        frame->header.blocksize > FLAC_MOD_MAX_BLOCKSIZE || !buffer[0] ||
        (io->info->channels == 2 && !buffer[1])) {
        set_error(io, FLAC_MOD_ERR_DECODER);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    blocksize = frame->header.blocksize;
    if (io->callback_limit != 0u &&
        io->callback_count >= io->callback_limit) {
        set_error(io, FLAC_MOD_ERR_DECODER);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    io->callback_count++;
    if (io->frames > 0xFFFFFFFFu - blocksize) {
        set_error(io, FLAC_MOD_ERR_OUTPUT_FULL);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (io->info->total_samples != 0 &&
        (FLAC__uint64)io->frames + (FLAC__uint64)blocksize >
            (FLAC__uint64)io->info->total_samples) {
        set_error(io, FLAC_MOD_ERR_DECODER);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (!io->sink) {
        set_error(io, FLAC_MOD_ERR_ARGUMENT);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (io->sink((const int32_t * const *)buffer, blocksize,
                 io->info->channels, io->info->bits_per_sample,
                 io->sink_user) != 0) {
        set_error(io, FLAC_MOD_ERR_OUTPUT_FULL);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    io->frames += blocksize;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void error_callback(const FLAC__StreamDecoder *decoder,
                           FLAC__StreamDecoderErrorStatus status,
                           void *client_data)
{
    flac_io *io = (flac_io *)client_data;
    (void)decoder;
    if (io)
        set_error(io, FLAC_MOD_ERROR_MARKER | (unsigned)status);
}

typedef flac_mod_session flac_session_state;

#if !FLAC_MOD_TARGET_RUNTIME
static flac_mod_session *flac_active_session;
#endif

static flac_session_state *flac_session_get(flac_mod_session *session)
{
    return session;
}

static int flac_ranges_overlap(const void *left, unsigned left_count,
                               const void *right, unsigned right_count)
{
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;

    if (!left || !right || left_count == 0u || right_count == 0u)
        return 0;
    /* A nonempty span that cannot be represented in the target address
     * domain is never safe. Treat it as a conflict so every caller rejects
     * it before clearing outputs, copying descriptors, or touching decoder
     * state. Returning "disjoint" here would let malformed spans evade the
     * alias guards and reach wrapped pointer arithmetic later. */
    if (left_start > UINTPTR_MAX - left_count)
        return 1;
    if (right_start > UINTPTR_MAX - right_count)
        return 1;
    return left_start < right_start + right_count &&
           right_start < left_start + left_count;
}

static int flac_session_output_conflicts(
    const flac_mod_session *session, const void *output, unsigned count)
{
    unsigned arena_count = 0u;

    (void)session;

    if (heap_begin && heap_end && (uintptr_t)heap_end >=
                                      (uintptr_t)heap_begin &&
        (uintptr_t)heap_end - (uintptr_t)heap_begin <= UINT_MAX)
        arena_count = (unsigned)((uintptr_t)heap_end -
                                 (uintptr_t)heap_begin);
    return flac_ranges_overlap(output, count, heap_begin, arena_count);
}

static int flac_session_error_result(flac_session_state *state,
                                     unsigned fallback)
{
    unsigned error;

    if (!state)
        error = fallback;
    else {
        if (state->io.error == FLAC_MOD_OK)
            set_error(&state->io, fallback);
        error = state->io.error;
        state->terminal = 1u;
    }
    flac_mod_last_error = error;
    return (error & FLAC_MOD_ERROR_MARKER) != 0u
               ? FLAC_MOD_ERR_STREAM
               : (int)error;
}

int flac_mod_session_open(flac_mod_session *session,
                          const flac_mod_input *input)
{
    flac_session_state *state;
    FLAC__StreamDecoderInitStatus init_status;

    if (!flac_runtime_available())
        return FLAC_MOD_ERR_INIT;
    if (flac_provider_state_conflict(session, (unsigned)sizeof(*session)) ||
        flac_provider_state_conflict(input, (unsigned)sizeof(*input)))
        return FLAC_MOD_ERR_ARGUMENT;
    flac_mod_last_error = FLAC_MOD_OK;
    /* The incremental entry has no wrapper preflight.  Validate both whole
     * objects before reading even the input callback field: a descriptor
     * crossing the address ceiling can otherwise fault here before the
     * later session/input overlap check rejects it. */
    if (!session || !input ||
        (uintptr_t)session > UINTPTR_MAX - (uintptr_t)sizeof(*session) ||
        (uintptr_t)input > UINTPTR_MAX - (uintptr_t)sizeof(*input) ||
        !input->read) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }
    /* Check ownership before touching the caller's object.  This is the
     * no-input-callback path used for nested rejection.  The process-wide
     * owner is deliberately non-atomic; callers provide task confinement or
     * an external lock for true preemptive concurrency. */
    if (flac_active_session || heap_active) {
        flac_mod_last_error = FLAC_MOD_ERR_STATE;
        return FLAC_MOD_ERR_STATE;
    }
    /* The session is cleared below and remains live while libFLAC owns arena
     * allocations. Reject writable aliases before either domain is touched. */
    if (flac_ranges_overlap(session, (unsigned)sizeof(*session), input,
                            (unsigned)sizeof(*input)) ||
        flac_session_output_conflicts(session, session,
                                      (unsigned)sizeof(*session))) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }

    memset(session, 0, sizeof(*session));
    if (!heap_begin_decode()) {
        flac_mod_last_error = FLAC_MOD_ERR_INIT;
        return FLAC_MOD_ERR_INIT;
    }

    state = flac_session_get(session);
    state->io.input = *input;
    state->io.info = &state->info;
    state->operation_active = 1u;
    flac_active_session = session;

    state->decoder = FLAC__stream_decoder_new();
    if (!state->decoder) {
        state->operation_active = 0u;
        flac_mod_session_close(session);
        flac_mod_last_error = FLAC_MOD_ERR_ALLOC;
        return FLAC_MOD_ERR_ALLOC;
    }
    (void)FLAC__stream_decoder_set_md5_checking(state->decoder, false);
    (void)FLAC__stream_decoder_set_metadata_respond(
        state->decoder, FLAC__METADATA_TYPE_STREAMINFO);
    init_status = FLAC__stream_decoder_init_stream(
        state->decoder, read_callback, 0, 0, 0, 0, write_callback,
        metadata_callback, error_callback, &state->io);
    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        state->operation_active = 0u;
        flac_mod_session_close(session);
        flac_mod_last_error =
            FLAC_MOD_ERROR_MARKER | (unsigned)init_status;
        return FLAC_MOD_ERR_INIT;
    }

    state->initialized = 1u;
    state->operation_active = 0u;
    flac_mod_last_error = FLAC_MOD_OK;
    return FLAC_MOD_OK;
}

static int flac_session_report_end(flac_session_state *state,
                                   flac_mod_info *info)
{
    FLAC__StreamDecoderState final_state;
    FLAC__bool finish_ok;

    final_state = FLAC__stream_decoder_get_state(state->decoder);
    if (!state->io.streaminfo_seen)
        set_error(&state->io, FLAC_MOD_ERR_NO_STREAMINFO);
    if (state->io.eof_during_frame)
        set_error(&state->io, FLAC_MOD_ERR_DECODER);
    if (state->io.error == FLAC_MOD_OK && state->info.total_samples != 0u &&
        (FLAC__uint64)state->io.frames !=
            (FLAC__uint64)state->info.total_samples)
        set_error(&state->io, FLAC_MOD_ERR_DECODER);
    if (final_state != FLAC__STREAM_DECODER_END_OF_STREAM)
        set_error(&state->io, FLAC_MOD_ERR_DECODER);

    finish_ok = FLAC__stream_decoder_finish(state->decoder);
    state->finished = 1u;
    if (!finish_ok)
        set_error(&state->io, FLAC_MOD_ERR_DECODER);
    if (info)
        *info = state->info;
    if (state->io.error != FLAC_MOD_OK)
        return flac_session_error_result(state, FLAC_MOD_ERR_DECODER);

    state->end_reported = 1u;
    flac_mod_last_error = FLAC_MOD_OK;
    return FLAC_MOD_END;
}

int flac_mod_session_next(flac_mod_session *session,
                          flac_mod_pcm_sink sink, void *sink_user,
                          flac_mod_info *info, unsigned *frames_out)
{
    flac_session_state *state;
    unsigned frames_before;

    /* The provider diagnostic and owner fields live inside the runtime
     * object.  Reject aliases before even reporting through that object: an
     * output may cover the diagnostic itself, so changing it would violate
     * the transactional output contract. */
    if ((info && flac_provider_state_conflict(
                     info, (unsigned)sizeof(*info))) ||
        (frames_out && flac_provider_state_conflict(
                           frames_out, (unsigned)sizeof(*frames_out))))
        return FLAC_MOD_ERR_ARGUMENT;
    if ((frames_out && session && flac_ranges_overlap(
                         frames_out, (unsigned)sizeof(*frames_out), session,
                         (unsigned)sizeof(*session))) ||
        (info && session && flac_ranges_overlap(
                            info, (unsigned)sizeof(*info), session,
                            (unsigned)sizeof(*session))) ||
        (info && frames_out && flac_ranges_overlap(
                                info, (unsigned)sizeof(*info), frames_out,
                                (unsigned)sizeof(*frames_out)))) {
        if (flac_runtime_available())
            flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }
    if (!flac_runtime_available()) {
        if (frames_out)
            *frames_out = 0u;
        if (info)
            memset(info, 0, sizeof(*info));
        return FLAC_MOD_ERR_STATE;
    }
    if ((frames_out && flac_session_output_conflicts(
                           session, frames_out,
                           (unsigned)sizeof(*frames_out))) ||
        (info && flac_session_output_conflicts(
                     session, info, (unsigned)sizeof(*info)))) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }
    if (frames_out)
        *frames_out = 0u;
    if (info)
        memset(info, 0, sizeof(*info));
    if (!session || !sink || !frames_out) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }
    if (flac_active_session != session) {
        flac_mod_last_error = FLAC_MOD_ERR_STATE;
        return FLAC_MOD_ERR_STATE;
    }

    state = flac_session_get(session);
    if (!state->decoder || !state->initialized || state->terminal ||
        state->end_reported || state->operation_active) {
        flac_mod_last_error = FLAC_MOD_ERR_STATE;
        return FLAC_MOD_ERR_STATE;
    }
    if (state->end_ready)
        return flac_session_report_end(state, info);

    state->operation_active = 1u;
    state->io.sink = sink;
    state->io.sink_user = sink_user;
    state->io.callback_count = 0u;
    state->io.callback_limit = 1u;
    frames_before = state->io.frames;

    for (;;) {
        FLAC__bool process_ok =
            FLAC__stream_decoder_process_single(state->decoder);
        FLAC__StreamDecoderState decoder_state =
            FLAC__stream_decoder_get_state(state->decoder);

        if (state->io.error != FLAC_MOD_OK || !process_ok) {
            state->operation_active = 0u;
            if (info)
                *info = state->info;
            return flac_session_error_result(state,
                                             FLAC_MOD_ERR_DECODER);
        }
        if (state->io.callback_count != 0u) {
            if (state->io.callback_count != 1u ||
                state->io.frames <= frames_before) {
                state->operation_active = 0u;
                if (info)
                    *info = state->info;
                return flac_session_error_result(state,
                                                 FLAC_MOD_ERR_DECODER);
            }
            *frames_out = state->io.frames - frames_before;
            if (decoder_state == FLAC__STREAM_DECODER_END_OF_STREAM)
                state->end_ready = 1u;
            if (info)
                *info = state->info;
            state->operation_active = 0u;
            flac_mod_last_error = FLAC_MOD_OK;
            return FLAC_MOD_OK;
        }
        if (decoder_state == FLAC__STREAM_DECODER_END_OF_STREAM) {
            state->end_ready = 1u;
            state->operation_active = 0u;
            return flac_session_report_end(state, info);
        }
        /* process_single() consumed one metadata block.  Continue until this
         * public next call has either one PCM block or a definitive end. */
    }
}

void flac_mod_session_close(flac_mod_session *session)
{
    flac_session_state *state;

    if (!session)
        return;
    /* Close has no peer range to compare against when there is no active
     * owner. Reject an unrepresentable destination explicitly before the
     * runtime-hidden legacy clear or any ordinary independent-object clear. */
    if ((uintptr_t)session >
        UINTPTR_MAX - (unsigned)sizeof(*session))
        return;
    if (!flac_runtime_available()) {
        /* Runtime-hidden callers cannot discover the provider object. Their
         * confinement contract must exclude hidden-state aliases. Preserve
         * the legacy independent-object clear for adapter teardown. */
        memset(session, 0, sizeof(*session));
        return;
    }
    if (flac_provider_state_conflict(session, (unsigned)sizeof(*session)))
        return;
    if (flac_active_session != session) {
        /* Ignore callback-supplied interior pointers into the live session
         * or allocator arena.  A non-owner close remains idempotent for an
         * ordinary independent session object, but must not erase live
         * decoder state through an overlapping destination. */
        if (flac_active_session &&
            (flac_ranges_overlap(session, (unsigned)sizeof(*session),
                                 flac_active_session,
                                 (unsigned)sizeof(*flac_active_session)) ||
             flac_session_output_conflicts(
                 flac_active_session, session,
                 (unsigned)sizeof(*session))))
            return;
        memset(session, 0, sizeof(*session));
        return;
    }

    state = flac_session_get(session);
    /* A source/sink callback cannot invalidate the active stack operation.
     * Its outer owner can close normally as soon as that call returns. */
    if (state->operation_active)
        return;
    if (state->decoder) {
        if (state->initialized && !state->finished)
            (void)FLAC__stream_decoder_finish(state->decoder);
        FLAC__stream_decoder_delete(state->decoder);
    }
    heap_end_decode();
    flac_active_session = 0;
    memset(session, 0, sizeof(*session));
}

static int flac_discard_sink(const int32_t * const channels[],
                             unsigned frame_count,
                             unsigned channel_count,
                             unsigned bits_per_sample, void *user)
{
    (void)channels;
    (void)frame_count;
    (void)channel_count;
    (void)bits_per_sample;
    (void)user;
    return 0;
}

enum {
    FLAC_DECODE_OUTPUTS_SAFE = 0,
    FLAC_DECODE_OUTPUTS_CONFLICT = 1,
    FLAC_DECODE_OUTPUTS_PROVIDER_CONFLICT = 2
};

static int flac_decode_outputs_conflict(const flac_mod_input *input,
                                        flac_mod_info *info,
                                        unsigned *frames_out,
                                        const void *source,
                                        unsigned source_count)
{
    /* Validate readable spans even when the caller omitted both writable
     * outputs.  Otherwise the alias comparisons below have no peer to
     * examine and a wrapper can dereference a wrapped descriptor/source. */
    if ((input && (uintptr_t)input >
                      UINTPTR_MAX - (uintptr_t)sizeof(*input)) ||
        (source && (uintptr_t)source >
                       UINTPTR_MAX - (uintptr_t)source_count))
        return FLAC_DECODE_OUTPUTS_CONFLICT;
    if ((input && flac_provider_state_conflict(
                      input, (unsigned)sizeof(*input))) ||
        (source && flac_provider_state_conflict(source, source_count)))
        return FLAC_DECODE_OUTPUTS_PROVIDER_CONFLICT;
    if ((info && flac_provider_state_conflict(
                     info, (unsigned)sizeof(*info))) ||
        (frames_out && flac_provider_state_conflict(
                           frames_out, (unsigned)sizeof(*frames_out))))
        return FLAC_DECODE_OUTPUTS_PROVIDER_CONFLICT;
    return
        (info && frames_out &&
         flac_ranges_overlap(info, (unsigned)sizeof(*info), frames_out,
                             (unsigned)sizeof(*frames_out))) ||
        (info && input &&
         flac_ranges_overlap(info, (unsigned)sizeof(*info), input,
                             (unsigned)sizeof(*input))) ||
        (frames_out && input &&
         flac_ranges_overlap(frames_out, (unsigned)sizeof(*frames_out), input,
                             (unsigned)sizeof(*input))) ||
        (info && source &&
         flac_ranges_overlap(info, (unsigned)sizeof(*info), source,
                             source_count)) ||
        (frames_out && source &&
         flac_ranges_overlap(frames_out, (unsigned)sizeof(*frames_out),
                             source, source_count)) ||
        (info && flac_session_output_conflicts(
                     0, info, (unsigned)sizeof(*info))) ||
        (frames_out && flac_session_output_conflicts(
                            0, frames_out,
                            (unsigned)sizeof(*frames_out)));
}

static int flac_mod_decode_internal(const flac_mod_input *input,
                                    flac_mod_pcm_sink sink, void *sink_user,
                                    flac_mod_info *info, unsigned *frames_out)
{
    flac_mod_session session;
    flac_mod_info local_info;
    flac_mod_pcm_sink effective_sink = sink ? sink : flac_discard_sink;
    unsigned total_frames = 0u;
    int output_conflict;
    int result;

    if (!flac_runtime_available())
        return FLAC_MOD_ERR_INIT;
    output_conflict = flac_decode_outputs_conflict(
        input, info, frames_out, 0, 0u);
    if (output_conflict) {
        if (output_conflict == FLAC_DECODE_OUTPUTS_CONFLICT)
            flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }
    flac_mod_last_error = FLAC_MOD_OK;
    if (frames_out)
        *frames_out = 0u;
    if (info)
        memset(info, 0, sizeof(*info));
    if (!input || !input->read || !frames_out) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }

    memset(&local_info, 0, sizeof(local_info));
    result = flac_mod_session_open(&session, input);
    if (result != FLAC_MOD_OK) {
        /* Preserve the original one-shot nested-use result. */
        if (result == FLAC_MOD_ERR_STATE) {
            flac_mod_last_error = FLAC_MOD_ERR_INIT;
            return FLAC_MOD_ERR_INIT;
        }
        return result;
    }

    for (;;) {
        unsigned block_frames = 0u;

        result = flac_mod_session_next(&session, effective_sink, sink_user,
                                       &local_info, &block_frames);
        if (result == FLAC_MOD_OK) {
            if (total_frames > 0xFFFFFFFFu - block_frames) {
                result = FLAC_MOD_ERR_OUTPUT_FULL;
                flac_mod_last_error = FLAC_MOD_ERR_OUTPUT_FULL;
                break;
            }
            total_frames += block_frames;
            continue;
        }
        if (result == FLAC_MOD_END)
            result = FLAC_MOD_OK;
        break;
    }

    if (info)
        *info = local_info;
    *frames_out = total_frames;
    flac_mod_session_close(&session);
    return result;
}

typedef struct {
    const unsigned char *data;
    unsigned length;
    unsigned position;
} flac_memory_input;

static int memory_input_read(void *user, unsigned char *buffer, unsigned bytes)
{
    flac_memory_input *input = (flac_memory_input *)user;
    unsigned left;

    if (!input || (!buffer && bytes != 0) || input->position > input->length)
        return -1;
    left = input->length - input->position;
    if (bytes > left)
        bytes = left;
    if (bytes != 0) {
        memcpy(buffer, input->data + input->position, bytes);
        input->position += bytes;
    }
    return (int)bytes;
}

int flac_mod_decode_input(const flac_mod_input *input,
                          flac_mod_pcm_sink sink, void *sink_user,
                          flac_mod_info *info, unsigned *frames_out)
{
    return flac_mod_decode_internal(input, sink, sink_user, info, frames_out);
}

int flac_mod_decode(const unsigned char *data, unsigned data_length,
                    flac_mod_pcm_sink sink, void *sink_user,
                    flac_mod_info *info, unsigned *frames_out)
{
    flac_memory_input memory;
    flac_mod_input input;
    int output_conflict;

    if (!flac_runtime_available())
        return FLAC_MOD_ERR_INIT;
    output_conflict = flac_decode_outputs_conflict(
        0, info, frames_out, data, data_length);
    if (output_conflict) {
        if (output_conflict == FLAC_DECODE_OUTPUTS_CONFLICT)
            flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }
    if (!data || data_length == 0) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        if (frames_out)
            *frames_out = 0;
        if (info)
            memset(info, 0, sizeof(*info));
        return FLAC_MOD_ERR_ARGUMENT;
    }
    memory.data = data;
    memory.length = data_length;
    memory.position = 0;
    input.read = memory_input_read;
    input.user = &memory;
    return flac_mod_decode_internal(&input, sink, sink_user, info, frames_out);
}

static int file_input_read(void *user, unsigned char *buffer, unsigned bytes)
{
    flac_mod_file_input *input = (flac_mod_file_input *)user;

    if (!input || !input->operation || (!buffer && bytes != 0))
        return -1;
    return input->operation(buffer, FLAC_MOD_FILE_READ_OPERATION, bytes,
                            input->file_handle, input->context);
}

int flac_mod_decode_file(const flac_mod_file_input *input,
                         flac_mod_pcm_sink sink, void *sink_user,
                         flac_mod_info *info, unsigned *frames_out)
{
    flac_mod_file_input file;
    flac_mod_input stream;
    int output_conflict;

    if (!flac_runtime_available())
        return FLAC_MOD_ERR_INIT;
    output_conflict = flac_decode_outputs_conflict(
        0, info, frames_out, input, (unsigned)sizeof(*input));
    if (output_conflict) {
        if (output_conflict == FLAC_DECODE_OUTPUTS_CONFLICT)
            flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }
    if (!input || !input->operation) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        if (frames_out)
            *frames_out = 0;
        if (info)
            memset(info, 0, sizeof(*info));
        return FLAC_MOD_ERR_ARGUMENT;
    }
    file = *input;
    stream.read = file_input_read;
    stream.user = &file;
    return flac_mod_decode_internal(&stream, sink, sink_user, info,
                                    frames_out);
}

typedef struct {
    flac_mod_random_input source;
    unsigned offset;
} random_file_input;

static int random_file_input_read(void *user, unsigned char *buffer,
                                  unsigned bytes)
{
    random_file_input *input = (random_file_input *)user;
    unsigned bytes_read = 0;
    int status;

    if (!input || !input->source.read_at || (!buffer && bytes != 0))
        return -1;
    status = input->source.read_at(input->source.user, input->offset,
                                   buffer, bytes, &bytes_read);
    if (status != 0 || bytes_read > bytes ||
        bytes_read > 0x7FFFFFFFu ||
        input->offset > 0xFFFFFFFFu - bytes_read)
        return -1;
    input->offset += bytes_read;
    return (int)bytes_read;
}

int flac_mod_decode_file_at(const flac_mod_random_input *input,
                            flac_mod_pcm_sink sink, void *sink_user,
                            flac_mod_info *info, unsigned *frames_out)
{
    random_file_input file;
    flac_mod_input stream;
    int output_conflict;

    if (!flac_runtime_available())
        return FLAC_MOD_ERR_INIT;
    output_conflict = flac_decode_outputs_conflict(
        0, info, frames_out, input, (unsigned)sizeof(*input));
    if (output_conflict) {
        if (output_conflict == FLAC_DECODE_OUTPUTS_CONFLICT)
            flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }
    if (!input || !input->read_at) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        if (frames_out)
            *frames_out = 0;
        if (info)
            memset(info, 0, sizeof(*info));
        return FLAC_MOD_ERR_ARGUMENT;
    }
    file.source = *input;
    file.offset = 0;
    stream.read = random_file_input_read;
    stream.user = &file;
    return flac_mod_decode_internal(&stream, sink, sink_user, info,
                                    frames_out);
}

typedef struct {
    flac_mod_bounded_random_input source;
    unsigned offset;
} bounded_random_file_input;

static int bounded_random_file_input_read(void *user, unsigned char *buffer,
                                          unsigned bytes)
{
    bounded_random_file_input *input = (bounded_random_file_input *)user;
    unsigned request;
    unsigned remaining;
    unsigned bytes_read = 0;
    unsigned absolute_offset;
    int status;

    if (!input || !input->source.read_at || (!buffer && bytes != 0))
        return -1;
    if (input->offset > input->source.length)
        return -1;
    remaining = input->source.length - input->offset;
    request = bytes;
    if (input->source.max_request != 0 &&
        request > input->source.max_request)
        request = input->source.max_request;
    if (request > remaining)
        request = remaining;
    if (request == 0)
        return 0;
    if (input->source.base_offset >
        0xFFFFFFFFu - input->offset)
        return -1;
    absolute_offset = input->source.base_offset + input->offset;
    status = input->source.read_at(input->source.user, absolute_offset,
                                   buffer, request, &bytes_read);
    /* length is authoritative for this bounded adapter.  Because request is
     * nonzero below that boundary, zero progress cannot mean EOF and must not
     * be translated into libFLAC's clean END_OF_STREAM status. */
    if (status != 0 || bytes_read == 0u || bytes_read > request ||
        bytes_read > 0x7FFFFFFFu ||
        input->offset > 0xFFFFFFFFu - bytes_read)
        return -1;
    input->offset += bytes_read;
    return (int)bytes_read;
}

int flac_mod_decode_file_at_bounded(
    const flac_mod_bounded_random_input *input,
    flac_mod_pcm_sink sink, void *sink_user,
    flac_mod_info *info, unsigned *frames_out)
{
    bounded_random_file_input file;
    flac_mod_input stream;
    int output_conflict;

    if (!flac_runtime_available())
        return FLAC_MOD_ERR_INIT;
    output_conflict = flac_decode_outputs_conflict(
        0, info, frames_out, input, (unsigned)sizeof(*input));
    if (output_conflict) {
        if (output_conflict == FLAC_DECODE_OUTPUTS_CONFLICT)
            flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return FLAC_MOD_ERR_ARGUMENT;
    }
    if (!input || !input->read_at || input->length == 0 ||
        (input->length > 1u && input->base_offset >
         0xFFFFFFFFu - (input->length - 1u))) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        if (frames_out)
            *frames_out = 0;
        if (info)
            memset(info, 0, sizeof(*info));
        return FLAC_MOD_ERR_ARGUMENT;
    }
    file.source = *input;
    file.offset = 0;
    stream.read = bounded_random_file_input_read;
    stream.user = &file;
    return flac_mod_decode_internal(&stream, sink, sink_user, info,
                                    frames_out);
}

typedef struct {
    short *output;
    unsigned capacity;
    unsigned written;
} packed_output;

static short pack_sample(int sample, unsigned bits_per_sample)
{
    int64_t value = (int64_t)sample;

    /* Do the scaling in a wider unsigned-safe domain.  Shifting a negative
     * signed C value is undefined, and the target build must not depend on
     * the host compiler's signed right-shift choice. */
    if (bits_per_sample < 16) {
        value *= (int64_t)1 << (16 - bits_per_sample);
    } else if (bits_per_sample > 16) {
        int64_t divisor = (int64_t)1 << (bits_per_sample - 16);
        if (value >= 0)
            value /= divisor;
        else
            /* Match an arithmetic right shift's floor-toward-negative-
             * infinity behavior without relying on implementation-defined
             * signed shifts. */
            value = -((-value + divisor - 1) / divisor);
    }
    if (value > 32767)
        value = 32767;
    else if (value < -32768)
        value = -32768;
    return (short)value;
}

static int packed_sink(const int32_t * const channels[], unsigned frame_count,
                       unsigned channel_count, unsigned bits_per_sample,
                       void *user)
{
    packed_output *output = (packed_output *)user;
    unsigned i;
    if (!output || !output->output || !channels || !channels[0] ||
        channel_count == 0 || channel_count > 2 ||
        (channel_count == 2 && !channels[1]) ||
        output->written > output->capacity ||
        frame_count > output->capacity - output->written)
        return 1;
    for (i = 0; i < frame_count; i++) {
        size_t frame = (size_t)output->written + (size_t)i;
        if (frame > (size_t)-1 / 2u)
            return 1;
        size_t sample = frame * 2u;
        short left = pack_sample(channels[0][i], bits_per_sample);
        short right = channel_count == 1
                    ? left : pack_sample(channels[1][i], bits_per_sample);
        output->output[sample] = left;
        output->output[sample + 1u] = right;
    }
    output->written += frame_count;
    return 0;
}

static int flac_packed_ranges_conflict(const unsigned char *flac_data,
                                       unsigned flac_len,
                                       short *pcm_out,
                                       unsigned pcm_max_frames,
                                       unsigned *frames_out)
{
    unsigned pcm_bytes;

    if ((frames_out && flac_provider_state_conflict(
                           frames_out, (unsigned)sizeof(*frames_out))) ||
        flac_provider_pcm_conflict(pcm_out, pcm_max_frames))
        return FLAC_DECODE_OUTPUTS_PROVIDER_CONFLICT;
    if (pcm_max_frames > UINT_MAX / (2u * (unsigned)sizeof(*pcm_out)))
        return FLAC_DECODE_OUTPUTS_CONFLICT;
    pcm_bytes = pcm_max_frames * 2u * (unsigned)sizeof(*pcm_out);
    return
        (frames_out && pcm_out &&
         flac_ranges_overlap(frames_out, (unsigned)sizeof(*frames_out),
                             pcm_out, pcm_bytes)) ||
        (frames_out && flac_data &&
         flac_ranges_overlap(frames_out, (unsigned)sizeof(*frames_out),
                             flac_data, flac_len)) ||
        (pcm_out && flac_data &&
         flac_ranges_overlap(pcm_out, pcm_bytes, flac_data, flac_len));
}

static int flac_packed_arena_conflict(short *pcm_out,
                                      unsigned pcm_max_frames,
                                      unsigned *frames_out)
{
    unsigned pcm_bytes =
        pcm_max_frames * 2u * (unsigned)sizeof(*pcm_out);

    return
        (frames_out && flac_session_output_conflicts(
                           0, frames_out,
                           (unsigned)sizeof(*frames_out))) ||
        (pcm_out && flac_session_output_conflicts(0, pcm_out, pcm_bytes));
}

void flac_mod_entry(const unsigned char *flac_data, unsigned flac_len,
                    short *pcm_out, unsigned pcm_max_frames,
                    unsigned *frames_out)
{
    packed_output output;
    flac_mod_info info;
    unsigned decoded_frames = 0;
    int result;

    int runtime_available = flac_runtime_available();
    int output_conflict;

    /* These checks need no runtime state.  Keep them ahead of the legacy
     * INIT marker so an unavailable/reentrant adapter can never overwrite an
     * aliased input or output while trying to report its failure. */
    output_conflict = flac_packed_ranges_conflict(
        flac_data, flac_len, pcm_out, pcm_max_frames, frames_out);
    if (output_conflict) {
        if (runtime_available &&
            output_conflict == FLAC_DECODE_OUTPUTS_CONFLICT)
            flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return;
    }
    if (!frames_out) {
        if (runtime_available)
            flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return;
    }
    if (!runtime_available) {
        *frames_out = FLAC_MOD_ERROR_MARKER | FLAC_MOD_ERR_INIT;
        return;
    }
    if (flac_packed_arena_conflict(pcm_out, pcm_max_frames, frames_out)) {
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return;
    }
    if (flac_active_session || heap_active) {
        flac_mod_last_error = FLAC_MOD_ERR_STATE;
        return;
    }
    *frames_out = 0;
    output.output = pcm_out;
    output.capacity = pcm_max_frames;
    output.written = 0;
    if (!pcm_out || pcm_max_frames == 0) {
        *frames_out = FLAC_MOD_ERROR_MARKER | FLAC_MOD_ERR_ARGUMENT;
        flac_mod_last_error = FLAC_MOD_ERR_ARGUMENT;
        return;
    }

    result = flac_mod_decode(flac_data, flac_len, packed_sink, &output,
                             &info, &decoded_frames);
    if (result != FLAC_MOD_OK) {
        *frames_out = FLAC_MOD_ERROR_MARKER |
                      (flac_mod_last_error & 0xFFFFu);
        return;
    }
    *frames_out = decoded_frames;
}

/* libFLAC's file-based entry points remain unreachable from this adapter, but
 * the stream decoder object contains a finish path that references fclose. */
#if FLAC_MOD_TARGET_RUNTIME
__asm__(
    ".section .rodata.stdin,\"a\"\n"
    ".align 2\n"
    ".global stdin\n"
    ".type stdin,@object\n"
    ".size stdin,4\n"
    "stdin:\n"
    ".long 0\n"
    ".previous\n");
#else
FILE *stdin = 0;
#endif
int fclose(FILE *file) { (void)file; return 0; }
