/*
 * Checksum-covered integrity and lifecycle gate for the independently built
 * XDJ-700 lossless payload.
 *
 * The dispatch entry acquires WRITER, then atomically advances stable IDLE to
 * GATE_CHECKING before reading a payload byte.  A successful verifier publishes
 * that exact generation as GATE_READY for stock_lossless_adapter_dispatch() to
 * adopt as CLAIMED.  CRC failure clears the validity commit before returning
 * the generation to IDLE.
 *
 * Every non-dispatch entry composes with the same lifecycle word.
 * GATE_CHECKING and the writer-held GATE_READY handoff are quarantined.  Every
 * other non-IDLE phase reaches the
 * checked payload only with the exact four-word validity record; an absent,
 * torn, or stale record is quarantined with a function-specific failure and
 * never falls through to stock.  Stable IDLE retains the exact stock fallback.
 */

#ifndef XDJ700_GATE_PAYLOAD_BYTES
#error "XDJ700_GATE_PAYLOAD_BYTES is required"
#endif
#ifndef XDJ700_GATE_PAYLOAD_CRC32
#error "XDJ700_GATE_PAYLOAD_CRC32 is required"
#endif
#ifndef XDJ700_GATE_PAYLOAD_READ_VMA
#error "XDJ700_GATE_PAYLOAD_READ_VMA is required"
#endif
#ifndef XDJ700_GATE_DISPATCH_TARGET
#error "XDJ700_GATE_DISPATCH_TARGET is required"
#endif
#ifndef XDJ700_GATE_READ_TARGET
#error "XDJ700_GATE_READ_TARGET is required"
#endif
#ifndef XDJ700_GATE_SIZE_TARGET
#error "XDJ700_GATE_SIZE_TARGET is required"
#endif
#ifndef XDJ700_GATE_POSITION_TARGET
#error "XDJ700_GATE_POSITION_TARGET is required"
#endif
#ifndef XDJ700_GATE_SEEK_TARGET
#error "XDJ700_GATE_SEEK_TARGET is required"
#endif
#ifndef XDJ700_GATE_EOF_TARGET
#error "XDJ700_GATE_EOF_TARGET is required"
#endif
#ifndef XDJ700_GATE_TRANSFER_TARGET
#error "XDJ700_GATE_TRANSFER_TARGET is required"
#endif
#ifndef XDJ700_GATE_CLOSE_TARGET
#error "XDJ700_GATE_CLOSE_TARGET is required"
#endif
#ifndef XDJ700_GATE_ENABLE_ALAC
#error "XDJ700_GATE_ENABLE_ALAC is required"
#endif
#ifndef XDJ700_GATE_ALAC_SELECTOR6_ONLY
#define XDJ700_GATE_ALAC_SELECTOR6_ONLY 0
#endif
#if XDJ700_GATE_ALAC_SELECTOR6_ONLY != 0 && \
    XDJ700_GATE_ALAC_SELECTOR6_ONLY != 1
#error "selector-6-only ALAC policy must be zero or one"
#endif
#if XDJ700_GATE_ALAC_SELECTOR6_ONLY && !XDJ700_GATE_ENABLE_ALAC
#error "selector-6-only ALAC policy requires ALAC mode"
#endif
#ifndef XDJ700_GATE_REQUIRE_DIAGNOSTIC_ISLAND_ZERO
#error "XDJ700_GATE_REQUIRE_DIAGNOSTIC_ISLAND_ZERO is required"
#endif
#if XDJ700_GATE_REQUIRE_DIAGNOSTIC_ISLAND_ZERO != 0 && \
    XDJ700_GATE_REQUIRE_DIAGNOSTIC_ISLAND_ZERO != 1
#error "XDJ700_GATE_REQUIRE_DIAGNOSTIC_ISLAND_ZERO must be 0 or 1"
#endif
#ifndef XDJ700_GATE_USE_EXTERNAL_READER_RELEASE
#define XDJ700_GATE_USE_EXTERNAL_READER_RELEASE 0
#endif
#if XDJ700_GATE_USE_EXTERNAL_READER_RELEASE != 0 && \
    XDJ700_GATE_USE_EXTERNAL_READER_RELEASE != 1
