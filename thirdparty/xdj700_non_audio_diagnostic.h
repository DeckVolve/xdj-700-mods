#ifndef XDJ700_NON_AUDIO_DIAGNOSTIC_H
#define XDJ700_NON_AUDIO_DIAGNOSTIC_H

/*
 * Clean-room state core for the XDJ-700 quarantined non-audio diagnostic.
 *
 * This module validates a bounded diagnostic event sequence.  It does not
 * acquire the firmware lifecycle owner, acquire the integrity-gate activity
 * reader/writer, decode audio, invoke a native call, or make an image safe to
 * install.  Each mutating entry therefore requires a by-value owner
 * attestation containing two independently captured control snapshots.  The
 * integration must acquire the declared owner before constructing the
 * attestation, keep that owner until the entry returns, and derive
 * actual_session_address from the target pointer rather than copied state.
 *
 * In particular, the GATE_READER and GATE_WRITER kinds are assertions by
 * trusted integration code; this standalone module cannot prove that the
 * target gate owner is actually held.  OPENING, CANCEL_OPENING, ACTIVE_BUSY,
 * CLOSE, and GATE_WRITER owners must be released before a blocking native
 * call.
 * GATE_READER intentionally spans the native call so a writer cannot retire
 * or replace its generation.  ACTIVE_SEND is a dedicated lifecycle phase:
 * integration publishes it before active-completion prepare and keeps that
 * exact phase published through the blocking native send and commit, so close
 * can only retry rather than acquire or free the session.  Prepare/commit
 * pairs straddle native calls with registered shared-state claims and by-value
 * tickets.
 *
 * The active-completion path must acquire exact same-generation ACTIVE_SEND
 * before prepare, independently capture a fresh ACTIVE_SEND attestation for
 * commit after the native call, and return the lifecycle to ACTIVE only after
 * commit.  Published ACTIVE_SEND is the lifecycle exclusion that forbids
 * close from freeing the session; ACTIVE_SEND_INFLIGHT is a duplicate progress
 * invariant in the diagnostic island, not a lock.
 *
 * The feature is inert unless XDJ700_NON_AUDIO_DIAGNOSTIC_ENABLE is exactly 1.
 */

#include <stddef.h>
#include <stdint.h>

#ifndef XDJ700_NON_AUDIO_DIAGNOSTIC_ENABLE
#define XDJ700_NON_AUDIO_DIAGNOSTIC_ENABLE 0
#endif

#if XDJ700_NON_AUDIO_DIAGNOSTIC_ENABLE != 0 && \
    XDJ700_NON_AUDIO_DIAGNOSTIC_ENABLE != 1
#error "XDJ700_NON_AUDIO_DIAGNOSTIC_ENABLE must be 0 or 1"
#endif

/*
 * Host-only pointer modeling; production always checks the actual pointer.
 * A modeled build must provide xdj700_nad_test_model_session_address(), which
 * maps a particular host object (not merely its copied contents) to a target
 * address.  This preserves the pointer-identity check on 64-bit hosts.
 */
#ifndef XDJ700_NAD_MODEL_SESSION_ADDRESSES
#define XDJ700_NAD_MODEL_SESSION_ADDRESSES 0
#endif

#if XDJ700_NAD_MODEL_SESSION_ADDRESSES != 0 && \
    XDJ700_NAD_MODEL_SESSION_ADDRESSES != 1
#error "XDJ700_NAD_MODEL_SESSION_ADDRESSES must be 0 or 1"
#endif

#if XDJ700_NAD_MODEL_SESSION_ADDRESSES && \
    (!defined(XDJ700_NAD_TEST_HOOKS) || !XDJ700_NAD_TEST_HOOKS)
#error "modeled session addresses are restricted to test-hook builds"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define XDJ700_NAD_NODISCARD __attribute__((warn_unused_result))
#else
#define XDJ700_NAD_NODISCARD
#endif

#define XDJ700_NAD_PCM_BYTES              UINT32_C(9408)
#define XDJ700_NAD_PCM_FRAMES             UINT32_C(2352)
#define XDJ700_NAD_PCM_FNV_OFFSET         UINT32_C(0x811c9dc5)
#define XDJ700_NAD_PCM_FNV_EXPECTED       UINT32_C(0x0c4de0a0)
#define XDJ700_NAD_PCM_FNV_PRIME          UINT32_C(16777619)

