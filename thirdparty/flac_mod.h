#ifndef XDJ700_FLAC_MOD_H
#define XDJ700_FLAC_MOD_H

#include <stdint.h>

/*
 * Small, device-independent interface around the freestanding libFLAC
 * decoder.  The decoder core deliberately exposes decoded FLAC samples as
 * signed 32-bit channel planes; the eventual XDJ PCM-task adapter can turn
 * those planes into the format used by the stock audio pipeline without
 * changing the codec itself.
 */

typedef struct {
    unsigned sample_rate;
    unsigned channels;
    unsigned bits_per_sample;
    unsigned total_samples;
} flac_mod_info;

/* Conservative target policy for one decoded FLAC frame.  The FLAC format
 * permits a 65535-sample block, but the current freestanding SH-4 arena is
 * intentionally bounded and cannot safely reserve that worst case alongside
 * the decoder state.  Keep this limit explicit until a device-side RAM
 * budget is measured. */
#define FLAC_MOD_MAX_BLOCKSIZE 4608u

/* The target libFLAC policy (XDJ700_FLAC_BLOCKSIZE_GUARD) also rejects
 * missing-frame gaps instead of allocating a silence-repair frame on the
 * native task stack or producing several PCM callbacks in one step. Builds
 * without that macro retain the upstream library's gap-repair behavior. */

/* libFLAC allocates through the glue's process-wide bounded arena. Storage
 * supplied with flac_mod_init_arena() must meet both of these bounds.  A
 * larger buffer is accepted, but only FLAC_MOD_ARENA_SIZE bytes are used.
 * Define FLAC_MOD_REQUIRE_CALLER_ARENA to a nonzero value when compiling
 * flac_glue.c to omit its linked default arena; decode calls then fail with
 * FLAC_MOD_ERR_INIT until a caller installs valid storage. */
#ifndef FLAC_MOD_ARENA_SIZE
#define FLAC_MOD_ARENA_SIZE (128u * 1024u)
#endif
#define FLAC_MOD_ARENA_ALIGNMENT 8u

/* A pull reader used by the decoder.  Return the number of bytes copied,
 * zero for clean end-of-input, or a negative value for an input error.  A
 * reader may return fewer bytes than requested; libFLAC will ask again. */
typedef int (*flac_mod_input_read)(void *user, unsigned char *buffer,
                                   unsigned bytes);

typedef struct {
    flac_mod_input_read read;
    void *user;
} flac_mod_input;

/* Return zero to accept a decoded frame, non-zero to abort decoding.  The
 * channel planes are signed int32_t storage owned by the decoder and are
 * valid only during this synchronous callback; consume or copy them before
 * returning, do not modify or retain them, and do not re-enter this decoder
 * with the same workspace. */
typedef int (*flac_mod_pcm_sink)(const int32_t * const channels[],
                                 unsigned frame_count,
                                 unsigned channel_count,
                                 unsigned bits_per_sample,
                                 void *user);

enum {
    FLAC_MOD_OK = 0,
    FLAC_MOD_ERR_ARGUMENT = 1,
    FLAC_MOD_ERR_ALLOC = 2,
    FLAC_MOD_ERR_INIT = 3,
    FLAC_MOD_ERR_NO_STREAMINFO = 4,
    FLAC_MOD_ERR_UNSUPPORTED_FORMAT = 5,
    FLAC_MOD_ERR_OUTPUT_FULL = 6,
    FLAC_MOD_ERR_STREAM = 7,
    FLAC_MOD_ERR_DECODER = 8,
    /* Session lifecycle results appended without renumbering the original
     * one-shot error ABI. */
    FLAC_MOD_END = 9,
    FLAC_MOD_ERR_STATE = 10,
    /* Appended so all existing numeric results remain stable.  This means
     * the caller's pull reader returned its documented negative I/O result;
     * malformed callback counts and libFLAC stream failures remain STREAM. */
    FLAC_MOD_ERR_IO = 11
};

#define FLAC_MOD_ERROR_MARKER 0xF1AC0000u

extern volatile unsigned flac_mod_last_error;