#error "XDJ700_GATE_USE_EXTERNAL_READER_RELEASE must be 0 or 1"
#endif
#if XDJ700_GATE_USE_EXTERNAL_READER_RELEASE && \
    !defined(XDJ700_GATE_EXTERNAL_READER_RELEASE_TARGET)
#error "external reader-release target is required"
#endif
#ifndef XDJ700_GATE_ENABLE_CACHE_REPAIR
#define XDJ700_GATE_ENABLE_CACHE_REPAIR 0
#endif
#if XDJ700_GATE_ENABLE_CACHE_REPAIR != 0 && \
    XDJ700_GATE_ENABLE_CACHE_REPAIR != 1
#error "cache repair must be zero or one"
#endif
#ifndef XDJ700_GATE_CACHE_LOSSLESS_ONLY
#define XDJ700_GATE_CACHE_LOSSLESS_ONLY 0
#endif
#if XDJ700_GATE_CACHE_LOSSLESS_ONLY != 0 && \
    XDJ700_GATE_CACHE_LOSSLESS_ONLY != 1
#error "narrow cache policy must be zero or one"
#endif
#if XDJ700_GATE_CACHE_LOSSLESS_ONLY && !XDJ700_GATE_ENABLE_ALAC
#error "narrow cache policy requires ALAC mode"
#endif
#if XDJ700_GATE_ALAC_SELECTOR6_ONLY && XDJ700_GATE_ENABLE_CACHE_REPAIR && \
    !XDJ700_GATE_CACHE_LOSSLESS_ONLY
#error "selector-6-only admission requires selector-5/6 cache repair"
#endif

#define GATE_STATE_P2          0xA80FBAEC
#define GATE_ACTIVITY_P2       0xA80FBAFC
#define GATE_CONTROL_P2        0xA8C01E84
#define GATE_PHASE_MASK        0x0000000F
#define GATE_GENERATION_MASK   0xFFFFFFF0
#define GATE_GENERATION_STEP   0x00000010
#define GATE_PHASE_READY       9
#define GATE_PHASE_CHECKING    15
#define GATE_ACTIVITY_WRITER   0x80000000
#define GATE_VALID_MAGIC       0x58374731
#define GATE_VALID_MAGIC_INV   0xA7C8B8CE
#define GATE_DIAGNOSTIC_ISLAND_P2 0xA80FD2F0

#if XDJ700_GATE_ENABLE_CACHE_REPAIR
    /*
     * The native source producer normally returns a cached parser context at
     * 08BFFE72 when its opaque selection key matches.  A lossless session has
     * temporarily made that context describe WAVE; after close the restored
     * selector identifies it as ours, but a cache hit would bypass dispatch
     * and replay the stale WAVE interpretation.  The stock primitive at
     * 08C02F8A removes newest records until it finds the byte-sized source key
     * passed in r5.  Every stock caller zero-extends that key, so -1 is the
     * native no-match sentinel: it retires the inactive cache before the
     * untouched cache-miss path allocates a fresh record.  This also leaves
     * the ring count bounded across repeated loads.  Active sessions still
     * carry selector 11 and retain the exact stock hit path.  The opt-in
     * narrow policy retires only 5/6, preserving stock AAC 2/3/4 cache hits.
     * It is valid only with a paired dispatcher/adapter policy that excludes
     * ALAC 2/3/4; this assembly switch alone is not a release candidate.
     *
     * Entry registers are the stock values after 08BFFE70:
     *   r2 cached key, r6 requested key, r5 cached record, r10 request.
     */
    .section .xdj700_lossless_cache_gate,"ax",@progbits
    .align 2
    .global xdj700_lossless_cache_gate
    .type xdj700_lossless_cache_gate,@function
xdj700_lossless_cache_gate:
    .space 4,0 /* owner-derived cache entry filled at template build */
    mov.l   @(36,r5),r0