#define XDJ700_NAD_RESULT_1_10            UINT32_C(0x30312e31)
#define XDJ700_NAD_RESULT_1_11            UINT32_C(0x31312e31)
#define XDJ700_NAD_RESULT_1_12            UINT32_C(0x32312e31)
#define XDJ700_NAD_RESULT_1_13            UINT32_C(0x33312e31)
#define XDJ700_NAD_RESULT_1_14            UINT32_C(0x34312e31)

#define XDJ700_NAD_PHASE_MASK             UINT32_C(0x0000000f)
#define XDJ700_NAD_GENERATION_MASK        UINT32_C(0xfffffff0)

#define XDJ700_NAD_PHASE_IDLE             UINT32_C(0)
#define XDJ700_NAD_PHASE_CLAIMED          UINT32_C(1)
#define XDJ700_NAD_PHASE_PROBING          UINT32_C(2)
#define XDJ700_NAD_PHASE_OPENING          UINT32_C(3)
#define XDJ700_NAD_PHASE_ACTIVE           UINT32_C(4)
#define XDJ700_NAD_PHASE_ACTIVE_BUSY      UINT32_C(5)
#define XDJ700_NAD_PHASE_CLOSING          UINT32_C(6)
#define XDJ700_NAD_PHASE_CLOSE_FAILED     UINT32_C(7)
#define XDJ700_NAD_PHASE_POISONED         UINT32_C(8)
#define XDJ700_NAD_PHASE_GATE_READY       UINT32_C(9)
#define XDJ700_NAD_PHASE_OPEN_CLEANUP     UINT32_C(10)
#define XDJ700_NAD_PHASE_RETIRING         UINT32_C(11)
#define XDJ700_NAD_PHASE_CANCEL_PROBING   UINT32_C(12)
#define XDJ700_NAD_PHASE_CANCEL_OPENING   UINT32_C(13)
#define XDJ700_NAD_PHASE_ACTIVE_SEND      UINT32_C(14)
#define XDJ700_NAD_PHASE_GATE_CHECKING    UINT32_C(15)

/* Low-nibble tags used only in their named island word. */
#define XDJ700_NAD_ACTIVE_SEND_INFLIGHT   UINT32_C(1)
#define XDJ700_NAD_ACTIVE_SEND_DONE       UINT32_C(2)
#define XDJ700_NAD_CLEANUP_INFLIGHT       UINT32_C(3)
#define XDJ700_NAD_FINAL_SEND_INFLIGHT    UINT32_C(1)
#define XDJ700_NAD_SUCCESS_INFLIGHT       UINT32_C(1)

#define XDJ700_NAD_HELPER_FIRST_PR        UINT32_C(0x08c20614)
#define XDJ700_NAD_HELPER_SECOND_PR       UINT32_C(0x08c20634)
#define XDJ700_NAD_HELPER_ALTERNATE_PR    UINT32_C(0x08c2059c)
#define XDJ700_NAD_HELPER_R4              UINT32_C(0x09d28aac)
#define XDJ700_NAD_HELPER_FIRST_R6        UINT32_C(0x118381e0)
#define XDJ700_NAD_HELPER_FIRST_R7        UINT32_C(0x00000930)
#define XDJ700_NAD_HELPER_SECOND_R6       UINT32_C(0x1183bde0)
#define XDJ700_NAD_HELPER_SECOND_R7       UINT32_C(0x00000010)
#define XDJ700_NAD_HELPER_SECOND_OFFSET   UINT32_C(0x0000f000)
#define XDJ700_NAD_HELPER_WINDOW_BYTES    UINT32_C(0x0000f010)
#define XDJ700_NAD_HELPER_DATA_ALIGNMENT  UINT32_C(4)
#define XDJ700_NAD_SESSION_ALIGNMENT      UINT32_C(8)
#define XDJ700_NAD_SLOT_GUARD             UINT32_C(0x00008008)
#define XDJ700_NAD_DDR_MIN                UINT32_C(0x0b000000)
/* End of the firmware-configured RTOS arena: C90956C + 1E00000.
 * Shared by adapter allocation checks, runtime readers, and session bounds. */
