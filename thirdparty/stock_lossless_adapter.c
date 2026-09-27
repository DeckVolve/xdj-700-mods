#include "stock_lossless_adapter.h"
#include "xdj700_non_audio_diagnostic.h"

#include <stddef.h>
#include <stdint.h>

#ifndef STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
#define STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE 0
#endif
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE != 0 && \
    STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE != 1
#error "STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE must be 0 or 1"
#endif
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE && \
    !defined(STOCK_LOSSLESS_GATE_HOOK_RETURN_PR)
#error "STOCK_LOSSLESS_GATE_HOOK_RETURN_PR is required by production wrappers"
#endif

#include "lossless_wave_bridge.h"
#include "retained_format_router.h"
#include "dual_format_runtime.h"
#include "stock_lossless_sync.h"
#ifndef STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
#define STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD 0
#endif
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD != 0 && \
    STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD != 1
#error "ALAC native-fault record must be disabled or enabled"
#endif
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
#include "alac_native_fault_record.h"
/* GCC-specific, opt-in research layout. This is not a target stack proof. */
#define STOCK_NFR_COMPACT __attribute__((optimize("Os")))
#ifndef STOCK_LOSSLESS_ALAC_NATIVE_FAULT_TEST_HOOK
#define STOCK_LOSSLESS_ALAC_NATIVE_FAULT_TEST_HOOK(fault_) ((void)(fault_))
#endif
#endif
#if STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
#include "stock_lossless_trial_checkpoint.h"
#endif

#ifndef STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
#define STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE 0
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE < 0 || \
    STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE > 2
#error "diagnostic mode must be 0 (disabled), 1 (FLAC), or 2 (ALAC)"
#endif
#ifndef STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN
#define STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN 0
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN != 0 && \
    STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN != 1
#error "decoder-drain diagnostic must be 0 or 1"
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN && \
    !STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
#error "decoder drain requires an exact-fixture diagnostic mode"
#endif
#ifndef STOCK_LOSSLESS_NON_AUDIO_OPEN_ONLY
#define STOCK_LOSSLESS_NON_AUDIO_OPEN_ONLY 0
#endif
#if STOCK_LOSSLESS_NON_AUDIO_OPEN_ONLY != 0 && \
    STOCK_LOSSLESS_NON_AUDIO_OPEN_ONLY != 1
#error "open-only diagnostic must be 0 or 1"
#endif
#if STOCK_LOSSLESS_NON_AUDIO_OPEN_ONLY && \
    (STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE != 2 || \
     STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN)
#error "open-only diagnostic requires ALAC mode without decoder drain"
#endif
#ifndef STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
#define STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE 0
#endif
#if STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE < 0 || \
    STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE > 2
#error "trial exact-fixture mode must be 0 (disabled), 1 (FLAC), or 2 (ALAC)"
#endif
#if STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE && \
    STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
#error "trial exact-fixture playback and non-audio diagnostic modes conflict"
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
#include "stock_lossless_non_audio_runtime.h"
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE || \
    STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
#include "xdj700_non_audio_fixture.h"
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
#if !XDJ700_NON_AUDIO_DIAGNOSTIC_ENABLE || !STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
#error "diagnostic adapter requires enabled diagnostic core and integrity gate"
#endif
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE || \
    STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
extern const unsigned char _binary_diagnostic_fixture_start[];
extern const unsigned char _binary_diagnostic_fixture_end[];
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
const uint32_t stock_lossless_non_audio_diagnostic_mode
    __attribute__((section(".rodata.stock_lossless_non_audio_policy"), used)) =
        STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE;
const uint32_t stock_lossless_non_audio_decoder_drain
    __attribute__((section(".rodata.stock_lossless_non_audio_policy"), used)) =
        STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN;
#if STOCK_LOSSLESS_NON_AUDIO_OPEN_ONLY
const uint32_t stock_lossless_non_audio_open_only
    __attribute__((section(".rodata.stock_lossless_non_audio_policy"), used)) =
        STOCK_LOSSLESS_NON_AUDIO_OPEN_ONLY;
#endif
#endif
#if STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
const uint32_t stock_lossless_trial_exact_fixture_mode
    __attribute__((section(".rodata.stock_lossless_trial_policy"), used)) =
        STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE;

static uint32_t *stock_trial_result(void)
{
    return (uint32_t *)(uintptr_t)UINT32_C(0x08000740);
}

static int stock_trial_checkpoint(uint32_t from, uint32_t to)
{
    return stock_lossless_trial_checkpoint_word(
        stock_trial_result(), from, to, XDJ700_NAD_RESULT_1_14);
}
#endif

#ifndef STOCK_LOSSLESS_ADAPTER_CURRENT_RETURN_PR
#define STOCK_LOSSLESS_ADAPTER_CURRENT_RETURN_PR() \
    ((uintptr_t)__builtin_return_address(0))
#endif
#ifndef STOCK_LOSSLESS_ADAPTER_AFTER_ACTIVATE_TEST_HOOK
#define STOCK_LOSSLESS_ADAPTER_AFTER_ACTIVATE_TEST_HOOK(control_, source_, token_) \
    ((void)(control_), (void)(source_), (void)(token_))
#endif

/* Retained in the flat payload so production builders can attest that direct
 * IDLE adapter ingress was compiled out. */
const uint32_t stock_lossless_integrity_gate_required
    __attribute__((section(".rodata.stock_lossless_integrity_gate_policy"),
                   used)) = STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE;
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
/* Exact incoming PR required by the destructive close path.  Retain the word
 * so package builders can bind the payload and gate-common call site. */
const uint32_t stock_lossless_gate_hook_return_pr
    __attribute__((section(".rodata.stock_lossless_integrity_gate_policy"),
                   used)) = STOCK_LOSSLESS_GATE_HOOK_RETURN_PR;
#endif

/* Exact XDJ-700 v1.15 integration facts covered by the companion target
 * regression.  The source-manager descriptor is a singleton at manager+0x54.
 * Its +0x20 word is initialized to 0x00008008 and is retained unchanged as an
 * integration guard.  Synchronization instead lives in the independently
 * reserved 20-byte control block.  Target code accesses that physical block
 * only through its uncached P2 alias, avoiding dirty-data/clean-instruction
 * aliases on the cache line shared with the relocated selector continuation. */
#ifndef STOCK_MANAGER_ADDRESS
#define STOCK_MANAGER_ADDRESS       ((uintptr_t)0x09D2825Cu)
#endif
#define STOCK_DESCRIPTOR_OFFSET     0x54u
#define STOCK_DESCRIPTOR_WORDS      11u
#define STOCK_DESCRIPTOR_SLOT_WORD  8u
#define STOCK_DESCRIPTOR_SLOT_GUARD UINT32_C(0x00008008)
#define STOCK_SELECTOR_OFFSET       8u
/* Minimum live native FAT file-object span evidenced by stock read/seek:
 * cursor fields through +0x2f and a 512-byte buffer at +0x38..+0x237.
 * The full allocation extent is not established by this constant. */
#define STOCK_NATIVE_FILE_LIVE_MIN_BYTES 0x238u

#ifndef STOCK_LOSSLESS_CONTROL_ADDRESS
#define STOCK_LOSSLESS_CONTROL_ADDRESS ((uintptr_t)0xA8C01E84u)
#endif

#define STOCK_READ_ADDRESS          ((uintptr_t)0x08C5635Eu)
#ifndef STOCK_SIZE_ADDRESS
#define STOCK_SIZE_ADDRESS          ((uintptr_t)0x08DE0E7Au)
#endif
#ifndef STOCK_POSITION_ADDRESS
#define STOCK_POSITION_ADDRESS      ((uintptr_t)0x08DE0364u)
#endif
#ifndef STOCK_SEEK_ADDRESS
#define STOCK_SEEK_ADDRESS          ((uintptr_t)0x08DE00D6u)
#endif
#define STOCK_EOF_ADDRESS           ((uintptr_t)0x08DE0510u)
#define STOCK_TRANSFER_ADDRESS      ((uintptr_t)0x08DDFC96u)
#ifndef STOCK_CLOSE_ADDRESS
#define STOCK_CLOSE_ADDRESS         ((uintptr_t)0x08DDF992u)
#endif
#ifndef STOCK_ALLOCATE_ADDRESS
#define STOCK_ALLOCATE_ADDRESS      ((uintptr_t)0x08B4C30Cu)
#endif
#ifndef STOCK_RELEASE_ADDRESS
#define STOCK_RELEASE_ADDRESS       ((uintptr_t)0x08B4C352u)
#endif

#define STOCK_SELECTOR_FLAC_CANDIDATE 5u
#define STOCK_SELECTOR_ALAC_OBSERVED    6u
#define STOCK_SELECTOR_WAVE           11u
#define STOCK_SELECTOR_TERMINAL       UINT32_MAX

#define STOCK_ADAPTER_MAGIC UINT32_C(0x4C534131) /* "LSA1" */
#define STOCK_STATE_ALIGNMENT 8u
#define STOCK_SHARED_ALIGNMENT DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_ALIGNMENT
#define STOCK_SHARED_BYTES DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_SIZE
#ifndef STOCK_FIFO_BYTES
#define STOCK_FIFO_BYTES LOSSLESS_WAVE_BRIDGE_MAX_FIFO_BYTES
#endif
#define STOCK_DRAIN_SCRATCH_BYTES 256u
#define STOCK_DRAIN_MAX_READS \
    ((XDJ700_NAD_PCM_BYTES + STOCK_DRAIN_SCRATCH_BYTES - 1u) / \
     STOCK_DRAIN_SCRATCH_BYTES)
#define STOCK_DRAIN_EXTRA_BYTES \
    (STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN ? STOCK_DRAIN_SCRATCH_BYTES : 0u)
#define STOCK_MAX_WAVE_BYTES ((uint64_t)INT32_MAX)
#define STOCK_MAX_FRAMES ((STOCK_MAX_WAVE_BYTES - 44u) / 4u)
/* Bound public ALAC preflight even in the general build.  This covers more
 * than twice the maximum 131,072 ordinary 4,096-frame packets that fit under
 * STOCK_MAX_FRAMES, plus chunk/table overhead, while malformed files cannot
 * drive an unbounded table walk. */
#define STOCK_GENERAL_ALAC_MAX_WORK 524288u

#ifndef STOCK_LOSSLESS_ENABLE_ALAC
#define STOCK_LOSSLESS_ENABLE_ALAC 0
#endif
#ifndef STOCK_LOSSLESS_ALAC_SELECTOR6_ONLY
#define STOCK_LOSSLESS_ALAC_SELECTOR6_ONLY 0
#endif
#if STOCK_LOSSLESS_ALAC_SELECTOR6_ONLY != 0 && \
    STOCK_LOSSLESS_ALAC_SELECTOR6_ONLY != 1
#error "selector-6-only ALAC policy must be zero or one"
#endif
#if STOCK_LOSSLESS_ALAC_SELECTOR6_ONLY && !STOCK_LOSSLESS_ENABLE_ALAC
#error "selector-6-only ALAC policy requires ALAC mode"
#endif
#if (STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE == 1 && STOCK_LOSSLESS_ENABLE_ALAC) || \
    (STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE == 2 && !STOCK_LOSSLESS_ENABLE_ALAC)