#if XDJ700_GATE_ENABLE_ALAC
#if XDJ700_GATE_CACHE_LOSSLESS_ONLY
    cmp/eq  #5,r0
    bt      .Lcache_retire
    cmp/eq  #6,r0
    bf      .Lcache_hit
#else
    add     #-2,r0
    mov     #4,r1
    cmp/hi  r1,r0
    bt      .Lcache_hit
#endif
#else
    cmp/eq  #5,r0
    bf      .Lcache_hit
#endif
.Lcache_retire:
    sts.l   pr,@-r15
    mov     #-1,r5
    mov.l   .Lcache_manager,r4
    mov.l   .Lcache_remove,r1
    jsr     @r1
    nop
    lds.l   @r15+,pr
.Lcache_miss:
    mov.l   .Lcache_miss_resume,r1
    jmp     @r1
    nop
.Lcache_hit:
    .space 4,0 /* owner-derived cache hit body filled at template build */
    .space 2,0 /* owner-derived cache hit return filled at template build */
    mov.l   .Lcache_hit_resume,r1
    jmp     @r1
    nop
    .align 2
.Lcache_manager:
    .long   0x09D28C88
.Lcache_remove:
    .long   0x08C02F8A
.Lcache_miss_resume:
    .long   0x08BFFE7E
.Lcache_hit_resume:
    .long   0x08C0004E
    .size xdj700_lossless_cache_gate,.-xdj700_lossless_cache_gate
#endif

    .section .text.xdj700_lossless_gate,"ax",@progbits
    .align 2
    .global xdj700_lossless_gate_dispatch
    .type xdj700_lossless_gate_dispatch,@function
xdj700_lossless_gate_dispatch:
    /* r0-r3 are ABI scratch at this two-argument function entry.  Preserve
     * only the live manager argument while the verifier uses r4 for CRC. */
    mov.l   r4,@-r15

    /* Only selectors the installed payload can classify participate in the
     * gate protocol.  All other selectors take the byte-exact stock path. */
    /* The stock entry itself dereferences request+4 and source+8.  The
     * adapter revalidates manager/request/source after it adopts our token. */
    mov.l   @(4,r5),r1
    mov.l   @(8,r1),r0
#if XDJ700_GATE_ENABLE_ALAC
#if XDJ700_GATE_ALAC_SELECTOR6_ONLY
    cmp/eq  #5,r0
    bt      .Lgate_dispatch_candidate
    cmp/eq  #6,r0
    bt      .Lgate_dispatch_candidate
#else
    add     #-2,r0
    mov     #4,r1
    cmp/hi  r1,r0
    bf      .Lgate_dispatch_candidate
#endif
#else
    cmp/eq  #5,r0
    bt      .Lgate_dispatch_candidate
#endif
    bra     .Lgate_dispatch_stock
    nop

.Lgate_dispatch_candidate:
    /* Take the activity writer before changing IDLE.  An outer/nested stock
     * reader therefore makes this dispatch lose without ever publishing
     * GATE_CHECKING into the reader's native call chain. */
    mov.l   .Ldispatch_activity,r1
    movli.l @r1,r0
    tst     r0,r0
    bf      .Lgate_dispatch_stock
    mov.l   .Lactivity_writer,r0
    movco.l r0,@r1
    bf      .Lgate_dispatch_stock
    .global xdj700_lossless_gate_writer_acquired
xdj700_lossless_gate_writer_acquired:
    synco

    /* WRITER stabilizes activity while the satellite verifies that IDLE has
     * no retained control bindings (and, in the final integration, no live
     * diagnostic-island claim).  Dirty state cannot advance to CHECKING. */
    sts.l   pr,@-r15
    mov.l   .Ldispatch_idle_clean,r0
    jsr     @r0
    nop
    lds.l   @r15+,pr
    bf      .Lgate_claim_failed

    /* Preclaim a new generation with one SH-4A linked-store transaction. */
    mov.l   .Ldispatch_control,r1
    movli.l @r1,r0
    mov     r0,r2
    tst     #GATE_PHASE_MASK,r0
    bf      .Lgate_claim_failed
    mov.l   .Lgeneration_mask,r3
    cmp/eq  r3,r2
    bt      .Lgate_claim_failed
    mov     r2,r0
    add     #GATE_GENERATION_STEP,r0
    or      #GATE_PHASE_CHECKING,r0
    mov     r0,r2
    .global xdj700_lossless_gate_lifecycle_store
