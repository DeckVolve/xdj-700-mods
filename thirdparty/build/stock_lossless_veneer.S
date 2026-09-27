/*
 * Far SH-4 trampolines for the XDJ-700 retained lossless adapter.
 *
 * The dispatch trampoline reproduces the exact sixteen instructions displaced
 * at 0x08C01E78..0x08C01E97, invokes the independent content gate with the
 * untouched entry arguments, and resumes at the original selector-11 or
 * other-selector continuation.  The entry overlay reserves
 * 0x08C01E84..0x08C01E97 as a zero-initialized, boot-lifetime control block.
 * The read entry tail-calls the C adapter.  Its fallback trampoline reproduces
 * the six displaced register saves at 0x08C5635E and resumes at 0x08C5636A.
 */

    .section .text.stock_lossless_dispatch_trampoline,"ax",@progbits
    .align 2
    .global stock_lossless_dispatch_trampoline
    .type stock_lossless_dispatch_trampoline,@function
stock_lossless_dispatch_trampoline:
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
#ifndef STOCK_LOSSLESS_GATE_RETURN_PR
#error "STOCK_LOSSLESS_GATE_RETURN_PR is required by production ingress"
#endif
    /* This is the sole production adoption ingress.  Reject an accidental or
     * concurrent direct call before reading request or lifecycle state. */
    sts     pr,r0
    mov.l   .Ldispatch_gate_return,r1
    cmp/eq  r1,r0
    bf      .Ldispatch_untrusted
#endif
    .space 12,0 /* owner-derived dispatch frame filled at template build */

    mov.l   r4,@-r15
    mov.l   r5,@-r15
    mov.l   r6,@-r15
    mov.l   r7,@-r15
    sts     fpscr,r0
    mov.l   r0,@-r15
    mov.l   .Ldispatch_handler,r0
    jsr     @r0
    nop
    .global stock_lossless_dispatch_gate_impl_return
    .hidden stock_lossless_dispatch_gate_impl_return
stock_lossless_dispatch_gate_impl_return:
    mov.l   @r15+,r0
    lds     r0,fpscr
    mov.l   @r15+,r7
    mov.l   @r15+,r6
    mov.l   @r15+,r5
    mov.l   @r15+,r4

    /* Exact displaced XDJ-700 v1.15 instructions from 0x08C01E84..97. */
    .space 20,0 /* owner-derived dispatch control filled at template build */

    /* Preserve r1 while using it for a far jump.  Restoring it in the delay
     * slot leaves the stock continuation with exactly the relocated register
     * state and stack depth. */
    mov.l   r1,@-r15
    mov.l   .Ldispatch_selector11_resume,r1
    jmp     @r1
    mov.l   @r15+,r1

.Ldispatch_other_selector:
    mov.l   r1,@-r15
    mov.l   .Ldispatch_other_resume,r1
    jmp     @r1
    mov.l   @r15+,r1
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE

.Ldispatch_untrusted:
    rts
    nop
#endif
    .align 2
.Ldispatch_handler:
#if STOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE
    .long   stock_lossless_adapter_dispatch_gate_impl
.Ldispatch_gate_return:
    .long   STOCK_LOSSLESS_GATE_RETURN_PR
#else
    .long   stock_lossless_adapter_dispatch
#endif
.Ldispatch_selector11_resume:
    .long   0x08C01E98
.Ldispatch_other_resume:
    .long   0x08C01EA0
    .size stock_lossless_dispatch_trampoline,.-stock_lossless_dispatch_trampoline

    .section .text.stock_lossless_read_entry,"ax",@progbits
    .align 2
    .global stock_lossless_read_entry
    .type stock_lossless_read_entry,@function
stock_lossless_read_entry:
    mov.l   .Lread_handler,r0
    jmp     @r0
    nop
    .align 2
.Lread_handler:
    .long   stock_lossless_adapter_read
    .size stock_lossless_read_entry,.-stock_lossless_read_entry

    .section .text.stock_lossless_read_original,"ax",@progbits
    .align 2
    .global stock_lossless_read_original
    .type stock_lossless_read_original,@function
stock_lossless_read_original:
    .space 12,0 /* owner-derived read prologue filled at template build */
    mov.l   .Lread_resume,r0
    jmp     @r0
    nop
    .align 2
.Lread_resume:
    .long   0x08C5636A
    .size stock_lossless_read_original,.-stock_lossless_read_original

    .section .note.GNU-stack,"",@progbits