/* Install caller-owned storage for subsequent decode calls.  The address
 * must be FLAC_MOD_ARENA_ALIGNMENT-aligned and arena_size must be at least
 * FLAC_MOD_ARENA_SIZE.  The storage must remain valid and exclusively owned
 * by this module until initialization is attempted again or the payload is
 * shut down.  An invalid call made while no decode is active clears the
 * prior configuration (fail closed); a decode cannot proceed until valid
 * storage is installed.  Initialization during a decode is rejected without
 * disturbing that decode's arena. In provider builds, a configured arena
 * span that wraps the address domain or intersects the live provider runtime
 * object is rejected before any provider byte, including diagnostics, is
 * changed. Standalone invalid-initialization behavior remains unchanged.
 *
 * The arena, allocator symbols, owner marker, and flac_mod_last_error are
 * process-wide and non-atomic.  Same-thread reentrant entry is detected and
 * rejected, but true preemptive concurrency is outside this module's
 * synchronization contract: callers must confine it to one task or hold an
 * external lock around initialization and every decode/session operation. */
int flac_mod_init_arena(void *arena, unsigned arena_size);

/* Caller-owned state for one incremental decoder.  Decoder buffers still
 * come exclusively from the configured FLAC_MOD_ARENA_SIZE arena; this
 * object only retains the small control/callback context.  Its members are
 * public solely to make allocation unnecessary and must not be inspected or
 * modified.  The object's address must remain stable from open through close
 * and it must not be copied while open. In provider builds, open and a close
 * that can see runtime state reject a session span intersecting that state
 * before mutation or I/O. When the adapter deliberately hides runtime state,
 * close cannot discover such an alias; callers must confine the legacy
 * teardown destination to an independent session object. */
typedef struct {
    flac_mod_input input;
    flac_mod_pcm_sink sink;
    void *sink_user;
    flac_mod_info *info;
    unsigned frames;
    unsigned streaminfo_seen;
    unsigned eof_during_frame;
    unsigned error;
    unsigned callback_count;
    unsigned callback_limit;
} flac_mod_session_io_state;

typedef struct {
    void *decoder;
    flac_mod_session_io_state io;
    flac_mod_info info;
    unsigned initialized;
    unsigned finished;
    unsigned terminal;
    unsigned end_ready;
    unsigned end_reported;
    unsigned operation_active;
} flac_mod_session;

/* Initialize a decoder without consuming input.  Once serialized by the
 * caller, a nested open observes the process-wide owner and fails with
 * FLAC_MOD_ERR_STATE before invoking input->read.  The owner check is not an
 * atomic concurrency primitive; see the task-confinement/external-lock rule
 * above. session must not overlap input or the configured decoder arena. An
 * unsafe alias returns FLAC_MOD_ERR_ARGUMENT without modifying the session,
 * input descriptor, or arena and without invoking input->read. Every non-null
 * session and input-descriptor span must fit the target address domain. A
 * provider-backed input descriptor must also be independent of the visible
 * runtime-state object; both checks precede reading input->read. */
int flac_mod_session_open(flac_mod_session *session,
                          const flac_mod_input *input);

/* Decode at most one audio frame with FLAC__stream_decoder_process_single().
 * FLAC_MOD_OK means one synchronous sink callback was accepted and
 * frames_out names that block's frame count.  sink and frames_out are
 * required; a missing sink returns FLAC_MOD_ERR_ARGUMENT before consuming
 * input and leaves the session usable.  FLAC_MOD_END is returned once after
 * clean end-of-stream.  A negative input callback result is preserved as
 * FLAC_MOD_ERR_IO.  An input/sink/decode error makes the session terminal;
 * calls after END or an error return FLAC_MOD_ERR_STATE. info and frames_out
 * must not overlap each other, the session object, the configured decoder
 * arena, or a visible provider runtime-state object. Such an alias returns
 * FLAC_MOD_ERR_ARGUMENT without modifying the aliased bytes, diagnostics,
 * session or provider state, consuming input, or invoking the sink. Every
 * non-null output span must fit the target address domain. */
int flac_mod_session_next(flac_mod_session *session,
                          flac_mod_pcm_sink sink, void *sink_user,
                          flac_mod_info *info, unsigned *frames_out);

/* Release all libFLAC allocations and the process-wide arena ownership.
 * Safe after success, failure, or cancellation, and idempotent. During an
 * active callback, close on the exact owner or on an interior pointer into
 * the owner/arena is a transactional no-op; it cannot invalidate live stack
 * state. An ordinary independent non-owner object is still cleared. A wrapped
 * session destination is always a no-op, including when there is no active
 * owner or provider runtime is deliberately hidden. */
void flac_mod_session_close(flac_mod_session *session);

int flac_mod_decode_input(const flac_mod_input *input,
                          flac_mod_pcm_sink sink, void *sink_user,
                          flac_mod_info *info, unsigned *frames_out);