xdj700_lossless_gate_lifecycle_store:
    movco.l r0,@r1
    bf      .Lgate_claim_failed
    synco
    mov.l   r2,@-r15

    /* Do not invalidate a previously committed record while the payload is
     * checked.  GATE_CHECKING itself excludes every wrapper. */
    mov.l   .Lpayload_base,r1
    mov.l   .Lhook_bytes,r2
    mov     #-1,r3
    mov.l   .Lcrc32_polynomial,r4
.Lgate_crc_byte:
    mov.b   @r1+,r0
    extu.b  r0,r0
    xor     r0,r3
    mov     #8,r0
.Lgate_crc_bit:
    shlr    r3
    bf      .Lgate_crc_no_xor
    xor     r4,r3
.Lgate_crc_no_xor:
    dt      r0
    bf      .Lgate_crc_bit
    dt      r2
    bf      .Lgate_crc_byte
    not     r3,r3
    mov.l   .Lhook_crc,r0
    cmp/eq  r0,r3
    bf      .Lgate_crc_bad

    /* CRC success: metadata first, commit last.  Only then advance this exact
     * generation from CHECKING to READY; production adapter ingress accepts
     * READY+WRITER and nothing earlier. */
    mov.l   .Lhook_state,r4
    .global xdj700_lossless_gate_commit_crc
xdj700_lossless_gate_commit_crc:
    mov.l   r3,@(8,r4)
    mov.l   .Lhook_bytes,r0
    .global xdj700_lossless_gate_commit_bytes
xdj700_lossless_gate_commit_bytes:
    mov.l   r0,@(12,r4)
    mov.l   .Lvalid_magic_inv,r0
    .global xdj700_lossless_gate_commit_magic_inv
xdj700_lossless_gate_commit_magic_inv:
    mov.l   r0,@(4,r4)
    synco
    mov.l   .Lhook_magic,r0
    .global xdj700_lossless_gate_commit_magic
xdj700_lossless_gate_commit_magic:
    mov.l   r0,@r4
    synco
    mov.l   @r15,r2
    mov.l   .Ldispatch_control,r1
.Lgate_publish_ready:
    movli.l @r1,r0
    cmp/eq  r2,r0
    bf      .Lgate_crc_bad
    add     #(GATE_PHASE_READY-GATE_PHASE_CHECKING),r0
    .global xdj700_lossless_gate_ready_store
xdj700_lossless_gate_ready_store:
    movco.l r0,@r1
    bf      .Lgate_publish_ready
    synco
    add     #4,r15
    mov.l   @r15+,r4
    /* The adapter atomically downgrades WRITER to one READER.  Route its
     * eventual stock return through the same release veneer as native gate
     * fallbacks, so CRC never overlaps adapter work or its continuation. */
    sts.l   pr,@-r15
    mova    xdj700_lossless_gate_dispatch_return,r0
    lds     r0,pr
    mov.l   .Ldispatch_target,r0
    .global xdj700_lossless_gate_dispatch_payload_jump
xdj700_lossless_gate_dispatch_payload_jump:
    jmp     @r0
    nop

.Lgate_crc_bad:
    /* Commit-clear precedes the exact-generation release.  If ownership was
     * unexpectedly lost, quarantine rather than entering stock ambiguously. */
    mov.l   .Lhook_state,r4
    mov     #0,r0
    mov.l   r0,@r4
    synco
    mov.l   @r15+,r2
    mov.l   .Ldispatch_control,r1
.Lgate_crc_release_lifecycle:
    movli.l @r1,r0
    cmp/eq  r2,r0
    bf      .Lgate_dispatch_quarantine
    mov     r2,r0
    add     #-GATE_PHASE_CHECKING,r0
    movco.l r0,@r1
    bf      .Lgate_crc_release_lifecycle
    synco