#define XDJ700_NAD_DDR_MAX_EXCLUSIVE      UINT32_C(0x0e70956c)

/* Stock 08C20254/08C2025A loads 08C2031C and installs this fixed P2
 * producer base. The complete data/metadata window ends at AE91FB10.
 * It is separate from the RTOS allocation arena; other aliases and nearby
 * P2 addresses are not interchangeable with this native helper contract. */
#define XDJ700_NAD_NATIVE_PRODUCER_BASE   UINT32_C(0xae910b00)

/* The caller also validates the session's field complements and ownership.
 * Preserve the configured cached producer region used by diagnostic callers,
 * and admit the one independently recovered native uncached window. */
static inline int xdj700_nad_producer_base_valid(
    uint32_t base, uint32_t floor, uint32_t limit)
{
    if (floor < XDJ700_NAD_DDR_MIN ||
        limit > XDJ700_NAD_DDR_MAX_EXCLUSIVE || floor >= limit ||
        limit - floor < XDJ700_NAD_HELPER_WINDOW_BYTES ||
        (base & (XDJ700_NAD_HELPER_DATA_ALIGNMENT - 1u)) != 0u)
        return 0;
    return base == XDJ700_NAD_NATIVE_PRODUCER_BASE ||
        (base >= floor && base - floor <=
            limit - floor - XDJ700_NAD_HELPER_WINDOW_BYTES);
}

#define XDJ700_NAD_ACTIVE_COMPLETION_PR   UINT32_C(0x08c2074e)
#define XDJ700_NAD_FINAL_COMPLETION_PR    UINT32_C(0x08c207e0)
#define XDJ700_NAD_COMPLETION_R6          UINT32_C(8)
#define XDJ700_NAD_COMPLETION_R7          UINT32_C(0xffffffff)

enum xdj700_nad_status {
    XDJ700_NAD_REJECTED = 0,
    XDJ700_NAD_OK = 1,
    XDJ700_NAD_WAITING = 2,
    XDJ700_NAD_READY = 3,
    XDJ700_NAD_DISABLED = 4
};

/* Never use an enum xdj700_nad_status as a Boolean. */
#define XDJ700_NAD_STATUS_IS_OK(value) ((value) == XDJ700_NAD_OK)

enum xdj700_nad_helper_disposition {
    XDJ700_NAD_HELPER_STOCK = 0,
    XDJ700_NAD_HELPER_ACCEPTED = 1,
    XDJ700_NAD_HELPER_QUARANTINED = 2
};

enum xdj700_nad_ticket_kind {
    XDJ700_NAD_TICKET_NONE = 0,
    XDJ700_NAD_TICKET_ACTIVE_SEND = 1,
    XDJ700_NAD_TICKET_FINAL_SEND = 2,
    XDJ700_NAD_TICKET_CLEANUP = 3,
    XDJ700_NAD_TICKET_FAILURE_CLEANUP_1_11 = 4,
    XDJ700_NAD_TICKET_FAILURE_CLEANUP_1_12 = 5,
    XDJ700_NAD_TICKET_FAILURE_CLEANUP_1_13 = 6
};

enum xdj700_nad_owner_kind {
    XDJ700_NAD_OWNER_OPENING = 1,
    XDJ700_NAD_OWNER_ACTIVE_BUSY = 2,
    XDJ700_NAD_OWNER_CLOSE = 3,
    XDJ700_NAD_OWNER_GATE_WRITER = 4,
    XDJ700_NAD_OWNER_GATE_READER = 5,
    XDJ700_NAD_OWNER_ACTIVE_SEND = 6,
    XDJ700_NAD_OWNER_CANCEL_OPENING = 7
};

