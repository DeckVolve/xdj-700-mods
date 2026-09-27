#ifndef XDJ700_STOCK_LOSSLESS_SYNC_H
#define XDJ700_STOCK_LOSSLESS_SYNC_H

/*
 * Permanent synchronization metadata for the retained XDJ-700 lossless
 * adapter.  The dynamic codec allocation is never used as a lock: its address
 * may be read only after the caller atomically owns the matching lifecycle
 * phase.  Target builds compile this header's users for SH-4A with
 * -matomic-model=hard-llcs,strict, producing MOVLI.L/MOVCO.L without an RTOS
 * helper.
 */

#include <stdint.h>
#include <stddef.h>

#ifndef STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
#define STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE 0
#endif
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE != 0 && \
    STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE != 1
#error "STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE must be 0 or 1"
#endif

#define STOCK_SYNC_PHASE_MASK       UINT32_C(0x0000000f)
#define STOCK_SYNC_GENERATION_MASK  UINT32_C(0xfffffff0)
#define STOCK_SYNC_GENERATION_STEP  UINT32_C(0x00000010)

#ifndef STOCK_LOSSLESS_GATE_ACTIVITY_ADDRESS
#define STOCK_LOSSLESS_GATE_ACTIVITY_ADDRESS ((uintptr_t)0xA80FBAFCu)
#endif
#define STOCK_SYNC_GATE_WRITER UINT32_C(0x80000000)
#define STOCK_SYNC_GATE_READERS UINT32_C(0x7fffffff)

#ifndef STOCK_SYNC_CLOSE_UPGRADE_TEST_HOOK
#define STOCK_SYNC_CLOSE_UPGRADE_TEST_HOOK(control_) ((void)(control_))
#endif

enum stock_sync_phase {
    STOCK_SYNC_IDLE = 0,
    STOCK_SYNC_CLAIMED = 1,
    STOCK_SYNC_PROBING = 2,
    STOCK_SYNC_OPENING = 3,
    STOCK_SYNC_ACTIVE = 4,
    STOCK_SYNC_ACTIVE_BUSY = 5,
    STOCK_SYNC_CLOSING = 6,
    STOCK_SYNC_CLOSE_FAILED = 7,
    STOCK_SYNC_POISONED = 8,
    /* CRC is complete and the committed record is visible, but the adapter
     * has not yet uniquely adopted this generation as CLAIMED. */
    STOCK_SYNC_GATE_READY = 9,
    STOCK_SYNC_OPEN_CLEANUP = 10,
    STOCK_SYNC_RETIRING = 11,
    STOCK_SYNC_CANCEL_PROBING = 12,
    STOCK_SYNC_CANCEL_OPENING = 13,
    /* An active operation that must cross a blocking native send publishes
     * this exact phase before preparing shared completion state.  Close and
     * every other operation reject it without mutation; only its owner may
     * return the same generation to ACTIVE_BUSY for commit. */
    STOCK_SYNC_ACTIVE_SEND = 14,
    /* Owned by the checksum gate while it validates the complete payload.
     * The gate advances this exact generation to GATE_READY only after CRC
     * and record publication; every operation/close wrapper rejects it. */
    STOCK_SYNC_GATE_CHECKING = 15
};

enum stock_sync_route {
    STOCK_SYNC_ROUTE_REJECT = 0,
    STOCK_SYNC_ROUTE_DELEGATE = 1,
    STOCK_SYNC_ROUTE_OWNED = 2,
    STOCK_SYNC_ROUTE_POISONED = 3
};

enum stock_sync_identity {
    STOCK_SYNC_IDENTITY_FILE = 0,
    STOCK_SYNC_IDENTITY_DESCRIPTOR = 1
};

typedef struct {
    uint32_t lifecycle;
    uint32_t file_key;
    uint32_t descriptor_key;
    uint32_t dynamic_state;
    uint32_t slot_guard;
} stock_lossless_sync_control;

typedef char stock_lossless_sync_control_must_be_20_bytes[
    sizeof(stock_lossless_sync_control) == 20u ? 1 : -1];
typedef char stock_lossless_sync_lifecycle_at_0[
    offsetof(stock_lossless_sync_control, lifecycle) == 0u ? 1 : -1];
typedef char stock_lossless_sync_file_at_4[
    offsetof(stock_lossless_sync_control, file_key) == 4u ? 1 : -1];
typedef char stock_lossless_sync_descriptor_at_8[
    offsetof(stock_lossless_sync_control, descriptor_key) == 8u ? 1 : -1];