.Lgate_claim_failed:
    /* Claim failure never changed lifecycle; CRC failure has already retired
     * its exact CHECKING token.  Both paths now retire the exact writer. */
    mov.l   .Ldispatch_activity,r1
.Lgate_claim_release_writer:
    movli.l @r1,r0
    mov.l   .Lactivity_writer,r2
    cmp/eq  r2,r0
    bf      .Lgate_dispatch_quarantine
    mov     #0,r0
    movco.l r0,@r1
    bf      .Lgate_claim_release_writer
    synco
    bra     .Lgate_dispatch_stock
    nop

.Lgate_dispatch_quarantine:
    /* A competing dispatch or ambiguous lifecycle is dropped as a void call;
    * it never executes either unchecked tail bytes or the stock candidate. */
    mov.l   @r15+,r4
    rts
    nop

    /* Exact relocation of the sixteen XDJ-700 v1.15 instructions displaced
     * at 0x08C01E78..0x08C01E97. */
.Lgate_dispatch_stock:
    /* Non-candidate/CRC-failure stock dispatch is covered by the same reader
     * side of the activity lock as every other stock fallback. */
    mov.l   .Ldispatch_control,r1
    mov.l   @r1,r2
    mov     r2,r0
    tst     #GATE_PHASE_MASK,r0
    bf      .Lgate_dispatch_quarantine
    sts.l   pr,@-r15
    mov.l   .Ldispatch_reader_try,r0
    jsr     @r0
    nop
    lds.l   @r15+,pr
    bf      .Lgate_dispatch_quarantine

    mov.l   @r15+,r4
    /* Preserve the original caller PR below the exact relocated stock frame.
     * The stock epilogue returns through our reader-release veneer. */
    sts.l   pr,@-r15
    mova    xdj700_lossless_gate_dispatch_return,r0
    lds     r0,pr
    .space 32,0 /* owner-derived dispatch frame filled at template build */
    mov.l   .Ldispatch_selector11_resume,r1
    jmp     @r1
    nop
.Lgate_dispatch_other:
    mov.l   .Ldispatch_other_resume,r1
    jmp     @r1
    nop

    .align 2
.Lactivity_writer:               .long GATE_ACTIVITY_WRITER
.Lgeneration_mask:               .long GATE_GENERATION_MASK
.Lpayload_base:                  .long XDJ700_GATE_PAYLOAD_READ_VMA
.Lcrc32_polynomial:              .long 0xEDB88320
.Lvalid_magic_inv:               .long GATE_VALID_MAGIC_INV
.Ldispatch_target:               .long XDJ700_GATE_DISPATCH_TARGET
.Ldispatch_selector11_resume:    .long 0x08C01E98
.Ldispatch_other_resume:         .long 0x08C01EA0
.Ldispatch_activity:             .long GATE_ACTIVITY_P2
.Ldispatch_control:              .long GATE_CONTROL_P2
.Ldispatch_reader_try:           .long xdj700_lossless_gate_reader_try
.Ldispatch_idle_clean:           .long xdj700_lossless_gate_idle_clean
    .size xdj700_lossless_gate_dispatch,.-xdj700_lossless_gate_dispatch

    /* Seven compact entry stubs preserve r0 and identify the exact ABI. */
    .macro GATE_WRAPPER name, index
    .section .text.xdj700_lossless_gate,"ax",@progbits
    .align 1
    .global xdj700_lossless_gate_\name
    .type xdj700_lossless_gate_\name,@function
xdj700_lossless_gate_\name:
    bra     xdj700_lossless_gate_hook_common
    mov     #\index,r0
    .size xdj700_lossless_gate_\name,.-xdj700_lossless_gate_\name
    .endm

    GATE_WRAPPER size,     0
    GATE_WRAPPER position, 1
    GATE_WRAPPER seek,     2
    GATE_WRAPPER eof,      3
    GATE_WRAPPER transfer, 4
    GATE_WRAPPER close,    5
    GATE_WRAPPER read,     6

    /* Stock-read continuation entered while the gate holds an activity
     * reader.  These are the exact displaced six saves. */
    .section .text.xdj700_lossless_gate,"ax",@progbits
    .align 2