/* Stable event numbers; callbacks are linked only when test hooks are on. */
enum xdj700_nad_test_event {
    XDJ700_NAD_TEST_AFTER_ARM_CLAIM = 1,
    XDJ700_NAD_TEST_AFTER_HELPER1_CLAIM = 2,
    XDJ700_NAD_TEST_AFTER_HELPER2_CLAIM = 3,
    XDJ700_NAD_TEST_AFTER_ACTIVE_SEND_CLAIM = 4,
    XDJ700_NAD_TEST_AFTER_CLEANUP_CLAIM = 5,
    XDJ700_NAD_TEST_AFTER_FINAL_SEND_CLAIM = 6,
    XDJ700_NAD_TEST_BEFORE_RESULT_1_12 = 7,
    XDJ700_NAD_TEST_BEFORE_RESULT_1_13 = 8,
    XDJ700_NAD_TEST_BEFORE_SUCCESS_CLAIM = 9,
    XDJ700_NAD_TEST_AFTER_SUCCESS_CLAIM = 10,
    XDJ700_NAD_TEST_BEFORE_CLEAR_ARMED = 11,
    XDJ700_NAD_TEST_BEFORE_CLEAR_CLEANUP = 12,
    XDJ700_NAD_TEST_BEFORE_CLEAR_FINAL = 13,
    XDJ700_NAD_TEST_BEFORE_CLEAR_SUCCESS = 14,
    XDJ700_NAD_TEST_BEFORE_RESULT_1_14 = 15,
    XDJ700_NAD_TEST_AFTER_RESULT_1_13 = 16,
    XDJ700_NAD_TEST_AFTER_CLEANUP_SNAPSHOT = 17,
    XDJ700_NAD_TEST_AFTER_FAILURE_CLEANUP_CLAIM = 18,
    XDJ700_NAD_TEST_BEFORE_FAILED_CLEAR_FINAL = 19,
    XDJ700_NAD_TEST_BEFORE_FAILED_CLEAR_CLEANUP = 20,
    XDJ700_NAD_TEST_BEFORE_FAILED_CLEAR_ARMED = 21,
    XDJ700_NAD_TEST_AFTER_FAILED_CLEAR = 22
};

/* Exact four-word P2 state-island layout. */
typedef struct xdj700_nad_island {
    uint32_t armed_generation;
    uint32_t cleanup_generation;
    uint32_t final_send_generation;
    uint32_t failed_generation;
} xdj700_nad_island;

typedef struct xdj700_nad_binding {
    uint32_t lifecycle;
    uint32_t file_key;
    uint32_t descriptor_key;
    uint32_t dynamic_state;
    uint32_t slot_guard;
} xdj700_nad_binding;

/*
 * By-value snapshots prevent callers from aliasing before and after.  They do
 * not acquire an owner: kind and actual_session_address are attestations made
 * by integration code after it has acquired the corresponding target lock.
 */
typedef struct xdj700_nad_owner {
    uint32_t kind;
    uint32_t actual_session_address;
    xdj700_nad_binding before;
    xdj700_nad_binding after;
} xdj700_nad_owner;

typedef struct xdj700_nad_helper_call {
    uint32_t return_pc;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
} xdj700_nad_helper_call;

typedef struct xdj700_nad_completion_call {
    uint32_t return_pc;
    uint32_t message_word0;
    uint32_t message_word1;
    uint32_t r6;
    uint32_t r7;
} xdj700_nad_completion_call;

typedef struct xdj700_nad_session {
    uint32_t magic;
    uint32_t generation;
    uint32_t step;
    uint32_t session_key;
    uint32_t file_key;
    uint32_t descriptor_key;
    uint32_t slot_guard;
    uint32_t allocation_floor;
    uint32_t allocation_limit;
    uint32_t allocation_floor_inverse;
    uint32_t allocation_limit_inverse;
    uint32_t producer_floor;
    uint32_t producer_limit;
    uint32_t producer_floor_inverse;
    uint32_t producer_limit_inverse;
    uint32_t pcm_cursor;
    uint32_t pcm_cursor_inverse;
    uint32_t pcm_fnv;
    uint32_t pcm_fnv_inverse;
    uint32_t helper_data_base;
    uint32_t helper_data_base_inverse;
} xdj700_nad_session;

#if XDJ700_NAD_MODEL_SESSION_ADDRESSES
uint32_t xdj700_nad_test_model_session_address(
    const xdj700_nad_session *candidate);
#endif

