/* Private off-device candidate. Keep the dispatch reader through all parser
 * and source-output work, release it immediately before publishing the reply.
 * The remainder executes only fixed cave and native code, never payload code. */
    .section .text,"ax",@progbits
    .align 2
    .global early_reply
early_reply:
    sts     pr,r0
    mov.l   .Lnative_return,r1
    cmp/eq  r1,r0
    bf      .Lnative_send
    /* Native dispatch's four-byte reply local is at SP; saved PR is SP+4. */
    mov.l   @(4,r15),r0
    mov.l   .Lgate_return,r1
    cmp/eq  r1,r0
    bf      .Lnative_send

    mov.l   r4,@-r15
    mov.l   r5,@-r15
    mov.l   r6,@-r15
    mov.l   r7,@-r15
    sts.l   pr,@-r15
    mov.l   .Lreader_release,r1
    jsr     @r1
    nop
    bf      .Lrestore
    /* Five saved words move the caller's saved PR from SP+4 to SP+24.
     * Redirect only after successful release; the normal return remains
     * intact if the release helper rejects its activity word. */
    mov.l   .Lreleased_return,r0
    mov.l   r0,@(24,r15)
.Lrestore:
    lds.l   @r15+,pr
    mov.l   @r15+,r7
    mov.l   @r15+,r6
    mov.l   @r15+,r5
    mov.l   @r15+,r4
.Lnative_send:
    mov.l   .Lsend,r0
    jmp     @r0
    nop
    .align 2
.Lnative_return:   .long 0x08C01F26
.Lgate_return:     .long 0x080FBA9C
.Lreader_release:  .long 0x080FBAAC
.Lreleased_return: .long 0x080FBAA6
.Lsend:            .long 0x08E01AC0
    .section .note.GNU-stack,"",@progbits