.Lread_stock_tail:
    .global xdj700_lossless_gate_read_stock_tail
xdj700_lossless_gate_read_stock_tail:
    .space 12,0 /* owner-derived read prologue filled at template build */
    mov.l   .Lread_resume,r0
    jmp     @r0
    nop
    .align 2
.Lread_resume:                    .long 0x08C5636A

    .section .xdj700_lossless_gate_state,"aw",@progbits
    .align 2
    .global xdj700_lossless_gate_state
xdj700_lossless_gate_state:
    /* Four-word validity record followed by the independent activity word. */
    .long 0, 0, 0, 0, 0

    .section .text.xdj700_lossless_gate,"ax",@progbits
    .align 2
    .global xdj700_lossless_gate_hook_common
    .type xdj700_lossless_gate_hook_common,@function
xdj700_lossless_gate_hook_common:
    /* r0-r3 are caller-clobbered at each function entry. */
    mov     r0,r3

    /* Snapshot the lifecycle before validating the record. */
    mov.l   .Lhook_control,r1
    mov.l   @r1,r2
    mov.l   r2,@-r15
    mov.l   r3,@-r15
    mov     r2,r0
    and     #GATE_PHASE_MASK,r0
    tst     r0,r0
    bt      .Lhook_stock
    /* Every published owner phase must carry a nonzero generation.  For a
     * non-IDLE phase, equality of the full token and its low-nibble phase is
     * exactly the corrupted generation-zero case. */
    cmp/eq  r2,r0
    bt      .Lhook_quarantine
    cmp/eq  #GATE_PHASE_CHECKING,r0
    bt      .Lhook_quarantine
    cmp/eq  #GATE_PHASE_READY,r0
    bt      .Lhook_quarantine

    /* Every other non-IDLE phase requires the exact committed payload. */
    mov.l   .Lhook_state,r1
    mov.l   .Lhook_magic,r2
    mov.l   @r1,r0
    cmp/eq  r2,r0
    bf      .Lhook_quarantine
    not     r2,r2
    mov.l   @(4,r1),r0
    cmp/eq  r2,r0
    bf      .Lhook_quarantine
    mov.l   .Lhook_crc,r2
    mov.l   @(8,r1),r0
    cmp/eq  r2,r0
    bf      .Lhook_quarantine
    mov.l   .Lhook_bytes,r2
    mov.l   @(12,r1),r0
    cmp/eq  r2,r0
    bf      .Lhook_quarantine

    /* Pin this exact non-IDLE lifecycle against a new checksum writer before
     * crossing into the payload.  A close may still retire its owner, but a
     * subsequent dispatch cannot mutate/replace payload bytes until this
     * wrapper returns and releases its reader. */
    mov.l   @(4,r15),r2
    sts.l   pr,@-r15
    mov.l   .Lhook_reader_try,r0
    jsr     @r0
    nop
    lds.l   @r15+,pr
    bf      .Lhook_quarantine
    shll2   r3
    mova    .Ltail_targets,r0
    mov.l   @(r0,r3),r0
    bra     .Lhook_call
    nop

.Lhook_stock:
    /* Acquire a reader and recheck the exact full IDLE generation before the
    * stock call.  reader_try retires its count itself on a failed recheck. */
    sts.l   pr,@-r15
    mov.l   .Lhook_reader_try,r0
    jsr     @r0
    nop
    lds.l   @r15+,pr
    bf      .Lhook_quarantine
    shll2   r3
    mova    .Lnative_targets,r0
    mov.l   @(r0,r3),r0
.Lhook_call:
    /* Preserve the caller PR and duplicate stack argument five so both read
     * and transfer see their exact original ABI. */
    add     #8,r15
    sts.l   pr,@-r15
    mov.l   @(4,r15),r1
    mov.l   r1,@-r15
    .global xdj700_lossless_gate_hook_impl_call