/*
 * Tickets are deterministic corruption guards, not capabilities.  Authority
 * comes from an exact registered island claim.  Per-field complements avoid
 * the old paired-XOR cancellation class.
 *
 * Trusted-caller precondition for every ticket_out argument: it designates a
 * private, writable, naturally aligned 56-byte object for the full call and
 * is physically disjoint from every other argument and state object,
 * including through SH P1/P2 aliases.  The core validates ticket contents;
 * it cannot validate arbitrary C storage or physical aliases.  Integration
 * must use a local stack ticket and prove this precondition in its static and
 * emulator tests.
 */
typedef struct xdj700_nad_ticket {
    uint32_t magic;
    uint32_t magic_inverse;
    uint32_t kind;
    uint32_t kind_inverse;
    uint32_t generation;
    uint32_t generation_inverse;
    uint32_t file_key;
    uint32_t file_key_inverse;
    uint32_t descriptor_key;
    uint32_t descriptor_key_inverse;
    uint32_t dynamic_state;
    uint32_t dynamic_state_inverse;
    uint32_t slot_guard;
    uint32_t slot_guard_inverse;
} xdj700_nad_ticket;

typedef char xdj700_nad_island_must_be_16_bytes[
    sizeof(xdj700_nad_island) == 16u ? 1 : -1];
typedef char xdj700_nad_island_armed_at_0[
    offsetof(xdj700_nad_island, armed_generation) == 0u ? 1 : -1];
typedef char xdj700_nad_island_cleanup_at_4[
    offsetof(xdj700_nad_island, cleanup_generation) == 4u ? 1 : -1];
typedef char xdj700_nad_island_final_at_8[
    offsetof(xdj700_nad_island, final_send_generation) == 8u ? 1 : -1];
typedef char xdj700_nad_island_failed_at_12[
    offsetof(xdj700_nad_island, failed_generation) == 12u ? 1 : -1];
typedef char xdj700_nad_binding_must_be_20_bytes[
    sizeof(xdj700_nad_binding) == 20u ? 1 : -1];
typedef char xdj700_nad_owner_must_be_48_bytes[
    sizeof(xdj700_nad_owner) == 48u ? 1 : -1];
typedef char xdj700_nad_owner_before_at_8[
    offsetof(xdj700_nad_owner, before) == 8u ? 1 : -1];
typedef char xdj700_nad_owner_after_at_28[
    offsetof(xdj700_nad_owner, after) == 28u ? 1 : -1];
typedef char xdj700_nad_helper_call_must_be_20_bytes[
    sizeof(xdj700_nad_helper_call) == 20u ? 1 : -1];
typedef char xdj700_nad_helper_return_pc_at_0[
    offsetof(xdj700_nad_helper_call, return_pc) == 0u ? 1 : -1];
typedef char xdj700_nad_helper_r7_at_16[
    offsetof(xdj700_nad_helper_call, r7) == 16u ? 1 : -1];
typedef char xdj700_nad_completion_call_must_be_20_bytes[
    sizeof(xdj700_nad_completion_call) == 20u ? 1 : -1];
typedef char xdj700_nad_completion_return_pc_at_0[
    offsetof(xdj700_nad_completion_call, return_pc) == 0u ? 1 : -1];
typedef char xdj700_nad_completion_r7_at_16[
    offsetof(xdj700_nad_completion_call, r7) == 16u ? 1 : -1];
typedef char xdj700_nad_session_must_be_84_bytes[
    sizeof(xdj700_nad_session) == 84u ? 1 : -1];
typedef char xdj700_nad_session_magic_at_0[
    offsetof(xdj700_nad_session, magic) == 0u ? 1 : -1];
typedef char xdj700_nad_session_generation_at_4[
    offsetof(xdj700_nad_session, generation) == 4u ? 1 : -1];
typedef char xdj700_nad_session_step_at_8[
    offsetof(xdj700_nad_session, step) == 8u ? 1 : -1];
typedef char xdj700_nad_session_key_at_12[
    offsetof(xdj700_nad_session, session_key) == 12u ? 1 : -1];
typedef char xdj700_nad_session_helper_inverse_at_80[
    offsetof(xdj700_nad_session, helper_data_base_inverse) == 80u ? 1 : -1];
typedef char xdj700_nad_ticket_must_be_56_bytes[
    sizeof(xdj700_nad_ticket) == 56u ? 1 : -1];