#error "diagnostic fixture mode must match the explicit stock ALAC policy"
#endif

/* Preserve the lower bound used by existing integrations and include the
 * native RTOS allocation arena [0x0C90956C, 0x0E70956C). Firmware words
 * 0x080B303C and 0x080B3014 configure its base and 0x01E00000-byte size.
 * Both area 2 and area 3 are DDR windows on SH7734. The previous area-2-only
 * ceiling rejected native pool-6 allocations before the first state write.
 * Host tests may override these bounds with their statically allocated arenas.
 */
#ifndef STOCK_STATE_ADDRESS_MIN
#define STOCK_STATE_ADDRESS_MIN ((uintptr_t)XDJ700_NAD_DDR_MIN)
#endif
#ifndef STOCK_STATE_ADDRESS_MAX_EXCLUSIVE
#define STOCK_STATE_ADDRESS_MAX_EXCLUSIVE ((uintptr_t)XDJ700_NAD_DDR_MAX_EXCLUSIVE)
#endif

typedef int (*stock_read_fn)(void *, void *, unsigned, unsigned, unsigned *);
typedef int (*stock_size_fn)(void *, unsigned *, void *);
typedef int (*stock_position_fn)(void *, void *);
typedef int (*stock_seek_fn)(void *, int, int, void *);
typedef int (*stock_eof_fn)(void *, void *);
typedef int (*stock_transfer_fn)(void *, int, unsigned, void *, void *);
typedef int (*stock_close_fn)(void *, void *, void *, void *);
typedef void *(*stock_allocate_fn)(unsigned);
typedef int (*stock_release_fn)(void *);

/* Implemented in stock_lossless_veneer.S.  It reproduces the six displaced
 * register saves and resumes the unmodified stock read body at 0x08C5636A. */
extern int stock_lossless_read_original(
    void *, void *, unsigned, unsigned, unsigned *);

typedef struct {
    uint32_t descriptor[STOCK_DESCRIPTOR_WORDS];
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    alac_nfr_record native_fault;
    uint32_t native_fault_expected_token;
#endif
} stock_lossless_reader;

typedef struct {
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    xdj700_nad_session diagnostic;
#endif
    uint32_t magic;
    void *allocation;
    unsigned allocation_bytes;
    void *manager;
    void *descriptor;
    void *file;
    uint32_t *source;
    uint32_t source_selector;
    uint32_t cursor;
    uint32_t terminal_error;
    dual_format_runtime_state runtime;
    stock_lossless_reader reader;
    lossless_wave_bridge bridge;
} stock_lossless_state;

#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
typedef char stock_diagnostic_session_must_be_first[
    offsetof(stock_lossless_state, diagnostic) == 0u &&
    STOCK_STATE_ALIGNMENT >= XDJ700_NAD_SESSION_ALIGNMENT ? 1 : -1];
#endif

typedef char stock_descriptor_slot_must_be_within_copy[
    STOCK_DESCRIPTOR_SLOT_WORD < STOCK_DESCRIPTOR_WORDS ? 1 : -1];
#ifndef STOCK_LOSSLESS_TEST_ALLOW_UNDERSIZED_FIFO
typedef char stock_fifo_must_cover_bridge_envelope[
    STOCK_FIFO_BYTES >= LOSSLESS_WAVE_BRIDGE_MAX_FIFO_BYTES ? 1 : -1];
#endif

static stock_lossless_sync_control *stock_sync_control(void)
{
    return (stock_lossless_sync_control *)STOCK_LOSSLESS_CONTROL_ADDRESS;
}

static stock_lossless_state *stock_state_from_word(uint32_t state_word)
{
    uintptr_t address = (uintptr_t)state_word;

    if (address < STOCK_STATE_ADDRESS_MIN ||
        address >= STOCK_STATE_ADDRESS_MAX_EXCLUSIVE ||
        (address & (STOCK_STATE_ALIGNMENT - 1u)) != 0u ||
        sizeof(stock_lossless_state) >
            STOCK_STATE_ADDRESS_MAX_EXCLUSIVE - address)
        return 0;
    return (stock_lossless_state *)address;
}

static int stock_alac_candidate_selector(uint32_t selector)
{
    /* The passive real-deck observer reported selector 6 for the pinned
     * ALAC/M4A load.  An opt-in paired gate/adapter policy excludes 2/3/4
     * entirely, preserving the stock AAC path without probing those files.
     * Positive container routing, not selector 6 alone, admits ALAC. */
#if STOCK_LOSSLESS_ALAC_SELECTOR6_ONLY
    return selector == STOCK_SELECTOR_ALAC_OBSERVED;
#else
    return selector == 2u || selector == 3u || selector == 4u ||
           selector == STOCK_SELECTOR_ALAC_OBSERVED;
#endif
}

static int stock_state_valid(const stock_lossless_state *state)
{
    if (state == 0)
        return 0;
    if (state->magic != STOCK_ADAPTER_MAGIC ||
        state->allocation == 0 || state->allocation_bytes == 0u ||
        state->manager != (void *)STOCK_MANAGER_ADDRESS ||
        state->descriptor != (void *)(STOCK_MANAGER_ADDRESS +
                                      STOCK_DESCRIPTOR_OFFSET) ||
        state->file == 0 || state->source == 0 ||
        (state->source_selector != STOCK_SELECTOR_FLAC_CANDIDATE &&
         !stock_alac_candidate_selector(state->source_selector)))
        return 0;
    return 1;
}

static int stock_descriptor_matches(const uint32_t *descriptor,
                                    const void *file)
{
    return descriptor != 0 && file != 0 &&
           descriptor[0] == 0u &&
           descriptor[1] == (uint32_t)(uintptr_t)file &&
           descriptor[STOCK_DESCRIPTOR_SLOT_WORD] ==
               STOCK_DESCRIPTOR_SLOT_GUARD;
}

#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
/* Numeric snapshots only. Never follow a control-provided descriptor/state
 * pointer: the singleton's mapped address is an existing adapter constant.
 * The caller owns the exact expected token; this helper does not acquire it. */
static alac_nfr_snapshot stock_nfr_snapshot(void)
{
    stock_lossless_sync_control *control = stock_sync_control();
    const uint32_t *live = (const uint32_t *)(STOCK_MANAGER_ADDRESS +
                                               STOCK_DESCRIPTOR_OFFSET);
    alac_nfr_snapshot s;

    s.lifecycle = stock_sync_load(&control->lifecycle);
    s.file_key = stock_sync_load(&control->file_key);
    s.descriptor_key = stock_sync_load(&control->descriptor_key);
    s.dynamic_state = stock_sync_load(&control->dynamic_state);
    s.slot_guard = stock_sync_load(&control->slot_guard);
    s.live_descriptor_file = live[0] == 0u ? live[1] : 0u;
    s.live_descriptor_slot = live[STOCK_DESCRIPTOR_SLOT_WORD];
    if ((uintptr_t)live > UINT32_MAX ||
        s.descriptor_key != (uint32_t)(uintptr_t)live ||
        stock_sync_load(&control->lifecycle) != s.lifecycle)
        s.lifecycle = 0u;
    return s;
}

static int stock_nfr_reader_copy_matches(const stock_lossless_reader *reader)
{
    return reader->descriptor[0] == 0u &&
        reader->descriptor[1] == reader->native_fault.file_key &&
        reader->descriptor[STOCK_DESCRIPTOR_SLOT_WORD] ==
            reader->native_fault.slot_guard;
}

static void stock_nfr_emit(stock_lossless_reader *reader,
                           alac_nfr_snapshot before, alac_nfr_snapshot after)
{
    alac_nfr_fault fault;

    if (alac_nfr_take_first(&reader->native_fault, before, after, &fault))
        STOCK_LOSSLESS_ALAC_NATIVE_FAULT_TEST_HOOK(fault);
}

/* The callback only records. An owned outer caller must reattest numeric
 * binding after the bridge returns, before its exact token is released. */
static STOCK_NFR_COMPACT void stock_nfr_emit_fresh(stock_lossless_reader *reader,
                                 uint32_t expected_token)
{
    alac_nfr_snapshot before;
    alac_nfr_snapshot after;

    if (reader->native_fault.first_kind == ALAC_NFR_NONE ||
        reader->native_fault.published != 0u)
        return;
    if (expected_token == 0u ||
        reader->native_fault_expected_token != expected_token ||
        !stock_nfr_reader_copy_matches(reader)) {
        alac_nfr_discard(&reader->native_fault);
        return;
    }
    before = stock_nfr_snapshot();
    after = stock_nfr_snapshot();
    if (before.lifecycle != expected_token ||
        after.lifecycle != expected_token) {
        alac_nfr_discard(&reader->native_fault);
        return;
    }
    stock_nfr_emit(reader, before, after);
}

static STOCK_NFR_COMPACT void stock_nfr_recognize(stock_lossless_reader *reader,
                                enum alac_nfr_disposition disposition)
{
    alac_nfr_snapshot before = stock_nfr_snapshot();
    alac_nfr_snapshot after = stock_nfr_snapshot();

    if (reader->native_fault_expected_token == 0u ||
        before.lifecycle != reader->native_fault_expected_token ||
        after.lifecycle != reader->native_fault_expected_token ||
        !stock_nfr_reader_copy_matches(reader)) {
        alac_nfr_discard(&reader->native_fault);
        return;
    }
    if (alac_nfr_recognize(&reader->native_fault, before, after, disposition))
        stock_nfr_emit(reader, before, after);
}
#endif

dual_format_runtime_state *dual_format_runtime_current(void)
    __attribute__((used, section(".text.dual_format_runtime_current")));

dual_format_runtime_state *dual_format_runtime_current(void)
{
    stock_lossless_sync_control *control = stock_sync_control();
    uint32_t lifecycle = stock_sync_load(&control->lifecycle);
    uint32_t phase = stock_sync_phase_of(lifecycle);
    stock_lossless_state *state;

    /* Reject a corrupted generation-zero owner before the dynamic-state read.
     * These are the exclusive phases in which codec callbacks can run.
     * OPENING/OPEN_CLEANUP belong to dispatch or close teardown and
     * ACTIVE_BUSY to one wrapper.  Cancellation keeps callbacks visible only
     * to the in-flight opening/operation owner until it rolls back or leaves.
     * Plain ACTIVE, CLOSING, CLOSE_FAILED, and POISONED expose no pointer. */
    if ((lifecycle & STOCK_SYNC_GENERATION_MASK) == 0u ||
        (phase != STOCK_SYNC_OPENING &&
        phase != STOCK_SYNC_OPEN_CLEANUP &&
        phase != STOCK_SYNC_ACTIVE_BUSY &&
        phase != STOCK_SYNC_CANCEL_OPENING))
        return 0;
    state = stock_state_from_word(stock_sync_load(&control->dynamic_state));
    return stock_state_valid(state) ? &state->runtime : 0;
}