/* Non-null, nonempty input-descriptor, encoded-source, and writable-output
 * spans must fit the target address domain. One-shot outputs must not overlap
 * each other, the original generic, sequential-file, random-file, or bounded-
 * file input descriptor, the
 * configured decoder arena, a visible provider runtime-state object, or (for
 * flac_mod_decode) the encoded byte range. Unsafe aliases return
 * FLAC_MOD_ERR_ARGUMENT before a wrapper snapshots its descriptor or validates
 * the encoded pointer/length pair, without modifying either output/input
 * range, provider diagnostics, or consuming input. Wrapped spans are unsafe
 * even when their wrapped portion would otherwise appear disjoint. A null or
 * zero-length encoded input with distinct outputs retains the established
 * output-clearing error behavior. Readable input spans are checked even when
 * both output pointers are absent. */
int flac_mod_decode(const unsigned char *data, unsigned data_length,
                    flac_mod_pcm_sink sink, void *sink_user,
                    flac_mod_info *info, unsigned *frames_out);

/* The XDJ-700 parser trace identifies a generic sequential storage operation
 * as a five-argument call: destination, operation, byte count, file handle,
 * and context.  This legacy adapter exposes that shape without embedding any
 * firmware address or proprietary code in the decoder.  It deliberately
 * provides no file offset; callers using the recovered random-access parser
 * boundary should use flac_mod_decode_file_at() below. */
typedef int (*flac_mod_file_operation)(void *destination,
                                       unsigned operation,
                                       unsigned bytes,
                                       void *file_handle,
                                       void *context);

typedef struct {
    flac_mod_file_operation operation;
    void *file_handle;
    void *context;
} flac_mod_file_input;

#define FLAC_MOD_FILE_READ_OPERATION 1u

int flac_mod_decode_file(const flac_mod_file_input *input,
                         flac_mod_pcm_sink sink, void *sink_user,
                         flac_mod_info *info, unsigned *frames_out);

/* Random-access reader matching the semantics of the recovered parser read
 * wrapper: return zero for a completed operation and write the number of
 * bytes actually copied to *bytes_read.  A positive short read is continued
 * by the sequential decoder adapter; zero bytes signals end-of-input.  Return
 * a nonzero status for an I/O failure.  The offset is a 32-bit file offset,
 * matching the target ABI. */
typedef int (*flac_mod_read_at)(void *user, unsigned offset,
                                void *destination, unsigned bytes,
                                unsigned *bytes_read);

typedef struct {
    flac_mod_read_at read_at;
    void *user;
} flac_mod_random_input;

int flac_mod_decode_file_at(const flac_mod_random_input *input,
                            flac_mod_pcm_sink sink, void *sink_user,
                            flac_mod_info *info, unsigned *frames_out);

/* A bounded view over an underlying random-access source. Offsets presented
 * to read_at() are relative to this view; base_offset is added only after
 * range and 32-bit overflow checks. length is mandatory and defines EOF, so a
 * successful zero-byte callback below length is reported as FLAC_MOD_ERR_IO.
 * max_request may model a backend's largest safe transfer (zero means no
 * extra cap). */
typedef struct {
    flac_mod_read_at read_at;
    void *user;
    unsigned base_offset;
    unsigned length;
    unsigned max_request;
} flac_mod_bounded_random_input;

int flac_mod_decode_file_at_bounded(
    const flac_mod_bounded_random_input *input,
    flac_mod_pcm_sink sink, void *sink_user,
    flac_mod_info *info, unsigned *frames_out);

/* Provisional packed-output adapter used by the standalone harness.  It is
 * not the stock XDJ PCM ABI; the hook must be adapted after that ABI is
 * proven from the live application.  Output is interleaved stereo signed
 * 16-bit, with mono duplicated to both channels. The PCM and frame-count
 * outputs must not overlap each other, the encoded input, the configured
 * decoder arena, or a visible provider runtime-state object, and the declared
 * PCM span must fit the target address domain. Unsafe ranges set
 * FLAC_MOD_ERR_ARGUMENT without modifying caller bytes or provider diagnostics.
 * A same-thread nested call sets FLAC_MOD_ERR_STATE and likewise
 * leaves both outputs unchanged. If target runtime state is unavailable, a
 * safe non-null frame output receives the legacy INIT error marker; null or
 * statelessly unsafe outputs remain untouched. */
void flac_mod_entry(const unsigned char *flac_data, unsigned flac_len,
                    short *pcm_out, unsigned pcm_max_frames,
                    unsigned *frames_out);

#endif