typedef char xdj700_nad_ticket_magic_at_0[
    offsetof(xdj700_nad_ticket, magic) == 0u ? 1 : -1];
typedef char xdj700_nad_ticket_magic_inverse_at_4[
    offsetof(xdj700_nad_ticket, magic_inverse) == 4u ? 1 : -1];
typedef char xdj700_nad_ticket_kind_at_8[
    offsetof(xdj700_nad_ticket, kind) == 8u ? 1 : -1];
typedef char xdj700_nad_ticket_kind_inverse_at_12[
    offsetof(xdj700_nad_ticket, kind_inverse) == 12u ? 1 : -1];
typedef char xdj700_nad_ticket_generation_at_16[
    offsetof(xdj700_nad_ticket, generation) == 16u ? 1 : -1];
typedef char xdj700_nad_ticket_generation_inverse_at_20[
    offsetof(xdj700_nad_ticket, generation_inverse) == 20u ? 1 : -1];
typedef char xdj700_nad_ticket_file_at_24[
    offsetof(xdj700_nad_ticket, file_key) == 24u ? 1 : -1];
typedef char xdj700_nad_ticket_file_inverse_at_28[
    offsetof(xdj700_nad_ticket, file_key_inverse) == 28u ? 1 : -1];
typedef char xdj700_nad_ticket_descriptor_at_32[
    offsetof(xdj700_nad_ticket, descriptor_key) == 32u ? 1 : -1];
typedef char xdj700_nad_ticket_descriptor_inverse_at_36[
    offsetof(xdj700_nad_ticket, descriptor_key_inverse) == 36u ? 1 : -1];
typedef char xdj700_nad_ticket_dynamic_at_40[
    offsetof(xdj700_nad_ticket, dynamic_state) == 40u ? 1 : -1];
typedef char xdj700_nad_ticket_dynamic_inverse_at_44[
    offsetof(xdj700_nad_ticket, dynamic_state_inverse) == 44u ? 1 : -1];
typedef char xdj700_nad_ticket_slot_at_48[
    offsetof(xdj700_nad_ticket, slot_guard) == 48u ? 1 : -1];
typedef char xdj700_nad_ticket_slot_inverse_at_52[
    offsetof(xdj700_nad_ticket, slot_guard_inverse) == 52u ? 1 : -1];

/*
 * Registered state transitions (all comparisons are exact):
 *
 * operation                 required owner     shared/result transition
 * begin + arm               OPENING            1.10 -> 1.11; arm 0 -> gen
 * handoff                   OPENING             1.11 -> 1.12, before selector
 * PCM/helpers               ACTIVE_BUSY         session steps
 * active prepare/commit     ACTIVE_SEND         cleanup 0 -> gen|1,
 *                                                1.12 -> 1.13, then
 *                                                cleanup gen|1 -> gen|2
 * cleanup prepare           CLOSE               cleanup gen|2 -> gen|3
 * cleanup commit            GATE_READER/WRITER  cleanup gen|3 -> gen
 *                           RETIRING
 * failure cleanup prepare   CLOSE               failed==gen; cleanup
 *                                                {0,gen|1,gen|2} -> gen|3
 * failure cleanup commit    GATE_READER/WRITER  RETIRING; exact failure
 *                                                ticket; cleanup gen|3 -> gen
 * final prepare/commit      GATE_READER         final 0 -> gen|1 -> gen
 *                           RETIRING; cleanup==gen; sessionless
 * retire                    last reader         RETIRING -> IDLE, promote
 *                                                activity reader -> writer
 * finish                    GATE_WRITER IDLE    failure 0 -> gen|1 arbiter,
 *                                                strict clears, failure
 *                                                gen|1 -> 0, 1.13 -> 1.14
 * failed finish             GATE_WRITER IDLE    strict clears while retaining
 *                                                failure==gen and result
 */

XDJ700_NAD_NODISCARD int xdj700_nad_enabled(void);
XDJ700_NAD_NODISCARD uint8_t xdj700_nad_oracle_byte(uint32_t byte_offset);