static enum stock_sync_route stock_operation_begin(
    uintptr_t owner, enum stock_sync_identity identity,
    stock_lossless_state **state_out, uint32_t *busy_token_out)
{
    stock_lossless_sync_control *control = stock_sync_control();
    uint32_t state_word = 0u;
    uint32_t busy_token = 0u;
    stock_lossless_state *state;
    enum stock_sync_route route;

    if (state_out != 0)
        *state_out = 0;
    if (owner == 0u || owner > UINT32_MAX)
        return STOCK_SYNC_ROUTE_REJECT;
    route = stock_sync_operation_enter(
        control, (uint32_t)owner, identity, &state_word, &busy_token);
    if (route != STOCK_SYNC_ROUTE_OWNED) {
        if (route == STOCK_SYNC_ROUTE_DELEGATE && busy_token_out != 0)
            *busy_token_out = busy_token;
        return route;
    }
    state = stock_state_from_word(state_word);
    if (!stock_state_valid(state) ||
        (identity == STOCK_SYNC_IDENTITY_FILE &&
         state->file != (void *)owner) ||
        (identity == STOCK_SYNC_IDENTITY_DESCRIPTOR &&
         state->descriptor != (void *)owner)) {
        (void)stock_sync_poison_exact(control, busy_token);
        return STOCK_SYNC_ROUTE_POISONED;
    }
    if (!stock_descriptor_matches(
            (const uint32_t *)state->descriptor, state->file)) {
        /* An owner-matching virtual-WAVE caller may still use this singleton
         * after another native operation rebinds it.  Delegating that call
         * could return a different file's native bytes or compressed size.
         * Relinquish our busy token, then fail closed without touching the
         * caller's output.  A genuinely unrelated file-key call delegates
         * earlier in stock_sync_operation_enter; a distinct reader through
         * this same singleton cannot be distinguished here. */
        if (!stock_sync_operation_leave(control, busy_token))
            return STOCK_SYNC_ROUTE_POISONED;
        busy_token = stock_sync_with_phase(
            busy_token, STOCK_SYNC_ACTIVE);
        if (stock_sync_load(&control->lifecycle) != busy_token)
            return STOCK_SYNC_ROUTE_REJECT;
        return STOCK_SYNC_ROUTE_REJECT;
    }
    if (state_out != 0)
        *state_out = state;
    if (busy_token_out != 0)
        *busy_token_out = busy_token;
    return STOCK_SYNC_ROUTE_OWNED;
}