typedef char stock_lossless_sync_state_at_12[
    offsetof(stock_lossless_sync_control, dynamic_state) == 12u ? 1 : -1];
typedef char stock_lossless_sync_slot_at_16[
    offsetof(stock_lossless_sync_control, slot_guard) == 16u ? 1 : -1];

static uint32_t stock_sync_load(const uint32_t *word)
{
    return __atomic_load_n(word, __ATOMIC_ACQUIRE);
}

static void stock_sync_store(uint32_t *word, uint32_t value)
{
    __atomic_store_n(word, value, __ATOMIC_RELEASE);
}

static int stock_sync_cas(uint32_t *word, uint32_t *expected,
                          uint32_t desired)
{
    return __atomic_compare_exchange_n(word, expected, desired, 0,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static uint32_t stock_sync_phase_of(uint32_t lifecycle)
{
    return lifecycle & STOCK_SYNC_PHASE_MASK;
}

static uint32_t stock_sync_with_phase(uint32_t lifecycle, uint32_t phase)
{
    return (lifecycle & STOCK_SYNC_GENERATION_MASK) | phase;
}

static uint32_t *stock_sync_gate_activity(void)
{
    return (uint32_t *)STOCK_LOSSLESS_GATE_ACTIVITY_ADDRESS;
}

static int stock_sync_idle_bindings_zero(
    const stock_lossless_sync_control *control)
{
    return stock_sync_load(&control->file_key) == 0u &&
        stock_sync_load(&control->descriptor_key) == 0u &&
        stock_sync_load(&control->dynamic_state) == 0u &&
        stock_sync_load(&control->slot_guard) == 0u;
}

#if !STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
static int stock_sync_gate_writer_acquire(void)
{
    uint32_t expected = 0u;

    /* Dispatch is availability-sensitive: a reader already in stock makes
     * this attempt lose immediately without changing the lifecycle. */
    return stock_sync_cas(
        stock_sync_gate_activity(), &expected, STOCK_SYNC_GATE_WRITER);
}

static int stock_sync_gate_writer_release(void)
{
    uint32_t *activity = stock_sync_gate_activity();

    /* Once WRITER is owned, reservation loss may not abandon it.  Retry only
     * while the exact owned word remains; any other value fails closed. */
    for (;;) {
        uint32_t expected = STOCK_SYNC_GATE_WRITER;

        if (stock_sync_cas(activity, &expected, 0u))
            return 1;
        if (expected != STOCK_SYNC_GATE_WRITER)
            return 0;
    }
}
#endif

static int stock_sync_gate_writer_downgrade_reader(void)
{
    uint32_t *activity = stock_sync_gate_activity();

    /* A gate-originated dispatch continues through the adapter and then the
     * relocated stock body.  Downgrade, rather than unlock, so a later CRC
     * writer cannot overlap that native continuation if adapter cleanup has
     * already returned the lifecycle to IDLE. */
    for (;;) {
        uint32_t expected = STOCK_SYNC_GATE_WRITER;

        if (stock_sync_cas(activity, &expected, 1u))
            return 1;
        if (expected != STOCK_SYNC_GATE_WRITER)
            return 0;
    }
}

/* A matching custom close is entered through the gate while holding exactly
 * one activity reader.  Upgrade only that sole reader to WRITER before the
 * ACTIVE/CLOSE_FAILED -> CLOSING transition.  If another gate call is still
 * live (notably the dispatch continuation which just published ACTIVE), its
 * additional reader makes the close lose without changing lifecycle. */
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
static int stock_sync_gate_reader_upgrade_writer(void)
{
    uint32_t *activity = stock_sync_gate_activity();

    for (;;) {
        uint32_t expected = 1u;

        if (stock_sync_cas(activity, &expected, STOCK_SYNC_GATE_WRITER))
            return 1;
        if (expected != 1u)
            return 0;
    }
}
#endif

static int stock_sync_try_dispatch_claim_mode(
    stock_lossless_sync_control *control, uint32_t *claim_out,
    int *gate_adopted_out)
{
    uint32_t observed = stock_sync_load(&control->lifecycle);
    uint32_t desired;
    int gate_adopted = 0;

    if (gate_adopted_out != 0)
        *gate_adopted_out = 0;

    if (stock_sync_phase_of(observed) == STOCK_SYNC_GATE_READY) {
        /* The gate has completed CRC, committed the record, and exclusively
         * owns this generation.  Adoption closes both premature direct
         * ingress during CHECKING and the commit-to-claim window. */
        if ((observed & STOCK_SYNC_GENERATION_MASK) == 0u ||
            stock_sync_load(stock_sync_gate_activity()) !=
            STOCK_SYNC_GATE_WRITER)
            return 0;
        desired = stock_sync_with_phase(observed, STOCK_SYNC_CLAIMED);
        gate_adopted = 1;
    }
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
    else {
        /* There is deliberately no production IDLE claim path.  Keeping this
         * exclusion at preprocessing time makes a direct-capable payload a
         * different, host/probe-only artifact rather than a runtime mode. */
        return 0;
    }
#else
    else {
        if (stock_sync_phase_of(observed) != STOCK_SYNC_IDLE ||
            (observed & STOCK_SYNC_GENERATION_MASK) ==
                STOCK_SYNC_GENERATION_MASK)
            return 0;
        /* Direct test/probe entry follows the same writer-first ordering as
         * the gate.  Recheck the exact full lifecycle after acquisition. */
        if (!stock_sync_gate_writer_acquire())
            return 0;
        if (stock_sync_load(&control->lifecycle) != observed) {
            (void)stock_sync_gate_writer_release();
            return 0;
        }
        if (!stock_sync_idle_bindings_zero(control)) {
            (void)stock_sync_gate_writer_release();
            return 0;
        }
        desired = (observed + STOCK_SYNC_GENERATION_STEP) |
            STOCK_SYNC_CLAIMED;
    }
#endif
    if (!stock_sync_cas(&control->lifecycle, &observed, desired)) {
#if !STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
        if (!gate_adopted)
            (void)stock_sync_gate_writer_release();
#endif
        return 0;
    }
    if (claim_out != 0)
        *claim_out = desired;
    if (gate_adopted_out != 0)
        *gate_adopted_out = gate_adopted;
    return 1;
}

static int __attribute__((unused)) stock_sync_dispatch_claim_abort(
    stock_lossless_sync_control *control, uint32_t claim)
{
    uint32_t expected = claim;

    if (stock_sync_phase_of(claim) != STOCK_SYNC_CLAIMED)
        return 0;
    return stock_sync_cas(
        &control->lifecycle, &expected,
        stock_sync_with_phase(claim, STOCK_SYNC_IDLE));
}

/* Readers cover the complete duration of an intentional stock fallback.
 * The post-increment lifecycle comparison closes observation-to-call races:
 * dispatch must acquire the writer bit before it may publish GATE_CHECKING,
 * while any earlier reader makes that writer attempt lose with lifecycle
 * untouched.  Reader release retries until its acquired count is retired; it
 * never abandons a live reference after bounded contention. */
static int __attribute__((unused)) stock_sync_gate_reader_enter(
    stock_lossless_sync_control *control, uint32_t expected_lifecycle)
{
    uint32_t *activity = stock_sync_gate_activity();

    for (;;) {
        uint32_t observed = stock_sync_load(activity);
        uint32_t expected;

        if ((observed & STOCK_SYNC_GATE_WRITER) != 0u ||
            (observed & STOCK_SYNC_GATE_READERS) ==
                STOCK_SYNC_GATE_READERS)
            return 0;
        expected = observed;
        if (!stock_sync_cas(activity, &expected, observed + 1u))
            continue;
        if (stock_sync_load(&control->lifecycle) == expected_lifecycle &&
            (stock_sync_phase_of(expected_lifecycle) != STOCK_SYNC_IDLE ||
             stock_sync_idle_bindings_zero(control)))
            return 1;
        for (;;) {
            observed = stock_sync_load(activity);
            if ((observed & STOCK_SYNC_GATE_WRITER) != 0u ||
                (observed & STOCK_SYNC_GATE_READERS) == 0u)
                return 0;
            expected = observed;
            if (stock_sync_cas(activity, &expected, observed - 1u))
                return 0;
        }
    }
}

static int __attribute__((unused)) stock_sync_gate_reader_leave(void)
{
    uint32_t *activity = stock_sync_gate_activity();

    for (;;) {
        uint32_t observed = stock_sync_load(activity);
        uint32_t expected;

        if ((observed & STOCK_SYNC_GATE_WRITER) != 0u ||
            (observed & STOCK_SYNC_GATE_READERS) == 0u)
            return 0;
        expected = observed;
        if (stock_sync_cas(activity, &expected, observed - 1u))
            return 1;
    }
}

/* Complete the activity side of a dispatch claim.  Both gate-adopted and
 * direct test/probe claims already own the exact writer word before CLAIMED
 * is published, so neither can bypass an in-flight stock fallback reader.
 * A gate caller has installed a return veneer and therefore downgrades its
 * writer to one reader spanning adapter work plus the relocated stock body.
 * A direct diagnostic caller has no such veneer and releases normally. */
static int stock_sync_gate_dispatch_barrier(
    stock_lossless_sync_control *control, uint32_t claim, int gate_adopted)
{
    if (stock_sync_load(&control->lifecycle) != claim)
        return 0;
    if (gate_adopted)
        return stock_sync_gate_writer_downgrade_reader();
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
    return 0;
#else
    return stock_sync_gate_writer_release();
#endif
}

static int __attribute__((unused)) stock_sync_try_dispatch_claim(
    stock_lossless_sync_control *control, uint32_t *claim_out)
{
    uint32_t claim = 0u;
    int gate_adopted = 0;

    if (!stock_sync_try_dispatch_claim_mode(
            control, &claim, &gate_adopted) ||
        !stock_sync_gate_dispatch_barrier(
            control, claim, gate_adopted))
        return 0;
    if (claim_out != 0)
        *claim_out = claim;
    return 1;
}

/* Publish callback owner keys before entering the callback-capable PROBING
 * phase.  The lifecycle release is the publication edge for all four words. */
static int stock_sync_publish_probing(
    stock_lossless_sync_control *control, uint32_t claim,
    uint32_t file_key, uint32_t descriptor_key, uint32_t slot_guard)
{
    uint32_t expected = claim;

    if (stock_sync_phase_of(claim) != STOCK_SYNC_CLAIMED ||
        file_key == 0u || descriptor_key == 0u ||
        stock_sync_load(&control->lifecycle) != claim)
        return 0;
    stock_sync_store(&control->file_key, file_key);
    stock_sync_store(&control->descriptor_key, descriptor_key);
    stock_sync_store(&control->dynamic_state, 0u);
    stock_sync_store(&control->slot_guard, slot_guard);
    return stock_sync_cas(
        &control->lifecycle, &expected,
        stock_sync_with_phase(claim, STOCK_SYNC_PROBING));
}

/* Publish dynamic state before decoder open, whose codec callbacks require
 * dual_format_runtime_current() to resolve that state synchronously. */
static int stock_sync_publish_opening(
    stock_lossless_sync_control *control, uint32_t claim,
    uint32_t dynamic_state)
{
    uint32_t expected = stock_sync_with_phase(claim, STOCK_SYNC_PROBING);

    if (stock_sync_phase_of(claim) != STOCK_SYNC_CLAIMED ||
        dynamic_state == 0u ||
        stock_sync_load(&control->lifecycle) != expected)
        return 0;
    stock_sync_store(&control->dynamic_state, dynamic_state);
    return stock_sync_cas(
        &control->lifecycle, &expected,
        stock_sync_with_phase(claim, STOCK_SYNC_OPENING));
}

/* The exact-fixture open-only diagnostic deliberately never enters ACTIVE. */
static __attribute__((unused)) int stock_sync_activate(
    stock_lossless_sync_control *control, uint32_t claim)
{
    uint32_t expected = stock_sync_with_phase(claim, STOCK_SYNC_OPENING);

    if (stock_sync_phase_of(claim) != STOCK_SYNC_CLAIMED)
        return 0;
    return stock_sync_cas(
        &control->lifecycle, &expected,
        stock_sync_with_phase(claim, STOCK_SYNC_ACTIVE));
}

/* Normal open rollback first acquires a cleanup phase.  No new dispatch may
 * begin while source-position restoration, decoder close, or release runs.
 * If another context already poisoned the generation, the dispatch owner may
 * still clean up its own local resources but must never publish IDLE. */
static int stock_sync_open_cleanup_enter(
    stock_lossless_sync_control *control, uint32_t claim)
{
    const uint32_t phases[] = {
        STOCK_SYNC_OPENING, STOCK_SYNC_PROBING, STOCK_SYNC_CLAIMED,
        STOCK_SYNC_CANCEL_OPENING, STOCK_SYNC_CANCEL_PROBING
    };
    unsigned index;

    if (stock_sync_phase_of(claim) != STOCK_SYNC_CLAIMED)
        return 0;
    for (index = 0u; index < sizeof(phases) / sizeof(phases[0]); ++index) {
        uint32_t expected = stock_sync_with_phase(claim, phases[index]);

        if (stock_sync_cas(
                &control->lifecycle, &expected,
                stock_sync_with_phase(claim, STOCK_SYNC_OPEN_CLEANUP)))
            return 1;
        if ((expected & STOCK_SYNC_GENERATION_MASK) !=
            (claim & STOCK_SYNC_GENERATION_MASK))
            return 0;
        if (stock_sync_phase_of(expected) == STOCK_SYNC_POISONED)
            return 0;
    }
    return 0;
}

static int stock_sync_poison_exact(stock_lossless_sync_control *control,
                                   uint32_t expected)
{
    return stock_sync_cas(
        &control->lifecycle, &expected,
        stock_sync_with_phase(expected, STOCK_SYNC_POISONED));
}

/* Return OWNED only while holding ACTIVE_BUSY.  The exact lifecycle token is
 * returned so a stale operation cannot release a later generation. */
static enum stock_sync_route stock_sync_operation_enter(
    stock_lossless_sync_control *control, uint32_t owner_key,
    enum stock_sync_identity identity, uint32_t *dynamic_state_out,
    uint32_t *busy_token_out)
{
    unsigned retry;

    if (dynamic_state_out != 0)
        *dynamic_state_out = 0u;
    if (busy_token_out != 0)
        *busy_token_out = 0u;
    if (identity != STOCK_SYNC_IDENTITY_FILE &&
        identity != STOCK_SYNC_IDENTITY_DESCRIPTOR)
        return STOCK_SYNC_ROUTE_REJECT;
    for (retry = 0u; retry != 8u; ++retry) {
        uint32_t observed = stock_sync_load(&control->lifecycle);
        uint32_t phase = stock_sync_phase_of(observed);
        uint32_t desired;
        uint32_t owner;
        uint32_t state;

        if (phase != STOCK_SYNC_IDLE &&
            (observed & STOCK_SYNC_GENERATION_MASK) == 0u)
            return STOCK_SYNC_ROUTE_REJECT;
        if (phase == STOCK_SYNC_GATE_CHECKING ||
            phase == STOCK_SYNC_GATE_READY)
            return STOCK_SYNC_ROUTE_REJECT;
        if (phase == STOCK_SYNC_IDLE) {
            if (!stock_sync_idle_bindings_zero(control))
                return STOCK_SYNC_ROUTE_REJECT;
            if (busy_token_out != 0)
                *busy_token_out = observed;
            return STOCK_SYNC_ROUTE_DELEGATE;
        }
        if (phase == STOCK_SYNC_POISONED)
            return STOCK_SYNC_ROUTE_POISONED;
        owner = stock_sync_load(
            identity == STOCK_SYNC_IDENTITY_DESCRIPTOR ?
                &control->descriptor_key : &control->file_key);
        if (owner != owner_key) {
            if (owner != 0u && busy_token_out != 0)
                *busy_token_out = observed;
            return owner != 0u ? STOCK_SYNC_ROUTE_DELEGATE :
                                 STOCK_SYNC_ROUTE_REJECT;
        }
        if (phase != STOCK_SYNC_ACTIVE)
            return STOCK_SYNC_ROUTE_REJECT;
        desired = stock_sync_with_phase(observed, STOCK_SYNC_ACTIVE_BUSY);
        if (!stock_sync_cas(&control->lifecycle, &observed, desired))
            continue;
        owner = stock_sync_load(
            identity == STOCK_SYNC_IDENTITY_DESCRIPTOR ?
                &control->descriptor_key : &control->file_key);
        state = stock_sync_load(&control->dynamic_state);
        if (owner != owner_key || state == 0u) {
            uint32_t held = desired;

            if (state == 0u)
                (void)stock_sync_poison_exact(control, held);
            else
                (void)stock_sync_cas(
                    &control->lifecycle, &held,
                    stock_sync_with_phase(desired, STOCK_SYNC_ACTIVE));
            return state == 0u ? STOCK_SYNC_ROUTE_POISONED :
                                STOCK_SYNC_ROUTE_REJECT;
        }
        if (dynamic_state_out != 0)
            *dynamic_state_out = state;
        if (busy_token_out != 0)
            *busy_token_out = desired;
        return STOCK_SYNC_ROUTE_OWNED;
    }
    return STOCK_SYNC_ROUTE_REJECT;
}

/* Ingress code that already pinned an exact lifecycle with an activity reader
 * may not have a native owner key in its ABI.  This helper claims only that
 * exact nonzero ACTIVE token and publishes ACTIVE_BUSY before exposing the
 * dynamic-state word.  It does not acquire or release the caller's reader. */
static enum stock_sync_route __attribute__((unused))
stock_sync_operation_enter_exact(
    stock_lossless_sync_control *control, uint32_t active_token,
    uint32_t *dynamic_state_out, uint32_t *busy_token_out)
{
    uint32_t activity;
    uint32_t expected = active_token;
    uint32_t busy_token;
    uint32_t state;

    if (dynamic_state_out != 0)
        *dynamic_state_out = 0u;
    if (busy_token_out != 0)
        *busy_token_out = 0u;
    if (stock_sync_phase_of(active_token) != STOCK_SYNC_ACTIVE ||
        (active_token & STOCK_SYNC_GENERATION_MASK) == 0u)
        return STOCK_SYNC_ROUTE_REJECT;
    activity = stock_sync_load(stock_sync_gate_activity());
    if ((activity & STOCK_SYNC_GATE_WRITER) != 0u ||
        (activity & STOCK_SYNC_GATE_READERS) == 0u)
        return STOCK_SYNC_ROUTE_REJECT;
    busy_token = stock_sync_with_phase(
        active_token, STOCK_SYNC_ACTIVE_BUSY);
    if (!stock_sync_cas(&control->lifecycle, &expected, busy_token))
        return STOCK_SYNC_ROUTE_REJECT;
    state = stock_sync_load(&control->dynamic_state);
    if (state == 0u) {
        (void)stock_sync_poison_exact(control, busy_token);
        return STOCK_SYNC_ROUTE_POISONED;
    }
    if (dynamic_state_out != 0)
        *dynamic_state_out = state;
    if (busy_token_out != 0)
        *busy_token_out = busy_token;
    return STOCK_SYNC_ROUTE_OWNED;
}

static int stock_sync_operation_leave(stock_lossless_sync_control *control,
                                      uint32_t busy_token)
{
    uint32_t expected = busy_token;

    if (stock_sync_phase_of(busy_token) != STOCK_SYNC_ACTIVE_BUSY ||
        (busy_token & STOCK_SYNC_GENERATION_MASK) == 0u)
        return 0;
    return stock_sync_cas(
        &control->lifecycle, &expected,
        stock_sync_with_phase(busy_token, STOCK_SYNC_ACTIVE));
}

/* A blocking native send is a distinct same-generation owner phase.  Enter
 * and leave accept only an exact, nonzero-generation token so neither a stale
 * operation nor corrupted generation-zero state can publish callback-visible
 * ownership.  ACTIVE_SEND is deliberately hidden from runtime callbacks. */
static int __attribute__((unused)) stock_sync_active_send_enter(
    stock_lossless_sync_control *control, uint32_t busy_token,
    uint32_t *send_token_out)
{
    uint32_t expected = busy_token;
    uint32_t send_token;

    if (send_token_out != 0)
        *send_token_out = 0u;
    if (stock_sync_phase_of(busy_token) != STOCK_SYNC_ACTIVE_BUSY ||
        (busy_token & STOCK_SYNC_GENERATION_MASK) == 0u)
        return 0;
    send_token = stock_sync_with_phase(busy_token, STOCK_SYNC_ACTIVE_SEND);
    if (!stock_sync_cas(&control->lifecycle, &expected, send_token))
        return 0;
    if (send_token_out != 0)
        *send_token_out = send_token;
    return 1;
}

static int __attribute__((unused)) stock_sync_active_send_leave(
    stock_lossless_sync_control *control, uint32_t send_token,
    uint32_t *busy_token_out)
{
    uint32_t expected = send_token;
    uint32_t busy_token;

    if (busy_token_out != 0)
        *busy_token_out = 0u;
    if (stock_sync_phase_of(send_token) != STOCK_SYNC_ACTIVE_SEND ||
        (send_token & STOCK_SYNC_GENERATION_MASK) == 0u)
        return 0;
    busy_token = stock_sync_with_phase(send_token, STOCK_SYNC_ACTIVE_BUSY);
    if (!stock_sync_cas(&control->lifecycle, &expected, busy_token))
        return 0;
    if (busy_token_out != 0)
        *busy_token_out = busy_token;
    return 1;
}

/* An IDLE or unrelated-owner close delegates with the exact lifecycle token;
 * the adapter's activity reader covers the complete native call.  A matching
 * close during open marks that exact generation for cancellation and returns
 * failure.  A matching close during ACTIVE_BUSY or ACTIVE_SEND rejects without
 * changing the lifecycle; the in-flight owner remains the sole publisher and
 * a later close may retry after it returns to ACTIVE.  In an integrity-gate
 * build, ACTIVE/CLOSE_FAILED can enter CLOSING only after
 * the caller's sole activity reader is atomically upgraded to WRITER.  The
 * writer is downgraded immediately after the exact lifecycle CAS: CLOSING
 * excludes matching state users, while the restored reader spans native close
 * and cleanup and permits unrelated native reentry.  Direct host/probe builds
 * retain their separately compiled lifecycle-only transition. */
static enum stock_sync_route stock_sync_close_enter(
    stock_lossless_sync_control *control, uint32_t file_key,
    uint32_t *closing_token_out)
{
    unsigned retry;

    if (closing_token_out != 0)
        *closing_token_out = 0u;
    if (file_key == 0u)
        return STOCK_SYNC_ROUTE_REJECT;
    for (retry = 0u; retry != 8u; ++retry) {
        uint32_t observed = stock_sync_load(&control->lifecycle);
        uint32_t phase = stock_sync_phase_of(observed);
        uint32_t owner;
        uint32_t desired;
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
        uint32_t descriptor;
        uint32_t state;
        uint32_t slot;
#endif

        if (phase != STOCK_SYNC_IDLE &&
            (observed & STOCK_SYNC_GENERATION_MASK) == 0u)
            return STOCK_SYNC_ROUTE_REJECT;
        if (phase == STOCK_SYNC_GATE_CHECKING ||
            phase == STOCK_SYNC_GATE_READY)
            return STOCK_SYNC_ROUTE_REJECT;
        if (phase == STOCK_SYNC_IDLE) {
            if (!stock_sync_idle_bindings_zero(control))
                return STOCK_SYNC_ROUTE_REJECT;
            if (closing_token_out != 0)
                *closing_token_out = observed;
            return STOCK_SYNC_ROUTE_DELEGATE;
        }
        if (phase == STOCK_SYNC_POISONED)
            return STOCK_SYNC_ROUTE_POISONED;
        if (phase == STOCK_SYNC_CLOSING ||
            phase == STOCK_SYNC_OPEN_CLEANUP ||
            phase == STOCK_SYNC_RETIRING ||
            phase == STOCK_SYNC_CANCEL_PROBING ||
            phase == STOCK_SYNC_CANCEL_OPENING)
            return STOCK_SYNC_ROUTE_REJECT;
        if (phase == STOCK_SYNC_CLAIMED)
            return STOCK_SYNC_ROUTE_REJECT;
        owner = stock_sync_load(&control->file_key);
        if (owner != file_key) {
            if (owner != 0u && closing_token_out != 0)
                *closing_token_out = observed;
            return owner != 0u ? STOCK_SYNC_ROUTE_DELEGATE :
                                 STOCK_SYNC_ROUTE_REJECT;
        }
        if (phase == STOCK_SYNC_PROBING) {
            desired = stock_sync_with_phase(
                observed, STOCK_SYNC_CANCEL_PROBING);
            if (!stock_sync_cas(&control->lifecycle, &observed, desired))
                continue;
            return STOCK_SYNC_ROUTE_REJECT;
        }
        if (phase == STOCK_SYNC_OPENING) {
            desired = stock_sync_with_phase(
                observed, STOCK_SYNC_CANCEL_OPENING);
            if (!stock_sync_cas(&control->lifecycle, &observed, desired))
                continue;
            return STOCK_SYNC_ROUTE_REJECT;
        }
        if (phase == STOCK_SYNC_ACTIVE_BUSY ||
            phase == STOCK_SYNC_ACTIVE_SEND) {
            return STOCK_SYNC_ROUTE_REJECT;
        }
        if (phase != STOCK_SYNC_ACTIVE &&
            phase != STOCK_SYNC_CLOSE_FAILED)
            return STOCK_SYNC_ROUTE_REJECT;
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
        /* Snapshot the complete active binding before promotion, then verify
         * that exact tuple under WRITER.  This detects descriptor/state/slot
         * corruption or lifecycle reuse in the observation-to-upgrade gap. */
        descriptor = stock_sync_load(&control->descriptor_key);
        state = stock_sync_load(&control->dynamic_state);
        slot = stock_sync_load(&control->slot_guard);
        if (descriptor == 0u || state == 0u || slot == 0u)
            return STOCK_SYNC_ROUTE_REJECT;
        if (!stock_sync_gate_reader_upgrade_writer())
            return STOCK_SYNC_ROUTE_REJECT;
        STOCK_SYNC_CLOSE_UPGRADE_TEST_HOOK(control);
        if (stock_sync_load(&control->lifecycle) != observed ||
            stock_sync_load(&control->file_key) != owner ||
            owner != file_key ||
            stock_sync_load(&control->descriptor_key) != descriptor ||
            stock_sync_load(&control->dynamic_state) != state ||
            stock_sync_load(&control->slot_guard) != slot) {
            /* A complete tuple changed while this authenticated close held
             * sole-reader exclusion.  This is corruption, not contention:
             * poison the exact lifecycle we authenticated when possible and
             * deliberately strand WRITER so no callable ACTIVE tuple can be
             * observed even when the lifecycle word itself was replaced. */
            (void)stock_sync_poison_exact(control, observed);
            return STOCK_SYNC_ROUTE_POISONED;
        }
#endif
        desired = stock_sync_with_phase(observed, STOCK_SYNC_CLOSING);
        if (!stock_sync_cas(&control->lifecycle, &observed, desired)) {
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
            /* WRITER excludes a legitimate competing lifecycle transition;
             * a changed CAS operand is therefore another fail-closed tuple
             * violation.  Never republish it behind an ordinary reader. */
            (void)stock_sync_poison_exact(
                control, stock_sync_with_phase(desired, phase));
            return STOCK_SYNC_ROUTE_POISONED;
#else
            continue;
#endif
        }
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
        if (!stock_sync_gate_writer_downgrade_reader()) {
            (void)stock_sync_poison_exact(control, desired);
            return STOCK_SYNC_ROUTE_POISONED;
        }
#endif
        if (closing_token_out != 0)
            *closing_token_out = desired;
        return STOCK_SYNC_ROUTE_OWNED;
    }
    return STOCK_SYNC_ROUTE_REJECT;
}

static int stock_sync_close_failed(stock_lossless_sync_control *control,
                                   uint32_t closing_token)
{
    uint32_t expected = closing_token;

    if (stock_sync_phase_of(closing_token) != STOCK_SYNC_CLOSING)
        return 0;
    return stock_sync_cas(
        &control->lifecycle, &expected,
        stock_sync_with_phase(closing_token, STOCK_SYNC_CLOSE_FAILED));
}

static int stock_sync_close_cleanup_enter(
    stock_lossless_sync_control *control, uint32_t closing_token,
    uint32_t *cleanup_token_out)
{
    uint32_t expected = closing_token;
    uint32_t cleanup = stock_sync_with_phase(
        closing_token, STOCK_SYNC_OPEN_CLEANUP);

    if (cleanup_token_out != 0)
        *cleanup_token_out = 0u;
    if (stock_sync_phase_of(closing_token) != STOCK_SYNC_CLOSING)
        return 0;
    if (!stock_sync_cas(&control->lifecycle, &expected, cleanup))
        return 0;
    if (cleanup_token_out != 0)
        *cleanup_token_out = cleanup;
    return 1;
}

static int stock_sync_cleanup_released(
    stock_lossless_sync_control *control, uint32_t retiring_token)
{
    uint32_t expected = retiring_token;

    if (stock_sync_phase_of(retiring_token) != STOCK_SYNC_RETIRING)
        return 0;
    return stock_sync_cas(
        &control->lifecycle, &expected,
        stock_sync_with_phase(retiring_token, STOCK_SYNC_IDLE));
}

/* Codec close may require the runtime provider, so OPEN_CLEANUP remains
 * callback-visible.  Once close returns, RETIRING hides the runtime pointer
 * before dynamic state is cleared, zeroed, and released. */
static int stock_sync_cleanup_retire(
    stock_lossless_sync_control *control, uint32_t cleanup_token,
    uint32_t *retiring_token_out)
{
    uint32_t expected = cleanup_token;
    uint32_t retiring = stock_sync_with_phase(
        cleanup_token, STOCK_SYNC_RETIRING);

    if (retiring_token_out != 0)
        *retiring_token_out = 0u;
    if (stock_sync_phase_of(cleanup_token) != STOCK_SYNC_OPEN_CLEANUP)
        return 0;
    if (!stock_sync_cas(&control->lifecycle, &expected, retiring))
        return 0;
    /* The successful OPEN_CLEANUP -> RETIRING CAS elects one unique cleanup
     * owner.  Clear all retained keys before that owner can expose IDLE. */
    stock_sync_store(&control->file_key, 0u);
    stock_sync_store(&control->descriptor_key, 0u);
    stock_sync_store(&control->dynamic_state, 0u);
    stock_sync_store(&control->slot_guard, 0u);
    if (retiring_token_out != 0)
        *retiring_token_out = retiring;
    return 1;
}

static int stock_sync_cleanup_poison(
    stock_lossless_sync_control *control, uint32_t cleanup_token)
{
    uint32_t expected = cleanup_token;

    if (stock_sync_phase_of(cleanup_token) != STOCK_SYNC_OPEN_CLEANUP &&
        stock_sync_phase_of(cleanup_token) != STOCK_SYNC_RETIRING)
        return 0;
    return stock_sync_poison_exact(control, expected);
}

#endif