XDJ700_NAD_NODISCARD enum xdj700_nad_status xdj700_nad_begin_eligible(
    xdj700_nad_session *session, xdj700_nad_island *island,
    uint32_t *result_word, uint32_t generation, uint32_t session_key,
    uint32_t file_key, uint32_t descriptor_key, uint32_t slot_guard,
    uint32_t allocation_floor, uint32_t allocation_limit,
    uint32_t producer_floor, uint32_t producer_limit,
    const xdj700_nad_owner *owner);

/*
 * Arm under OPENING after dynamic_state publishes the exact session address.
 * Result remains 1.11 until lifetime-safe handoff.
 */
XDJ700_NAD_NODISCARD enum xdj700_nad_status xdj700_nad_arm(
    xdj700_nad_session *session, xdj700_nad_island *island,
    uint32_t *result_word, const xdj700_nad_owner *owner);

/*
 * Call only after local cleanup and before a successful handoff commit.
 * Exact OPENING and exact CANCEL_OPENING owners are accepted; the latter is
 * the stable owner to use after the lifecycle has published cancellation.
 * Cancellation first wins PREPARED/ARMED -> FAILED atomically and only then
 * may clear armed.  If handoff wins ARMED -> ACTIVE, cancellation rejects and
 * can never clear that committed generation's armed word.
 */
XDJ700_NAD_NODISCARD enum xdj700_nad_status
xdj700_nad_cancel_before_handoff(
    xdj700_nad_session *session, xdj700_nad_island *island,
    const xdj700_nad_owner *owner);

/*
 * A failure mutation requires the exact owner for the operation being failed;
 * ACTIVE_SEND is accepted while active-completion prepare/send/commit owns the
 * dedicated phase.
 */
XDJ700_NAD_NODISCARD enum xdj700_nad_status xdj700_nad_fail_generation(
    xdj700_nad_session *session, xdj700_nad_island *island,
    uint32_t generation, const xdj700_nad_owner *owner);

/*
 * Commit under the same exact OPENING generation after native open succeeds,
 * but before publishing selector 11 or transitioning OPENING -> ACTIVE.
 */
XDJ700_NAD_NODISCARD enum xdj700_nad_status xdj700_nad_commit_handoff(
    xdj700_nad_session *session, xdj700_nad_island *island,
    uint32_t *result_word, const xdj700_nad_owner *owner);

XDJ700_NAD_NODISCARD enum xdj700_nad_status xdj700_nad_observe_pcm(
    xdj700_nad_session *session, xdj700_nad_island *island,
    const xdj700_nad_owner *owner, uint32_t byte_offset,
    const uint8_t *bytes, uint32_t byte_count);

XDJ700_NAD_NODISCARD enum xdj700_nad_status xdj700_nad_finish_pcm(
    xdj700_nad_session *session, xdj700_nad_island *island,
    const xdj700_nad_owner *owner, uint32_t exact_decoder_end);

/* Native WAVE sniffing may overlap later producer reads. Verify all repeated
 * bytes, certify only the contiguous new suffix, and finish exactly once. */
XDJ700_NAD_NODISCARD enum xdj700_nad_status xdj700_nad_observe_pcm_window(
    xdj700_nad_session *session, xdj700_nad_island *island,
    const xdj700_nad_owner *owner, uint32_t byte_offset,
    const uint8_t *bytes, uint32_t byte_count, uint32_t exact_decoder_end);

XDJ700_NAD_NODISCARD enum xdj700_nad_helper_disposition
xdj700_nad_handle_helper(
    xdj700_nad_session *session, xdj700_nad_island *island,
    const xdj700_nad_owner *owner, const xdj700_nad_helper_call *call);

/* Requires exact same-generation ACTIVE_SEND, already published from ACTIVE. */
XDJ700_NAD_NODISCARD enum xdj700_nad_status
xdj700_nad_prepare_active_completion(
    xdj700_nad_session *session, xdj700_nad_island *island,
    uint32_t *result_word, const xdj700_nad_owner *owner,
    const xdj700_nad_completion_call *call, xdj700_nad_ticket *ticket_out);

/*
 * Requires a fresh exact same-generation ACTIVE_SEND owner after native send.
 * A rejected commit leaves a failure latch and does not convert INFLIGHT to
 * DONE; integration must use its terminal failure/reset path, not the normal
 * success cleanup path.
 */