#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
static STOCK_NFR_COMPACT int stock_reader_read_at(void *user, uint64_t offset,
#else
static int stock_reader_read_at(void *user, uint64_t offset,
#endif
                                void *destination, unsigned count,
                                unsigned *bytes_read)
{
    stock_lossless_reader *reader = (stock_lossless_reader *)user;
    unsigned total = 0u;

    /* The native reader takes a 32-bit absolute offset.  Reject an entire
     * unrepresentable request before its first call: otherwise a valid first
     * sector can alter the destination even though the later sector must
     * fail, while the published count is reset to zero. */
    if (reader == 0 || bytes_read == 0 ||
        (count != 0u && destination == 0) || offset > UINT32_MAX ||
        (count != 0u && (uint64_t)count - 1u > UINT32_MAX - offset)) {
        if (bytes_read != 0)
            *bytes_read = 0u;
        return 1;
    }
    *bytes_read = 0u;
    while (total < count) {
        uint64_t piece_offset = offset + total;
        unsigned piece = 0u;
        unsigned remaining = count - total;
        unsigned sector_room = 0x200u - ((unsigned)piece_offset & 0x1ffu);
        unsigned request = remaining > sector_room ? sector_room : remaining;
        int result;
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
        alac_nfr_snapshot fault_before;
        alac_nfr_snapshot fault_after;
#endif

        if (piece_offset > UINT32_MAX) {
            *bytes_read = 0u;
            return 1;
        }
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
        fault_before = stock_nfr_snapshot();
#endif
        result = stock_lossless_read_original(
            reader->descriptor, (unsigned char *)destination + total,
            (unsigned)piece_offset, request, &piece);
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
        fault_after = stock_nfr_snapshot();
        if (reader->native_fault_expected_token != 0u &&
            fault_before.lifecycle == reader->native_fault_expected_token &&
            fault_after.lifecycle == reader->native_fault_expected_token &&
            stock_nfr_reader_copy_matches(reader)) {
            (void)alac_nfr_native_result(&reader->native_fault,
                fault_before, fault_after, result, request, piece);
        } else {
            alac_nfr_discard(&reader->native_fault);
        }
#endif
        /* The native storage path is sector-oriented: the retained bridge
         * must never ask the stock reader for more than one 0x200-byte media
         * sector at a time.  A successful native call returns zero and
         * publishes the transferred byte count.  Reject every inconsistent
         * or signed-error combination. */
        if (result != 0 || piece > request) {
            *bytes_read = 0u;
            return 1;
        }
        if (piece == 0u)
            break;
        total += piece;
    }
    *bytes_read = total;
    return 0;
}

static void stock_copy_descriptor(stock_lossless_reader *destination,
                                  const uint32_t *source)
{
    unsigned index;

    for (index = 0u; index < STOCK_DESCRIPTOR_WORDS; ++index)
        destination->descriptor[index] = source[index];
}

static void stock_reset_source_parser_outputs(uint32_t *source)
{
    unsigned index;

    /* The stock selector-11 parser owns source+12 through source+28 for the
     * lifetime of the selected track.  A direct reload reuses the source
     * record after that lifetime and before parsing it again.  Reset those
     * five parser outputs only after this source has been positively
     * recognized as a lossless route and immediately before a new parser
     * session is prepared.  Clearing them at close is too early: the native
     * PCM/file teardown still consumes them after its close callback. */
    for (index = STOCK_SELECTOR_OFFSET / 4u + 1u;
         index <= STOCK_SELECTOR_OFFSET / 4u + 5u; ++index)
        source[index] = 0u;
}

static void stock_zero_bytes(void *memory, unsigned count)
{
    unsigned char *bytes = (unsigned char *)memory;
    unsigned index;

    for (index = 0u; index < count; ++index)
        bytes[index] = 0u;
}

static int stock_restore_position(void *file, int position,
                                  stock_lossless_reader *reader)
{
    stock_seek_fn seek = (stock_seek_fn)STOCK_SEEK_ADDRESS;
    stock_position_fn position_fn = (stock_position_fn)STOCK_POSITION_ADDRESS;

    /* The probe and decoder borrow the same native file object as stock.
     * A zero seek result alone does not establish that its cursor was
     * restored before stock resumes or the custom generation retires. */
    if (seek(file, position, 0, reader->descriptor + 2u) != 0 ||
        position_fn(file, reader->descriptor + 2u) != position)
        return 1;
    return 0;
}

static void stock_set_terminal_selector(uint32_t *source)
{
    if (source != 0)
        source[STOCK_SELECTOR_OFFSET / 4u] = STOCK_SELECTOR_TERMINAL;
}

#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
static int stock_diagnostic_owner(stock_lossless_state *state,
                                  uint32_t token, uint32_t kind,
                                  xdj700_nad_owner *owner)
{
    return stock_lossless_non_audio_capture_owner(
        kind, token, (uint32_t)(uintptr_t)state, 0, owner);
}

static int stock_diagnostic_open_owner(stock_lossless_state *state,
                                       uint32_t claim,
                                       xdj700_nad_owner *owner)
{
    uint32_t token = stock_sync_load(&stock_sync_control()->lifecycle);
    uint32_t phase = stock_sync_phase_of(token);
    if ((token & STOCK_SYNC_GENERATION_MASK) !=
            (claim & STOCK_SYNC_GENERATION_MASK) ||
        (phase != STOCK_SYNC_OPENING && phase != STOCK_SYNC_CANCEL_OPENING))
        return 0;
    return stock_diagnostic_owner(state, token,
        phase == STOCK_SYNC_OPENING ? XDJ700_NAD_OWNER_OPENING :
                                      XDJ700_NAD_OWNER_CANCEL_OPENING, owner);
}

static void stock_diagnostic_fail(stock_lossless_state *state,
                                   const xdj700_nad_owner *owner)
{
    enum xdj700_nad_status status = xdj700_nad_fail_generation(
        &state->diagnostic, stock_lossless_non_audio_island(),
        owner->before.lifecycle & STOCK_SYNC_GENERATION_MASK, owner);
    /* This API intentionally returns REJECTED after latching a failure. */
    (void)status;
}

static int stock_diagnostic_prepare(stock_lossless_state *state,
                                    const xdj700_nad_owner *owner,
                                    xdj700_nad_ticket *ticket, int *failure)
{
    uint32_t generation = owner->before.lifecycle & STOCK_SYNC_GENERATION_MASK;
    enum xdj700_nad_status status;
    *failure = stock_lossless_non_audio_failure_latched(generation);
    if (*failure)
        status = xdj700_nad_prepare_failure_cleanup(
            &state->diagnostic, stock_lossless_non_audio_island(),
            stock_lossless_non_audio_result(), owner, ticket);
    else {
        status = xdj700_nad_prepare_cleanup(
            &state->diagnostic, stock_lossless_non_audio_island(),
            stock_lossless_non_audio_result(), owner, ticket);
        /* A malformed normal completion may latch failure.  WAITING cannot
         * occur while CLOSE owns the lifecycle and must strand resources. */
        if (status == XDJ700_NAD_REJECTED &&
            stock_lossless_non_audio_failure_latched(generation)) {
            *failure = 1;
            status = xdj700_nad_prepare_failure_cleanup(
                &state->diagnostic, stock_lossless_non_audio_island(),
                stock_lossless_non_audio_result(), owner, ticket);
        }
    }
    return status == XDJ700_NAD_OK;
}

static int stock_diagnostic_retire(stock_lossless_state *state,
                                   uint32_t cleanup_token,
                                   const xdj700_nad_ticket *ticket,
                                   int failure)
{
    stock_lossless_sync_control *control = stock_sync_control();
    stock_release_fn release = (stock_release_fn)STOCK_RELEASE_ADDRESS;
    void *allocation = state->allocation;
    uint32_t retiring_token;
    xdj700_nad_owner owner;
    enum xdj700_nad_status status;

    if (!stock_sync_cleanup_retire(control, cleanup_token, &retiring_token)) {
        (void)stock_sync_cleanup_poison(control, cleanup_token);
        return 0;
    }
    stock_zero_bytes(state, (unsigned)sizeof(*state));
    if (release(allocation) != 0) {
        (void)stock_sync_cleanup_poison(control, retiring_token);
        return 0;
    }
    /* The allocation may now be unmapped.  Only the by-value ticket and fresh
     * sessionless control snapshots are legal after this point. */
    if (!stock_lossless_non_audio_capture_owner(
            XDJ700_NAD_OWNER_GATE_READER, retiring_token, 0u, 0, &owner)) {
        (void)stock_sync_cleanup_poison(control, retiring_token);
        return 0;
    }
    if (failure)
        status = xdj700_nad_commit_failure_cleanup(
            stock_lossless_non_audio_island(), stock_lossless_non_audio_result(),
            ticket, 1, &owner);
    else
        status = xdj700_nad_commit_cleanup(
            stock_lossless_non_audio_island(), stock_lossless_non_audio_result(),
            ticket, 1, &owner);
    if (status != XDJ700_NAD_READY &&
        (failure || status != XDJ700_NAD_WAITING)) {
        (void)stock_sync_cleanup_poison(control, retiring_token);
        return 0;
    }
    /* D100's last-reader writer owns final completion and IDLE publication. */
    return 1;
}

static void stock_diagnostic_rollback(stock_lossless_state *state,
                                      uint32_t claim, int bridge_attempted,
                                      int saved_position, int committed)
{
    stock_lossless_sync_control *control = stock_sync_control();
    xdj700_nad_owner owner;
    xdj700_nad_ticket ticket;
    uint32_t cleanup_token = stock_sync_with_phase(claim, STOCK_SYNC_OPEN_CLEANUP);
    int failure = committed || stock_lossless_non_audio_failure_latched(
        claim & STOCK_SYNC_GENERATION_MASK);
    enum xdj700_nad_status status;

    if (!stock_diagnostic_open_owner(state, claim, &owner))
        goto poison;
    if (failure)
        stock_diagnostic_fail(state, &owner);
    if (bridge_attempted) {
        if (state->bridge.operation_active != 0u)
            goto poison;
        lossless_wave_bridge_close(&state->bridge);
        if (state->bridge.operation_active != 0u)
            goto poison;
    }
    if (stock_restore_position(state->file, saved_position, &state->reader) != 0)
        goto poison;
    if (!stock_diagnostic_open_owner(state, claim, &owner))
        goto poison;
    if (!failure) {
        status = xdj700_nad_cancel_before_handoff(
            &state->diagnostic, stock_lossless_non_audio_island(), &owner);
        if (status != XDJ700_NAD_OK)
            goto poison;
    }
    if (!stock_sync_open_cleanup_enter(control, claim))
        goto poison;
    if (failure) {
        if (!stock_diagnostic_owner(state, cleanup_token,
                XDJ700_NAD_OWNER_CLOSE, &owner) ||
            !stock_diagnostic_prepare(state, &owner, &ticket, &failure))
            goto poison_cleanup;
        (void)stock_diagnostic_retire(state, cleanup_token, &ticket, failure);
    } else {
        stock_release_fn release = (stock_release_fn)STOCK_RELEASE_ADDRESS;
        void *allocation = state->allocation;
        uint32_t retiring_token;
        if (!stock_sync_cleanup_retire(control, cleanup_token, &retiring_token))
            goto poison_cleanup;
        stock_zero_bytes(state, (unsigned)sizeof(*state));
        if (release(allocation) != 0 ||
            !stock_sync_cleanup_released(control, retiring_token))
            (void)stock_sync_cleanup_poison(control, retiring_token);
    }
    return;
poison:
    /* Only poison this generation; a foreign owner must remain untouched. */
    cleanup_token = stock_sync_load(&control->lifecycle);
    if ((cleanup_token & STOCK_SYNC_GENERATION_MASK) ==
            (claim & STOCK_SYNC_GENERATION_MASK))
        (void)stock_sync_poison_exact(control, cleanup_token);
    return;
poison_cleanup:
    (void)stock_sync_cleanup_poison(control, cleanup_token);
}

static void stock_diagnostic_observe(stock_lossless_state *state,
                                     uint32_t token, uint64_t offset,
                                     const void *destination, unsigned total,
                                     int bridge_result)
{
    xdj700_nad_owner owner;
    uint64_t first = offset > LOSSLESS_WAVE_BRIDGE_HEADER_BYTES ?
        offset : LOSSLESS_WAVE_BRIDGE_HEADER_BYTES;
    uint64_t end = offset + total;
    const uint64_t pcm_end = LOSSLESS_WAVE_BRIDGE_HEADER_BYTES + XDJ700_NAD_PCM_BYTES;
    enum xdj700_nad_status status;

    if (!stock_diagnostic_owner(state, token, XDJ700_NAD_OWNER_ACTIVE_BUSY, &owner)) {
        (void)stock_sync_poison_exact(stock_sync_control(), token);
        return;
    }
    if (bridge_result != AUDIO_FILE_OK) {
        stock_diagnostic_fail(state, &owner);
        return;
    }
    if (end > pcm_end)
        end = pcm_end;
    if (first >= end)
        return;
    status = xdj700_nad_observe_pcm_window(
        &state->diagnostic, stock_lossless_non_audio_island(), &owner,
        (uint32_t)(first - LOSSLESS_WAVE_BRIDGE_HEADER_BYTES),
        (const uint8_t *)destination + (unsigned)(first - offset),
        (uint32_t)(end - first), state->bridge.decoder_ended);
    if (status != XDJ700_NAD_OK)
        stock_diagnostic_fail(state, &owner);
}
#endif

#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
/* Production exposes an inert compatibility symbol.  The CRC gate reaches
 * the hidden implementation only through stock_lossless_dispatch_trampoline,
 * whose first instructions validate the incoming gate-return PR. */
void stock_lossless_adapter_dispatch(void *manager, void *request)
{
    (void)manager;
    (void)request;
}

void stock_lossless_adapter_dispatch_gate_impl(void *manager, void *request)
    __attribute__((visibility("hidden")));
extern const unsigned char stock_lossless_dispatch_gate_impl_return[]
    __attribute__((visibility("hidden")));

void stock_lossless_adapter_dispatch_gate_impl(void *manager, void *request)
#else
void stock_lossless_adapter_dispatch(void *manager, void *request)
#endif
{
#if STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN
    /* The shared header also serves native-completion builds.  Referencing the
     * deliberately unused activation helper keeps strict per-unit warnings
     * useful without weakening them for this compile-time branch. */
    (void)stock_sync_activate;
#endif
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
    /* The outer trampoline has already authenticated the gate's incoming PR.
     * Authenticate its one exact JSRed continuation as well, so even a direct
     * call to this hidden worker cannot adopt READY. */
    if (STOCK_LOSSLESS_ADAPTER_CURRENT_RETURN_PR() !=
        (uintptr_t)stock_lossless_dispatch_gate_impl_return)
        return;
#endif
    stock_lossless_sync_control *control = stock_sync_control();
    stock_position_fn position_fn =
        (stock_position_fn)STOCK_POSITION_ADDRESS;
    stock_size_fn size_fn = (stock_size_fn)STOCK_SIZE_ADDRESS;
    stock_allocate_fn allocate = (stock_allocate_fn)STOCK_ALLOCATE_ADDRESS;
    stock_release_fn release = (stock_release_fn)STOCK_RELEASE_ADDRESS;
    uint32_t *descriptor = 0;
    uint32_t *source = 0;
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    stock_lossless_reader probe_reader = {0};
#else
    stock_lossless_reader probe_reader;
#endif
    retained_format_router_source router_source;
    retained_format_route route;
    uint32_t selector;
    uint32_t claim = 0u;
    uint32_t cleanup_token;
    uint32_t retiring_token;
    uint32_t slot_guard;
    void *file;
    unsigned source_size = 0u;
    int saved_position = 0;
    int have_saved_position = 0;
    int wanted = 0;
    int terminal = 0;
    int bridge_attempted = 0;
    unsigned request_bytes;
    void *allocation = 0;
    uintptr_t allocation_address;
    uintptr_t state_address;
    uintptr_t arena_address;
    uintptr_t allocation_end;
    uintptr_t fifo_end;
#if STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN
    uintptr_t scratch_address;
    uint32_t drain_hash = XDJ700_NAD_PCM_FNV_OFFSET;
    unsigned drain_offset = 0u;
    unsigned drain_reads = 0u;
#endif
    stock_lossless_state *state = 0;
    dual_format_payload_source bridge_source;
    dual_format_payload_storage storage;
    int result;
    int gate_adopted = 0;
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    int diagnostic_begun = 0;
    int diagnostic_committed = 0;
    xdj700_nad_owner diagnostic_owner;
#endif

    /* Claim first.  In production the integrity gate has completed CRC and
     * advanced CHECKING to READY; this operation atomically adopts that same
     * generation as CLAIMED before any request validation or payload callback
     * can open a handoff window.  Host-only builds retain a separately
     * compiled direct IDLE-to-CLAIMED path. */
    if (!stock_sync_try_dispatch_claim_mode(
            control, &claim, &gate_adopted))
        return;
    if (!stock_sync_gate_dispatch_barrier(
            control, claim, gate_adopted)) {
        if (gate_adopted)
            (void)stock_sync_poison_exact(control, claim);
        else
            (void)stock_sync_dispatch_claim_abort(control, claim);
        return;
    }
    if ((uintptr_t)manager != STOCK_MANAGER_ADDRESS || request == 0 ||
        ((uintptr_t)request & 3u) != 0u)
        goto cleanup;
#if UINTPTR_MAX > UINT32_MAX
    /* Host harnesses retain the target's four-byte request alignment while
     * their pointers are eight bytes wide.  Copy that host pointer without
     * imposing alignment that the target ABI does not require. */
    __builtin_memcpy(&source, (unsigned char *)request + 4u, sizeof(source));
#else
    source = *(uint32_t **)((unsigned char *)request + 4u);
#endif
    if (source == 0 || ((uintptr_t)source & 3u) != 0u)
        goto cleanup;
    selector = source[STOCK_SELECTOR_OFFSET / 4u];
    if (selector != STOCK_SELECTOR_FLAC_CANDIDATE &&
        !stock_alac_candidate_selector(selector))
        goto cleanup;

    descriptor = (uint32_t *)((unsigned char *)manager +
                              STOCK_DESCRIPTOR_OFFSET);
    file = (void *)(uintptr_t)descriptor[1];
    if (!stock_descriptor_matches(descriptor, file) ||
        (uintptr_t)file > UINT32_MAX ||
        (uintptr_t)descriptor > UINT32_MAX) {
        goto cleanup;
    }
    slot_guard = STOCK_DESCRIPTOR_SLOT_GUARD;
    if (!stock_sync_publish_probing(
            control, claim, (uint32_t)(uintptr_t)file,
            (uint32_t)(uintptr_t)descriptor, slot_guard)) {
        stock_set_terminal_selector(source);
        return;
    }

    stock_copy_descriptor(&probe_reader, descriptor);
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    probe_reader.native_fault_expected_token =
        stock_sync_with_phase(claim, STOCK_SYNC_PROBING);
    if (probe_reader.descriptor[0] != 0u ||
        probe_reader.descriptor[1] != (uint32_t)(uintptr_t)file ||
        probe_reader.descriptor[STOCK_DESCRIPTOR_SLOT_WORD] != slot_guard) {
        /* The record is still empty, so compare the copied descriptor to the
         * dispatch's by-value file before initializing it. */
        alac_nfr_discard(&probe_reader.native_fault);
    } else {
        alac_nfr_snapshot before = stock_nfr_snapshot();
        alac_nfr_snapshot after = stock_nfr_snapshot();

        if (before.lifecycle != probe_reader.native_fault_expected_token ||
            after.lifecycle != probe_reader.native_fault_expected_token)
            alac_nfr_discard(&probe_reader.native_fault);
        else
            (void)alac_nfr_begin(&probe_reader.native_fault, before, after);
    }
#endif
    saved_position = position_fn(file, probe_reader.descriptor + 2u);
    if (saved_position < 0)
        goto cleanup;
    have_saved_position = 1;
    if (size_fn(file, &source_size, probe_reader.descriptor + 2u) != 0 ||
        source_size < 12u)
        goto cleanup;

    router_source.read_at = stock_reader_read_at;
    router_source.user = &probe_reader;
    router_source.size = source_size;
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE || \
    STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
    /* Both diagnostic modes inspect ALAC before deciding whether to delegate.
     * Limit that work even when the ALAC bridge is disabled.  A small file can
     * still declare a huge fixed-size sample count, so size alone is not a
     * processing bound.  The exact public fixture uses 89 preflight work
     * units; 512 gives more than five times that measured cost. */
    route = retained_format_router_classify_detailed_bounded(
        &router_source, XDJ700_NAF_ALAC_BYTES, 512u);
#else
    route = retained_format_router_classify_detailed_bounded(
        &router_source, 0u, STOCK_GENERAL_ALAC_MAX_WORK);
#endif
    if (selector == STOCK_SELECTOR_FLAC_CANDIDATE &&
        route == RETAINED_FORMAT_ROUTE_BRIDGE_FLAC)
        wanted = 1;
    else if (STOCK_LOSSLESS_ENABLE_ALAC &&
             stock_alac_candidate_selector(selector) &&
             route == RETAINED_FORMAT_ROUTE_BRIDGE_ALAC)
        wanted = 1;
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    if (STOCK_LOSSLESS_ENABLE_ALAC &&
        stock_alac_candidate_selector(selector) &&
        route == RETAINED_FORMAT_ROUTE_BRIDGE_ALAC)
        stock_nfr_recognize(&probe_reader, ALAC_NFR_ADMITTED);
    else if (STOCK_LOSSLESS_ENABLE_ALAC &&
             stock_alac_candidate_selector(selector) &&
             route == RETAINED_FORMAT_ROUTE_REJECT_ALAC)
        stock_nfr_recognize(&probe_reader, ALAC_NFR_TERMINAL_REJECTED);
    else
        alac_nfr_discard(&probe_reader.native_fault);
#endif

    if (STOCK_LOSSLESS_ENABLE_ALAC &&
        stock_alac_candidate_selector(selector) &&
        route == RETAINED_FORMAT_ROUTE_REJECT_ALAC) {
        /* This source was positively recognized as ALAC at the public sample
         * entry boundary.  Do not let an unsupported or failed preflight fall
         * through to the stock MP4A/AAC decoder.  Restore the borrowed source
         * position for deterministic cleanup even though the route is
         * terminal either way. */
        stock_set_terminal_selector(source);
        terminal = 1;
        goto cleanup;
    }

    if (!wanted) {
        goto cleanup;
    }
    stock_reset_source_parser_outputs(source);
#if STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
    if ((STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE == XDJ700_NAF_MODE_FLAC &&
         (route != RETAINED_FORMAT_ROUTE_BRIDGE_FLAC ||
          source_size != XDJ700_NAF_FLAC_BYTES)) ||
        (STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE == XDJ700_NAF_MODE_ALAC &&
         (route != RETAINED_FORMAT_ROUTE_BRIDGE_ALAC ||
          source_size != XDJ700_NAF_ALAC_BYTES))) {
        if (route == RETAINED_FORMAT_ROUTE_BRIDGE_ALAC)
            terminal = 1;
        goto cleanup;
    }
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    /* Diagnostic progress is evidence for one boot-local attempt.  A prior
     * pre-handoff rollback may leave 1.11 with a clean island, and successful
     * retirement leaves 1.14 with that same clean island.  Neither permits a
     * new allocation/session: rejecting only in begin_eligible would already
     * have published OPENING and stranded another allocation as POISONED.
     * Stock routes have delegated above; a recognized custom source instead
     * retires this probing generation normally with the terminal selector.
     * The owned claim excludes an active diagnostic writer, and the exact
     * atomic read also rejects an unknown/corrupted result without clearing
     * its marker or diagnostic island. */
    if (stock_sync_load(stock_lossless_non_audio_result()) !=
            XDJ700_NAD_RESULT_1_10) {
        terminal = 1;
        goto cleanup;
    }
    if ((STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE == 1 &&
         (route != RETAINED_FORMAT_ROUTE_BRIDGE_FLAC ||
          source_size != XDJ700_NAF_FLAC_BYTES)) ||
        (STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE == 2 &&
         (route != RETAINED_FORMAT_ROUTE_BRIDGE_ALAC ||
          source_size != XDJ700_NAF_ALAC_BYTES))) {
        /* Recognition is already complete. An ALAC stream outside this
         * diagnostic's exact-fixture contract is not a stock AAC input.
         * True AAC was delegated above, before allocation or this gate. */
        if (route == RETAINED_FORMAT_ROUTE_BRIDGE_ALAC)
            terminal = 1;
        goto cleanup;
    }
#endif

    if (sizeof(stock_lossless_state) > UINT32_MAX -
            (STOCK_STATE_ALIGNMENT - 1u) -
            (STOCK_SHARED_ALIGNMENT - 1u) - STOCK_SHARED_BYTES -
            STOCK_FIFO_BYTES - STOCK_DRAIN_EXTRA_BYTES) {
        terminal = 1;
        goto cleanup;
    }
    request_bytes = (unsigned)sizeof(stock_lossless_state) +
        (STOCK_STATE_ALIGNMENT - 1u) + (STOCK_SHARED_ALIGNMENT - 1u) +
        STOCK_SHARED_BYTES + STOCK_FIFO_BYTES + STOCK_DRAIN_EXTRA_BYTES;
    /* Native 08B4C30C rounds up by three, then adds a four-byte header.
     * Our fixed request is small; preserve that native arithmetic contract
     * explicitly if the state/arena constants grow in a future build. */
    _Static_assert((uint64_t)sizeof(stock_lossless_state) +
        (STOCK_STATE_ALIGNMENT - 1u) + (STOCK_SHARED_ALIGNMENT - 1u) +
        STOCK_SHARED_BYTES + STOCK_FIFO_BYTES + STOCK_DRAIN_EXTRA_BYTES <=
            UINT32_MAX - 7u,
        "fixed lossless allocation must fit native rounding and header");
    allocation = allocate(request_bytes);
    if (allocation == 0) {
        terminal = 1;
        goto cleanup;
    }

    allocation_address = (uintptr_t)allocation;
    /* Check the complete native allocation before the first state write.
     * The later published-state check protects readers, but cannot undo an
     * earlier memset through a damaged allocator return.  P1/P2 aliases are
     * not accepted by this adapter's existing P0 state-pointer contract.
     * A nonnull result outside that contract has no established release
     * ownership: restore the borrowed position under cleanup ownership and
     * poison this generation without dereferencing or releasing the result. */
    if ((allocation_address & 3u) != 0u ||
        allocation_address < STOCK_STATE_ADDRESS_MIN ||
        allocation_address >= STOCK_STATE_ADDRESS_MAX_EXCLUSIVE ||
        request_bytes > STOCK_STATE_ADDRESS_MAX_EXCLUSIVE - allocation_address) {
        stock_set_terminal_selector(source);
        if (stock_sync_open_cleanup_enter(control, claim)) {
            (void)stock_restore_position(file, saved_position, &probe_reader);
            (void)stock_sync_cleanup_poison(control,
                stock_sync_with_phase(claim, STOCK_SYNC_OPEN_CLEANUP));
        }
        return;
    }
    if (allocation_address > UINTPTR_MAX - request_bytes) {
        terminal = 1;
        goto cleanup;
    }
    allocation_end = allocation_address + request_bytes;
    state_address = (allocation_address + STOCK_STATE_ALIGNMENT - 1u) &
                    ~(uintptr_t)(STOCK_STATE_ALIGNMENT - 1u);
    if (state_address < allocation_address ||
        state_address > allocation_end ||
        sizeof(stock_lossless_state) > allocation_end - state_address) {
        terminal = 1;
        goto cleanup;
    }
    state = (stock_lossless_state *)state_address;
    stock_zero_bytes(state, (unsigned)sizeof(*state));
    arena_address = (state_address + sizeof(*state) +
                     STOCK_SHARED_ALIGNMENT - 1u) &
                    ~(uintptr_t)(STOCK_SHARED_ALIGNMENT - 1u);
    if (arena_address < state_address ||
        arena_address > allocation_end ||
        STOCK_SHARED_BYTES > allocation_end - arena_address) {
        terminal = 1;
        goto cleanup;
    }
    fifo_end = arena_address + STOCK_SHARED_BYTES;
    if (fifo_end > allocation_end ||
        STOCK_FIFO_BYTES + STOCK_DRAIN_EXTRA_BYTES > allocation_end - fifo_end) {
        terminal = 1;
        goto cleanup;
    }
#if STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN
    scratch_address = fifo_end + STOCK_FIFO_BYTES;
#endif

    state->allocation = allocation;
    state->allocation_bytes = request_bytes;
    state->manager = manager;
    state->descriptor = descriptor;
    state->file = file;
    state->source = source;
    state->source_selector = selector;
    state->cursor = 0u;
    state->reader = probe_reader;
    state->magic = STOCK_ADAPTER_MAGIC;

#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE || \
    STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
    {
        uintptr_t fixture_start = (uintptr_t)_binary_diagnostic_fixture_start;
        uintptr_t fixture_end = (uintptr_t)_binary_diagnostic_fixture_end;
        enum xdj700_naf_status match;
        if (fixture_end < fixture_start || fixture_end - fixture_start > UINT32_MAX) {
            terminal = 1;
            goto cleanup;
        }
        match = xdj700_naf_match_exact_fixture(
            (enum xdj700_naf_mode)(STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE ?
                STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE :
                STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE),
            stock_reader_read_at, &state->reader, source_size,
            _binary_diagnostic_fixture_start, (uint32_t)(fixture_end - fixture_start));
        if (stock_restore_position(file, saved_position, &state->reader) != 0 ||
            match == XDJ700_NAF_READ_FAILED) {
            terminal = 1;
            goto cleanup;
        }
        if (match != XDJ700_NAF_MATCH) {
            if (route == RETAINED_FORMAT_ROUTE_BRIDGE_ALAC)
                terminal = 1;
            goto cleanup;
        }
#if STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
        if (!stock_trial_checkpoint(
                XDJ700_NAD_RESULT_1_10, XDJ700_NAD_RESULT_1_11)) {
            terminal = 1;
            goto cleanup;
        }
#endif
    }
#endif

    result = dual_format_payload_bind_shared_storage(
        &storage, (void *)arena_address, STOCK_SHARED_BYTES,
        STOCK_MAX_FRAMES);
    if (result != AUDIO_FILE_OK) {
        terminal = 1;
        goto cleanup;
    }

    bridge_source.read_at = stock_reader_read_at;
    bridge_source.read_user = &state->reader;
    bridge_source.size = source_size;
    if (state_address > UINT32_MAX ||
        stock_state_from_word((uint32_t)state_address) != state ||
        !stock_descriptor_matches(descriptor, file) ||
        !stock_sync_publish_opening(
            control, claim, (uint32_t)state_address)) {
        terminal = 1;
        goto cleanup;
    }
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    state->reader.native_fault_expected_token =
        stock_sync_with_phase(claim, STOCK_SYNC_OPENING);
    {
        alac_nfr_snapshot before = stock_nfr_snapshot();
        alac_nfr_snapshot after = stock_nfr_snapshot();

        if (before.lifecycle != state->reader.native_fault_expected_token ||
            after.lifecycle != state->reader.native_fault_expected_token ||
            !stock_nfr_reader_copy_matches(&state->reader))
            alac_nfr_discard(&state->reader.native_fault);
        else
            (void)alac_nfr_bind_opening(&state->reader.native_fault,
                                        before, after);
    }
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    if (allocation_address > UINT32_MAX || allocation_end > UINT32_MAX ||
        !stock_diagnostic_owner(state,
            stock_sync_with_phase(claim, STOCK_SYNC_OPENING),
            XDJ700_NAD_OWNER_OPENING, &diagnostic_owner) ||
        xdj700_nad_begin_eligible(
            &state->diagnostic, stock_lossless_non_audio_island(),
            stock_lossless_non_audio_result(), claim & STOCK_SYNC_GENERATION_MASK,
            (uint32_t)state_address, (uint32_t)(uintptr_t)file,
            (uint32_t)(uintptr_t)descriptor, slot_guard,
            (uint32_t)allocation_address, (uint32_t)allocation_end,
            XDJ700_NAD_DDR_MIN, XDJ700_NAD_DDR_MAX_EXCLUSIVE,
            &diagnostic_owner) != XDJ700_NAD_OK) {
        (void)stock_sync_poison_exact(control,
            stock_sync_with_phase(claim, STOCK_SYNC_OPENING));
        stock_set_terminal_selector(source);
        return;
    }
    diagnostic_begun = 1;
#endif
    bridge_attempted = 1;
    result = lossless_wave_bridge_open(
        &state->bridge, &bridge_source, &storage,
        (unsigned char *)fifo_end, STOCK_FIFO_BYTES, 0);
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    stock_nfr_emit_fresh(&state->reader,
        stock_sync_with_phase(claim, STOCK_SYNC_OPENING));
#endif
    if (result != AUDIO_FILE_OK ||
        lossless_wave_bridge_size(&state->bridge) > STOCK_MAX_WAVE_BYTES) {
        terminal = 1;
        goto cleanup;
    }
#if STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
    if (!stock_trial_checkpoint(
            XDJ700_NAD_RESULT_1_11, XDJ700_NAD_RESULT_1_12)) {
        terminal = 1;
        goto cleanup;
    }
#endif

    if (!stock_descriptor_matches(descriptor, file)) {
        terminal = 1;
        goto cleanup;
    }

#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
#if STOCK_LOSSLESS_NON_AUDIO_OPEN_ONLY
    {
        uint32_t expected = XDJ700_NAD_RESULT_1_11;

        /* Selector 6 is eligible only after exact fixture admission and a
         * bounded public-container route.  Stop after the real bridge open:
         * no PCM read, ACTIVE state, native WAVE selector, or completion. */
        if (lossless_wave_bridge_size(&state->bridge) <=
                LOSSLESS_WAVE_BRIDGE_HEADER_BYTES ||
            !stock_sync_cas(stock_lossless_non_audio_result(), &expected,
                            XDJ700_NAD_RESULT_1_12)) {
            terminal = 1;
            goto cleanup;
        }
        expected = XDJ700_NAD_RESULT_1_12;
        if (!stock_sync_cas(stock_lossless_non_audio_result(), &expected,
                            XDJ700_NAD_RESULT_1_13)) {
            terminal = 1;
            goto cleanup;
        }
        stock_set_terminal_selector(source);
        stock_diagnostic_rollback(state, claim, 1, saved_position, 0);
        if (stock_sync_load(&control->lifecycle) ==
                (claim & STOCK_SYNC_GENERATION_MASK) &&
            stock_sync_idle_bindings_zero(control) &&
            stock_sync_load(&stock_lossless_non_audio_island()->armed_generation) == 0u &&
            stock_sync_load(&stock_lossless_non_audio_island()->cleanup_generation) == 0u &&
            stock_sync_load(&stock_lossless_non_audio_island()->final_send_generation) == 0u &&
            stock_sync_load(&stock_lossless_non_audio_island()->failed_generation) == 0u) {
            expected = XDJ700_NAD_RESULT_1_13;
            (void)stock_sync_cas(stock_lossless_non_audio_result(), &expected,
                                 XDJ700_NAD_RESULT_1_14);
        }
        return;
    }
#elif STOCK_LOSSLESS_NON_AUDIO_DECODER_DRAIN
    {
        uint32_t expected = XDJ700_NAD_RESULT_1_11;

        /* This branch deliberately never publishes selector 11.  It executes
         * the decoder from the already proven OPENING owner, using scratch in
         * the same checked native allocation, and bounds both call count and
         * total PCM.  The existing exact-fixture comparison is the admission
         * identity; this byte oracle independently checks decoded content. */
        if (lossless_wave_bridge_size(&state->bridge) !=
                LOSSLESS_WAVE_BRIDGE_HEADER_BYTES + XDJ700_NAD_PCM_BYTES ||
            !stock_sync_cas(stock_lossless_non_audio_result(), &expected,
                            XDJ700_NAD_RESULT_1_12)) {
            terminal = 1;
            goto cleanup;
        }
        while (drain_offset < XDJ700_NAD_PCM_BYTES &&
               drain_reads < STOCK_DRAIN_MAX_READS) {
            unsigned requested = XDJ700_NAD_PCM_BYTES - drain_offset;
            unsigned got = 0u;
            unsigned index;

            if (requested > STOCK_DRAIN_SCRATCH_BYTES)
                requested = STOCK_DRAIN_SCRATCH_BYTES;
            result = lossless_wave_bridge_read_at(
                &state->bridge,
                LOSSLESS_WAVE_BRIDGE_HEADER_BYTES + drain_offset,
                (void *)scratch_address, requested, &got);
            ++drain_reads;
            if (result != AUDIO_FILE_OK || got == 0u || got > requested) {
                terminal = 1;
                goto cleanup;
            }
            for (index = 0u; index < got; ++index) {
                uint8_t byte = ((const uint8_t *)scratch_address)[index];

                if (byte != xdj700_nad_oracle_byte(drain_offset + index)) {
                    terminal = 1;
                    goto cleanup;
                }
                drain_hash ^= (uint32_t)byte;
                drain_hash *= XDJ700_NAD_PCM_FNV_PRIME;
            }
            drain_offset += got;
        }
        if (drain_offset != XDJ700_NAD_PCM_BYTES ||
            drain_reads == 0u || drain_reads > STOCK_DRAIN_MAX_READS ||
            drain_hash != XDJ700_NAD_PCM_FNV_EXPECTED ||
            state->bridge.decoder_ended == 0u) {
            terminal = 1;
            goto cleanup;
        }
        expected = XDJ700_NAD_RESULT_1_12;
        if (!stock_sync_cas(stock_lossless_non_audio_result(), &expected,
                            XDJ700_NAD_RESULT_1_13)) {
            terminal = 1;
            goto cleanup;
        }

        /* Roll back the never-committed handoff through the existing checked
         * close/position-restore/release path.  Only an exact clean IDLE after
         * that function returns may publish the final drain checkpoint. */
        stock_set_terminal_selector(source);
        stock_diagnostic_rollback(state, claim, 1, saved_position, 0);
        if (stock_sync_load(&control->lifecycle) ==
                (claim & STOCK_SYNC_GENERATION_MASK) &&
            stock_sync_idle_bindings_zero(control) &&
            stock_sync_load(&stock_lossless_non_audio_island()->armed_generation) == 0u &&
            stock_sync_load(&stock_lossless_non_audio_island()->cleanup_generation) == 0u &&
            stock_sync_load(&stock_lossless_non_audio_island()->final_send_generation) == 0u &&
            stock_sync_load(&stock_lossless_non_audio_island()->failed_generation) == 0u) {
            expected = XDJ700_NAD_RESULT_1_13;
            (void)stock_sync_cas(stock_lossless_non_audio_result(), &expected,
                                 XDJ700_NAD_RESULT_1_14);
        }
        return;
    }
#else
    if (lossless_wave_bridge_size(&state->bridge) !=
            LOSSLESS_WAVE_BRIDGE_HEADER_BYTES + XDJ700_NAD_PCM_BYTES ||
        !stock_diagnostic_owner(state,
            stock_sync_with_phase(claim, STOCK_SYNC_OPENING),
            XDJ700_NAD_OWNER_OPENING, &diagnostic_owner) ||
        xdj700_nad_arm(&state->diagnostic, stock_lossless_non_audio_island(),
            stock_lossless_non_audio_result(), &diagnostic_owner) != XDJ700_NAD_OK ||
        !stock_diagnostic_owner(state,
            stock_sync_with_phase(claim, STOCK_SYNC_OPENING),
            XDJ700_NAD_OWNER_OPENING, &diagnostic_owner) ||
        xdj700_nad_commit_handoff(&state->diagnostic,
            stock_lossless_non_audio_island(), stock_lossless_non_audio_result(),
            &diagnostic_owner) != XDJ700_NAD_OK) {
        terminal = 1;
        goto cleanup;
    }
    diagnostic_committed = 1;
    if (!stock_sync_activate(control, claim)) {
        terminal = 1;
        goto cleanup;
    }
    /* The outer dispatch reader excludes destructive close until this store. */
    STOCK_LOSSLESS_ADAPTER_AFTER_ACTIVATE_TEST_HOOK(control, source,
        stock_sync_with_phase(claim, STOCK_SYNC_ACTIVE));
    source[STOCK_SELECTOR_OFFSET / 4u] = STOCK_SELECTOR_WAVE;
#endif
#else
#if STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
    if (!stock_trial_checkpoint(
            XDJ700_NAD_RESULT_1_12, XDJ700_NAD_RESULT_1_13)) {
        terminal = 1;
        goto cleanup;
    }
    if (!stock_sync_activate(control, claim)) {
        terminal = 1;
        goto cleanup;
    }
    /* The custom wrappers own a complete bridge before the selector becomes
     * visible to the stock WAVE producer. */
    source[STOCK_SELECTOR_OFFSET / 4u] = STOCK_SELECTOR_WAVE;
#else
    /* Publish the selector before ACTIVE.  A close either observes OPENING and
     * poisons this generation, or observes the fully initialized ACTIVE state;
     * it can never free state before this final generation-bound CAS. */
    source[STOCK_SELECTOR_OFFSET / 4u] = STOCK_SELECTOR_WAVE;
    if (!stock_sync_activate(control, claim)) {
        terminal = 1;
        goto cleanup;
    }
#endif
#endif
    return;

cleanup:
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    if (diagnostic_begun) {
        stock_set_terminal_selector(source);
        stock_diagnostic_rollback(state, claim, bridge_attempted,
            saved_position, diagnostic_committed);
        return;
    }
#endif
    cleanup_token = stock_sync_with_phase(claim, STOCK_SYNC_OPEN_CLEANUP);
    if (!stock_sync_open_cleanup_enter(control, claim)) {
        /* Cancellation is an accepted input to cleanup_enter.  Failure here
         * therefore means this generation was corrupted or already poisoned.
         * Retain any allocation and decoder state until reboot rather than
         * risk cleanup without an exclusive lifecycle owner. */
        stock_set_terminal_selector(source);
        return;
    }
    if (bridge_attempted && state != 0 &&
        state->bridge.operation_active != 0u) {
        (void)stock_sync_cleanup_poison(control, cleanup_token);
        stock_set_terminal_selector(source);
        return;
    }
    if (bridge_attempted && state != 0)
        lossless_wave_bridge_close(&state->bridge);
    if (bridge_attempted && state != 0 &&
        state->bridge.operation_active != 0u) {
        (void)stock_sync_cleanup_poison(control, cleanup_token);
        stock_set_terminal_selector(source);
        return;
    }
    if (have_saved_position &&
        stock_restore_position(file, saved_position, &probe_reader) != 0)
        terminal = 1;
    if (terminal)
        stock_set_terminal_selector(source);
    if (!stock_sync_cleanup_retire(
            control, cleanup_token, &retiring_token)) {
        (void)stock_sync_cleanup_poison(control, cleanup_token);
        stock_set_terminal_selector(source);
        return;
    }
    if (state != 0) {
        stock_sync_store(&control->dynamic_state, 0u);
        stock_zero_bytes(state, (unsigned)sizeof(*state));
    }
    if (allocation != 0 && release(allocation) != 0) {
        (void)stock_sync_cleanup_poison(control, retiring_token);
        return;
    }
    if (!stock_sync_cleanup_released(control, retiring_token))
        (void)stock_sync_cleanup_poison(control, retiring_token);
}

static int stock_bridge_fill(stock_lossless_state *state, uint64_t offset,
                             void *destination, unsigned count,
                             unsigned *total_out)
{
    unsigned total = 0u;

    if (total_out == 0 || (count != 0u && destination == 0))
        return AUDIO_FILE_ERR_ARGUMENT;
    *total_out = 0u;
    while (total < count) {
        unsigned piece = 0u;
        int result = lossless_wave_bridge_read_at(
            &state->bridge, offset + total,
            (unsigned char *)destination + total, count - total, &piece);

        if (result != AUDIO_FILE_OK) {
            *total_out = total;
            return result;
        }
        if (piece == 0u)
            break;
        if (piece > count - total) {
            *total_out = total;
            return AUDIO_FILE_ERR_OUTPUT;
        }
        total += piece;
    }
    *total_out = total;
    return AUDIO_FILE_OK;
}

static int stock_sh4_direct_29bit_address(uintptr_t address)
{
    /* In compatible 29-bit mode P1/P2 name the same physical low 29 bits.
     * A low P0 address does too only when its MMU translation is disabled or
     * identity-mapped.  Comparing these spellings can conservatively reject
     * an identity alias; it does not discover arbitrary active TLB mappings.
     * Host pointers outside 32 bits retain their ordinary virtual checks. */
    return address <= UINT32_MAX &&
        (address < UINT32_C(0x20000000) ||
         (address >= UINT32_C(0x80000000) &&
          address < UINT32_C(0xC0000000)));
}

static int stock_output_overlaps_range(uintptr_t start, uintptr_t count,
                                       uintptr_t owner, uintptr_t extent)
{
    uintptr_t start_physical;
    uintptr_t owner_physical;

    if (count > UINTPTR_MAX - start || extent == 0u ||
        extent > UINTPTR_MAX - owner)
        return 1;
    if (start < owner + extent && start + count > owner)
        return 1;
    if (!stock_sh4_direct_29bit_address(start) ||
        !stock_sh4_direct_29bit_address(owner))
        return 0;
    start_physical = start & UINT32_C(0x1FFFFFFF);
    owner_physical = owner & UINT32_C(0x1FFFFFFF);
    /* A span crossing the direct 29-bit aperture is not a usable output. */
    if (count > UINT32_C(0x20000000) - start_physical ||
        extent > UINT32_C(0x20000000) - owner_physical)
        return 1;
    return start_physical < owner_physical + extent &&
           start_physical + count > owner_physical;
}

static int stock_output_conflicts_owner(const stock_lossless_state *state,
                                        const void *output, unsigned count)
{
    uintptr_t start;

    if (count == 0u)
        return 0;
    /* Preserve the bridge's existing NULL-output error and terminal latch. */
    if (output == 0)
        return 0;
    start = (uintptr_t)output;
    /* A wrapping output span is invalid even if its first bytes are outside
     * the private allocation.  Decoder storage is not the only live owner:
     * the control block and borrowed native binding records are also read
     * again before this operation can leave ACTIVE_BUSY or close safely. */
    if ((uintptr_t)count > UINTPTR_MAX - start)
        return 1;
    return stock_output_overlaps_range(start, count,
               (uintptr_t)state->allocation, state->allocation_bytes) ||
           stock_output_overlaps_range(start, count,
               (uintptr_t)stock_sync_control(),
               sizeof(stock_lossless_sync_control)) ||
           stock_output_overlaps_range(start, count,
               (uintptr_t)state->descriptor,
               STOCK_DESCRIPTOR_WORDS * sizeof(uint32_t)) ||
           stock_output_overlaps_range(start, count,
               (uintptr_t)state->source, 8u * sizeof(uint32_t)) ||
           stock_output_overlaps_range(start, count,
               (uintptr_t)state->file, STOCK_NATIVE_FILE_LIVE_MIN_BYTES);
}

static unsigned stock_bridge_output_count(const stock_lossless_state *state,
                                          uint64_t offset, unsigned count)
{
    uint64_t size = lossless_wave_bridge_size(&state->bridge);
    uint64_t available;

    if (offset >= size)
        return 0u;
    available = size - offset;
    return available < count ? (unsigned)available : count;
}

static int stock_read_outputs_overlap(const void *destination, unsigned count,
                                      const unsigned *bytes_read)
{
    uintptr_t data = (uintptr_t)destination;
    uintptr_t result = (uintptr_t)bytes_read;

    if (destination == 0 || count == 0u)
        return 0;
    if ((uintptr_t)count > UINTPTR_MAX - data ||
        sizeof(*bytes_read) > UINTPTR_MAX - result)
        return 1;
    return stock_output_overlaps_range(data, count, result,
                                       sizeof(*bytes_read));
}

int stock_lossless_adapter_read(void *descriptor, void *destination,
                                unsigned offset, unsigned count,
                                unsigned *bytes_read)
{
    stock_lossless_state *state = 0;
    uint32_t busy_token = 0u;
    enum stock_sync_route route = stock_operation_begin(
        (uintptr_t)descriptor, STOCK_SYNC_IDENTITY_DESCRIPTOR,
        &state, &busy_token);
    unsigned total = 0u;
    int result = 1;

    if (route == STOCK_SYNC_ROUTE_DELEGATE) {
        if (!stock_sync_gate_reader_enter(
                stock_sync_control(), busy_token)) {
            /* A failed reader admission gives us no private owner identity.
             * Do not let an arbitrary result pointer overwrite an active
             * control, source, descriptor, or decoder allocation. */
            return 1;
        }
        result = stock_lossless_read_original(
            descriptor, destination, offset, count, bytes_read);
        (void)stock_sync_gate_reader_leave();
        return result;
    }
    if (route != STOCK_SYNC_ROUTE_OWNED) {
        /* Rejected/poisoned routes likewise have no authenticated state to
         * use for an alias check.  Leave the caller output untouched. */
        return 1;
    }
    if (bytes_read == 0 || stock_output_conflicts_owner(
            state, bytes_read, (unsigned)sizeof(*bytes_read)) ||
        stock_read_outputs_overlap(destination,
            stock_bridge_output_count(state, offset, count), bytes_read))
        goto done;
    *bytes_read = 0u;
    if (stock_output_conflicts_owner(state, destination,
            stock_bridge_output_count(state, offset, count)))
        goto done;
    /* Invalid caller arguments are not decoder failures.  A native parser
     * can probe beyond virtual EOF; forwarding that RANGE error to the
     * bridge would latch terminal_error and make later EOF queries false. */
    if ((uint64_t)offset > lossless_wave_bridge_size(&state->bridge) ||
        (count != 0u && destination == 0))
        goto done;
    /* read_at owns the guarded decoder restart needed when a native random
     * read falls below the retained block.  This is required after the stock
     * mode-three scan: normal playback can seek back to a nonzero cue record,
     * not only to the first PCM byte. */
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    state->reader.native_fault_expected_token = busy_token;
#endif
    result = stock_bridge_fill(state, offset, destination, count, &total);
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    stock_nfr_emit_fresh(&state->reader, busy_token);
    state->reader.native_fault_expected_token = 0u;
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    stock_diagnostic_observe(state, busy_token, offset, destination, total, result);
#endif
    if (result != AUDIO_FILE_OK) {
        state->terminal_error = 1u;
        if (total == 0u)
            goto done;
    }
    /* A bridge failure after one or more complete pieces is a normal short
     * read.  Publish that prefix; the terminal bridge reports the failure on
     * the next call.  Never return failure while hiding written bytes. */
    *bytes_read = total;
    result = 0;
done:
    (void)stock_sync_operation_leave(stock_sync_control(), busy_token);
    return result;
}

int stock_lossless_adapter_size(void *file, unsigned *size_out, void *aux)
{
    stock_lossless_state *state = 0;
    uint32_t busy_token = 0u;
    enum stock_sync_route route = stock_operation_begin(
        (uintptr_t)file, STOCK_SYNC_IDENTITY_FILE, &state, &busy_token);
    stock_size_fn stock_size = (stock_size_fn)STOCK_SIZE_ADDRESS;
    int result = 1;

    if (route == STOCK_SYNC_ROUTE_DELEGATE) {
        if (!stock_sync_gate_reader_enter(
                stock_sync_control(), busy_token)) {
            return 1;
        }
        result = stock_size(file, size_out, aux);
        (void)stock_sync_gate_reader_leave();
        return result;
    }
    if (route != STOCK_SYNC_ROUTE_OWNED) {
        return 1;
    }
    if (size_out == 0 || stock_output_conflicts_owner(
            state, size_out, (unsigned)sizeof(*size_out)))
        goto done;
    *size_out = (unsigned)lossless_wave_bridge_size(&state->bridge);
    result = *size_out != 0u ? 0 : 1;
done:
    (void)stock_sync_operation_leave(stock_sync_control(), busy_token);
    return result;
}

int stock_lossless_adapter_position(void *file, void *aux)
{
    stock_lossless_state *state = 0;
    uint32_t busy_token = 0u;
    enum stock_sync_route route = stock_operation_begin(
        (uintptr_t)file, STOCK_SYNC_IDENTITY_FILE, &state, &busy_token);
    stock_position_fn stock_position =
        (stock_position_fn)STOCK_POSITION_ADDRESS;
    int result;

    if (route == STOCK_SYNC_ROUTE_DELEGATE) {
        if (!stock_sync_gate_reader_enter(
                stock_sync_control(), busy_token))
            return -1;
        result = stock_position(file, aux);
        (void)stock_sync_gate_reader_leave();
        return result;
    }
    if (route != STOCK_SYNC_ROUTE_OWNED)
        return -1;
    result = state->cursor <= (uint32_t)INT32_MAX ?
        (int)state->cursor : -1;
    (void)stock_sync_operation_leave(stock_sync_control(), busy_token);
    return result;
}

int stock_lossless_adapter_seek(void *file, int offset, int origin, void *aux)
{
    stock_lossless_state *state = 0;
    uint32_t busy_token = 0u;
    enum stock_sync_route route = stock_operation_begin(
        (uintptr_t)file, STOCK_SYNC_IDENTITY_FILE, &state, &busy_token);
    stock_seek_fn stock_seek = (stock_seek_fn)STOCK_SEEK_ADDRESS;
    int64_t base;
    int64_t wanted;
    uint64_t size;
    int result = 1;

    if (route == STOCK_SYNC_ROUTE_DELEGATE) {
        if (!stock_sync_gate_reader_enter(
                stock_sync_control(), busy_token))
            return 1;
        result = stock_seek(file, offset, origin, aux);
        (void)stock_sync_gate_reader_leave();
        return result;
    }
    if (route != STOCK_SYNC_ROUTE_OWNED)
        return 1;
    size = lossless_wave_bridge_size(&state->bridge);
    if (origin == 0)
        base = 0;
    else if (origin == 1)
        base = state->cursor;
    else if (origin == 2)
        base = (int64_t)size;
    else
        goto done;
    wanted = base + (int64_t)offset;
    if (wanted < 0 || (uint64_t)wanted > size || wanted > INT32_MAX)
        goto done;
    /* The bridge reopens lazily on the next read when this position is below
     * its retained replay floor.  Keep seek callback-free while admitting the
     * stock scan-to-playback transition and ordinary nonzero cue positions. */
    state->cursor = (uint32_t)wanted;
    result = 0;
done:
    (void)stock_sync_operation_leave(stock_sync_control(), busy_token);
    return result;
}

int stock_lossless_adapter_eof(void *file, void *aux)
{
    stock_lossless_state *state = 0;
    uint32_t busy_token = 0u;
    enum stock_sync_route route = stock_operation_begin(
        (uintptr_t)file, STOCK_SYNC_IDENTITY_FILE, &state, &busy_token);
    stock_eof_fn stock_eof = (stock_eof_fn)STOCK_EOF_ADDRESS;
    int result;

    if (route == STOCK_SYNC_ROUTE_DELEGATE) {
        if (!stock_sync_gate_reader_enter(
                stock_sync_control(), busy_token))
            return 0;
        result = stock_eof(file, aux);
        (void)stock_sync_gate_reader_leave();
        return result;
    }
    if (route != STOCK_SYNC_ROUTE_OWNED)
        return 0;
    result = state->terminal_error == 0u &&
        (uint64_t)state->cursor >=
            lossless_wave_bridge_size(&state->bridge) ? 1 : 0;
    (void)stock_sync_operation_leave(stock_sync_control(), busy_token);
    return result;
}

int stock_lossless_adapter_transfer(void *destination, int operation,
                                    unsigned count, void *file, void *context)
{
    stock_lossless_state *state = 0;
    uint32_t busy_token = 0u;
    enum stock_sync_route route = stock_operation_begin(
        (uintptr_t)file, STOCK_SYNC_IDENTITY_FILE, &state, &busy_token);
    stock_transfer_fn stock_transfer =
        (stock_transfer_fn)STOCK_TRANSFER_ADDRESS;
    unsigned total = 0u;
    int bridge_result;
    int result = -1;

    if (route == STOCK_SYNC_ROUTE_DELEGATE) {
        if (!stock_sync_gate_reader_enter(
                stock_sync_control(), busy_token))
            return -1;
        result = stock_transfer(
            destination, operation, count, file, context);
        (void)stock_sync_gate_reader_leave();
        return result;
    }
    if (route != STOCK_SYNC_ROUTE_OWNED)
        return -1;
    if (operation != 1)
        goto done;
    if (stock_output_conflicts_owner(state, destination,
            stock_bridge_output_count(state, state->cursor, count)))
        goto done;
    if (count != 0u && destination == 0)
        goto done;
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    state->reader.native_fault_expected_token = busy_token;
#endif
    bridge_result = stock_bridge_fill(state, state->cursor, destination, count,
                                      &total);
#if STOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD
    stock_nfr_emit_fresh(&state->reader, busy_token);
    state->reader.native_fault_expected_token = 0u;
#endif
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    stock_diagnostic_observe(state, busy_token, state->cursor, destination,
        total, bridge_result);
#endif
    if (total > UINT32_MAX - state->cursor)
        goto done;
    state->cursor += total;
    if (bridge_result != AUDIO_FILE_OK) {
        state->terminal_error = 1u;
        if (total == 0u)
            goto done;
    }
    result = total <= (unsigned)INT32_MAX ? (int)total : -1;
done:
    (void)stock_sync_operation_leave(stock_sync_control(), busy_token);
    return result;
}

int stock_lossless_adapter_close(void *file, void *arg1,
                                 void *arg2, void *arg3)
{
    stock_lossless_sync_control *control = stock_sync_control();
    stock_lossless_state *state;
    stock_close_fn stock_close = (stock_close_fn)STOCK_CLOSE_ADDRESS;
    stock_release_fn release = (stock_release_fn)STOCK_RELEASE_ADDRESS;
    enum stock_sync_route route;
    uint32_t closing_token = 0u;
    uint32_t cleanup_token = 0u;
    uint32_t retiring_token = 0u;
    uint32_t state_word;
    void *allocation;
    int result;
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    xdj700_nad_owner diagnostic_owner;
    xdj700_nad_ticket diagnostic_ticket;
    int diagnostic_failure;
#endif

#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
    /* A sole activity reader is not itself caller provenance.  Reject before
     * lifecycle/control/native access unless this call came from the exact
     * post-JSR site in the checksum gate's common wrapper. */
    if (STOCK_LOSSLESS_ADAPTER_CURRENT_RETURN_PR() !=
        (uintptr_t)STOCK_LOSSLESS_GATE_HOOK_RETURN_PR)
        return -1;
#endif
    if ((uintptr_t)file == 0u || (uintptr_t)file > UINT32_MAX)
        return -1;
    route = stock_sync_close_enter(
        control, (uint32_t)(uintptr_t)file, &closing_token);
    if (route == STOCK_SYNC_ROUTE_DELEGATE) {
        if (!stock_sync_gate_reader_enter(control, closing_token))
            return -1;
        result = stock_close(file, arg1, arg2, arg3);
        (void)stock_sync_gate_reader_leave();
        return result;
    }
    if (route == STOCK_SYNC_ROUTE_REJECT ||
        route == STOCK_SYNC_ROUTE_POISONED)
        return -1;
    if (route != STOCK_SYNC_ROUTE_OWNED)
        return -1;

    state_word = stock_sync_load(&control->dynamic_state);
    state = stock_state_from_word(state_word);
    if (!stock_state_valid(state) || state->file != file ||
        !stock_descriptor_matches(
            (const uint32_t *)state->descriptor, state->file)) {
        (void)stock_sync_poison_exact(control, closing_token);
        return -1;
    }
    allocation = state->allocation;
    if (state->bridge.operation_active != 0u) {
        (void)stock_sync_poison_exact(control, closing_token);
        return -1;
    }
    result = stock_close(file, arg1, arg2, arg3);
    if (result < 0) {
        if (!stock_sync_close_failed(control, closing_token))
            return -1;
        return result;
    }

#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    /* Native close can block.  Authenticate fresh bindings before touching
     * the retained pointer again, even when native close reported success. */
    if (!stock_diagnostic_owner(state, closing_token,
            XDJ700_NAD_OWNER_CLOSE, &diagnostic_owner)) {
        (void)stock_sync_poison_exact(control, closing_token);
        return -1;
    }
#else
    /* The native call may block.  Reject a replaced session before touching
     * the retained allocation again. */
    if (stock_sync_load(&control->dynamic_state) != state_word ||
        stock_sync_load(&control->lifecycle) != closing_token) {
        (void)stock_sync_poison_exact(control, closing_token);
        return -1;
    }
#endif
    /* Reauthenticate the live source and descriptor before retaining or
     * rewriting either binding. */
    if (!stock_state_valid(state) || state->file != file ||
        !stock_descriptor_matches(
            (const uint32_t *)state->descriptor, state->file) ||
        state->source[STOCK_SELECTOR_OFFSET / 4u] != STOCK_SELECTOR_WAVE) {
        (void)stock_sync_poison_exact(control, closing_token);
        return -1;
    }
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    if (!stock_diagnostic_prepare(state, &diagnostic_owner,
            &diagnostic_ticket, &diagnostic_failure)) {
        (void)stock_sync_poison_exact(control, closing_token);
        return -1;
    }
#endif
    if (state->bridge.operation_active != 0u ||
        !stock_sync_close_cleanup_enter(
            control, closing_token, &cleanup_token)) {
        (void)stock_sync_poison_exact(control, closing_token);
        return -1;
    }
    lossless_wave_bridge_close(&state->bridge);
    if (state->bridge.operation_active != 0u) {
        (void)stock_sync_cleanup_poison(control, cleanup_token);
        return -1;
    }
#if STOCK_LOSSLESS_NON_AUDIO_DIAGNOSTIC_MODE
    (void)allocation;
    (void)release;
    (void)retiring_token;
    return stock_diagnostic_retire(state, cleanup_token,
        &diagnostic_ticket, diagnostic_failure) ? result : -1;
#else
    if (!stock_sync_cleanup_retire(
            control, cleanup_token, &retiring_token)) {
        (void)stock_sync_cleanup_poison(control, cleanup_token);
        return -1;
    }
    /* Selector 11 is an active-session substitution, not durable file type.
     * Restore it at close so a later load reaches this adapter again.  Keep
     * the other WAVE parser outputs intact until native playback teardown is
     * finished; the next admitted lossless session resets them safely. */
    state->source[STOCK_SELECTOR_OFFSET / 4u] = state->source_selector;
    stock_sync_store(&control->dynamic_state, 0u);
    stock_zero_bytes(state, (unsigned)sizeof(*state));
    if (release(allocation) != 0) {
        (void)stock_sync_cleanup_poison(control, retiring_token);
        return -1;
    }
    if (!stock_sync_cleanup_released(control, retiring_token))
        return -1;
#if STOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE
    if (!stock_trial_checkpoint(
            XDJ700_NAD_RESULT_1_13, XDJ700_NAD_RESULT_1_14))
        return -1;
#endif
    return result;
#endif
}