xdj700_lossless_gate_hook_impl_call:
    jsr     @r0
    nop
    .global xdj700_lossless_gate_hook_impl_return
xdj700_lossless_gate_hook_impl_return:
    add     #4,r15
    mov.l   r0,@-r15
    mov.l   .Lhook_reader_release,r1
    jsr     @r1
    nop
    mov.l   @r15+,r0
    lds.l   @r15+,pr
    rts
    nop

.Lhook_quarantine:
    /* size and read promise no bytes on rejection. */
    mov.l   @r15,r3
    mov     r3,r0
    tst     r0,r0
    bf      .Lhook_not_size
    tst     r5,r5
    bt      .Lhook_positive_failure
    mov     #0,r0
    mov.l   r0,@r5
.Lhook_not_size:
    cmp/eq  #6,r0
    bf      .Lhook_result
    mov.l   @(8,r15),r1
    tst     r1,r1
    bt      .Lhook_positive_failure
    mov     #0,r0
    mov.l   r0,@r1
.Lhook_result:
.Lhook_positive_failure:
    mova    .Lhook_results,r0
    mov.b   @(r0,r3),r0
    exts.b  r0,r0
    add     #8,r15
    rts
    nop

    .align 2
.Lhook_results:
    .byte   1, -1, 1, 0, -1, -1, 1, 0

    .align 2
.Lhook_state:                    .long GATE_STATE_P2
.Lhook_magic:                    .long GATE_VALID_MAGIC
.Lhook_crc:                      .long XDJ700_GATE_PAYLOAD_CRC32
.Lhook_bytes:                    .long XDJ700_GATE_PAYLOAD_BYTES
.Lhook_control:                  .long GATE_CONTROL_P2
.Lhook_reader_try:               .long xdj700_lossless_gate_reader_try
#if XDJ700_GATE_USE_EXTERNAL_READER_RELEASE
.Lhook_reader_release:           .long XDJ700_GATE_EXTERNAL_READER_RELEASE_TARGET
#else
.Lhook_reader_release:           .long xdj700_lossless_gate_reader_release
#endif
.Ltail_targets:
    .long XDJ700_GATE_SIZE_TARGET
    .long XDJ700_GATE_POSITION_TARGET
    .long XDJ700_GATE_SEEK_TARGET
    .long XDJ700_GATE_EOF_TARGET
    .long XDJ700_GATE_TRANSFER_TARGET
    .long XDJ700_GATE_CLOSE_TARGET
    .long XDJ700_GATE_READ_TARGET
.Lnative_targets:
    .long 0x08DE0E7A
    .long 0x08DE0364
    .long 0x08DE00D6
    .long 0x08DE0510
    .long 0x08DDFC96
    .long 0x08DDF992
    .long .Lread_stock_tail
    .size xdj700_lossless_gate_hook_common,.-xdj700_lossless_gate_hook_common

    .align 2
    .global xdj700_lossless_gate_dispatch_return
xdj700_lossless_gate_dispatch_return:
    mov.l   r0,@-r15
    mov.l   .Lreturn_reader_release,r1
    jsr     @r1
    nop
    mov.l   @r15+,r0
    lds.l   @r15+,pr
    rts
    nop

    /* Local standalone release.  Final integrated builds select the fixed
     * D100 release owner through every call-site literal instead. */
    .align 2
    .global xdj700_lossless_gate_reader_release
xdj700_lossless_gate_reader_release:
    mov.l   .Lmain_release_activity,r1
.Lreader_release_retry:
    movli.l @r1,r0
    cmp/pz  r0
    bf      .Lreader_release_bad
    tst     r0,r0
    bt      .Lreader_release_bad
    add     #-1,r0
    movco.l r0,@r1
    bf      .Lreader_release_retry
    synco
    rts
    sett
.Lreader_release_bad:
    rts
    clrt
    .align 2