XDJ700_NAD_NODISCARD enum xdj700_nad_status
xdj700_nad_commit_active_completion(
    xdj700_nad_island *island, uint32_t *result_word,
    const xdj700_nad_ticket *ticket, int native_return,
    const xdj700_nad_owner *owner);

/*
 * Exact same-generation active-send INFLIGHT returns WAITING without a
 * failure latch.  Seeing active-send DONE acquires the preceding 1.13
 * publication, so the caller may retry after the completion owner releases.
 */
XDJ700_NAD_NODISCARD enum xdj700_nad_status xdj700_nad_prepare_cleanup(
    xdj700_nad_session *session, xdj700_nad_island *island,
    uint32_t *result_word, const xdj700_nad_owner *owner,
    xdj700_nad_ticket *ticket_out);

/* Requires a gate READER or WRITER over exact same-generation RETIRING. */
XDJ700_NAD_NODISCARD enum xdj700_nad_status xdj700_nad_commit_cleanup(
    xdj700_nad_island *island, uint32_t *result_word,
    const xdj700_nad_ticket *ticket, int cleanup_succeeded,
    const xdj700_nad_owner *owner);

/*
 * Terminal cleanup for an already failed generation.  This is deliberately
 * separate from success cleanup: it never accepts failed==0 and cannot
 * advance the result.  Three distinct appended ticket kinds bind the exact
 * 1.11/1.12/1.13 last-success result without changing the 56-byte ticket ABI.
 * Prepare requires an exact CLOSE owner over the live session.  Commit
 * requires a sessionless, exact same-generation RETIRING gate reader or
 * writer after physical cleanup has succeeded.
 */
XDJ700_NAD_NODISCARD enum xdj700_nad_status
xdj700_nad_prepare_failure_cleanup(
    xdj700_nad_session *session, xdj700_nad_island *island,
    uint32_t *result_word, const xdj700_nad_owner *owner,
    xdj700_nad_ticket *ticket_out);

XDJ700_NAD_NODISCARD enum xdj700_nad_status
xdj700_nad_commit_failure_cleanup(
    xdj700_nad_island *island, uint32_t *result_word,
    const xdj700_nad_ticket *ticket,
    int cleanup_succeeded, const xdj700_nad_owner *owner);

XDJ700_NAD_NODISCARD enum xdj700_nad_status
xdj700_nad_prepare_final_completion(
    xdj700_nad_session *session, xdj700_nad_island *island,
    uint32_t *result_word, const xdj700_nad_owner *owner,
    const xdj700_nad_completion_call *call, xdj700_nad_ticket *ticket_out);

XDJ700_NAD_NODISCARD enum xdj700_nad_status
xdj700_nad_commit_final_completion(
    xdj700_nad_island *island, uint32_t *result_word,
    const xdj700_nad_ticket *ticket, int native_return,
    const xdj700_nad_owner *owner);

/*
 * The only API that may publish 1.14.  Both terminal commits merely return
 * READY.  Caller must hold the gate writer, call this function, then release
 * the writer with exact-owner retry semantics.  A failed writer release means
 * the surrounding diagnostic is not qualified even if this function returned
 * OK; that residual integration condition cannot be represented in this core.
 */
XDJ700_NAD_NODISCARD enum xdj700_nad_status
xdj700_nad_finish_under_writer(
    xdj700_nad_island *island, uint32_t *result_word,
    uint32_t generation, const xdj700_nad_owner *owner);

/*
 * Retire a physically cleaned failed generation under an exact IDLE gate
 * writer.  Success clears only armed/cleanup/final progress; failed==gen and
 * the exact known last-success result remain as durable evidence.  Any
 * mismatch or partial interleaving rejects without ever clearing failure.
 */
XDJ700_NAD_NODISCARD enum xdj700_nad_status
xdj700_nad_finish_failed_under_writer(
    xdj700_nad_island *island, uint32_t *result_word,
    uint32_t generation, const xdj700_nad_owner *owner);

/* Candidate stock route only; caller must hold the activity reader. */
XDJ700_NAD_NODISCARD enum xdj700_nad_helper_disposition
xdj700_nad_classify_idle_helper(
    const xdj700_nad_island *island, const xdj700_nad_owner *owner);

#endif