#if XDJ700_GATE_USE_EXTERNAL_READER_RELEASE
.Lreturn_reader_release:         .long XDJ700_GATE_EXTERNAL_READER_RELEASE_TARGET
#else
.Lreturn_reader_release:         .long xdj700_lossless_gate_reader_release
#endif
.Lmain_release_activity:         .long GATE_ACTIVITY_P2

    /* The independently audited satellite contains only the shared activity
     * reader utilities.  The skipped BB00..BB80 interval remains erased and
     * is neither executed nor read as data. */
    .section .xdj700_lossless_gate_satellite,"ax",@progbits
    /* Input/output r2 is the exact lifecycle snapshot and is preserved on
     * success for the ingress ABI.  r0/r1 are clobbered.  Failure returns T=0
     * after retiring any acquired count and preserves r5, the size-output
     * pointer.  Gate common saves its wrapper index itself; external callers
     * must treat the other caller-saved registers as undefined on failure. */
    .align 2
    .global xdj700_lossless_gate_reader_try
xdj700_lossless_gate_reader_try:
    mov.l   .Lreader_activity,r1
.Lreader_try_retry:
    movli.l @r1,r0
    cmp/pz  r0
    bf      .Lreader_try_fail
    add     #1,r0
    cmp/pz  r0
    bf      .Lreader_try_fail
    movco.l r0,@r1
    bf      .Lreader_try_retry
    .global xdj700_lossless_gate_reader_acquired
xdj700_lossless_gate_reader_acquired:
    synco
    mov.l   .Lreader_control,r1
    mov.l   @r1,r0
    cmp/eq  r2,r0
    bf      .Lreader_try_retire

    /* A stable IDLE token delegates only when every retained binding is zero.
     * The reader already excludes a dispatch writer and the exact lifecycle
     * recheck above prevents publication from racing these loads. */
    mov     r2,r0
    tst     #GATE_PHASE_MASK,r0
    bf      .Lreader_try_good
    mov.l   r2,@-r15
    sts.l   pr,@-r15
    bsr     xdj700_lossless_gate_idle_clean
    nop
    lds.l   @r15+,pr
    mov.l   @r15+,r2
    bt      .Lreader_try_good
.Lreader_try_retire:
    mov.l   r5,@-r15
    sts.l   pr,@-r15
    mov.l   .Lreader_release_target,r0
    jsr     @r0
    nop
    lds.l   @r15+,pr
    mov.l   @r15+,r5
.Lreader_try_fail:
    rts
    clrt
.Lreader_try_good:
    rts
    sett

    /* Pure exact-IDLE state validator, callable either with WRITER held before
     * CHECKING or with a reader held before native fallback.  Return T=1 only
     * when every configured retained word is zero. */
    .align 2
    .global xdj700_lossless_gate_idle_clean
xdj700_lossless_gate_idle_clean:
    mov.l   .Lreader_control,r1
    mov.l   @(4,r1),r0
    mov.l   @(8,r1),r2
    or      r2,r0
    mov.l   @(12,r1),r2
    or      r2,r0
    mov.l   @(16,r1),r2
    or      r2,r0
#if XDJ700_GATE_REQUIRE_DIAGNOSTIC_ISLAND_ZERO
    mov.l   .Lreader_diagnostic_island,r1
    mov.l   @r1,r2
    or      r2,r0
    mov.l   @(4,r1),r2
    or      r2,r0
    mov.l   @(8,r1),r2
    or      r2,r0
    mov.l   @(12,r1),r2
    or      r2,r0
#endif
    tst     r0,r0
    rts
    nop
    .align 2
.Lreader_activity:               .long GATE_ACTIVITY_P2
.Lreader_control:                .long GATE_CONTROL_P2
#if XDJ700_GATE_USE_EXTERNAL_READER_RELEASE
.Lreader_release_target:         .long XDJ700_GATE_EXTERNAL_READER_RELEASE_TARGET
#else
.Lreader_release_target:         .long xdj700_lossless_gate_reader_release
#endif
#if XDJ700_GATE_REQUIRE_DIAGNOSTIC_ISLAND_ZERO
.Lreader_diagnostic_island:      .long GATE_DIAGNOSTIC_ISLAND_P2
#endif

    .section .note.GNU-stack,"",@progbits
