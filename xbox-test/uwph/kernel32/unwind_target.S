.text
.globl k32UnwindTarget
.def k32UnwindTarget; .scl 2; .type 32; .endef
.seh_proc k32UnwindTarget
k32UnwindTarget:
    pushq %rbx
    .seh_pushreg %rbx
    subq $48, %rsp
    .seh_stackalloc 48
    .seh_endprologue
    # Incoming RCX selects RtlUnwindEx (1) or RtlUnwind (0).
    movabsq $0x123456789abcdef0, %rbx
    movq %rcx, %r8
    movq %rsp, %rcx
    leaq k32UnwindLanding(%rip), %rdx
    callq k32UnwindNested
    # A conforming unwind transfers directly to the landing address, with
    # ReturnValue in RAX. Returning normally from the nested call is failure.
    movl $99, %eax
k32UnwindLanding:
    movabsq $0x123456789abcdef0, %rdx
    cmpq %rdx, %rbx
    je k32UnwindRestored
    movl $98, %eax
k32UnwindRestored:
    addq $48, %rsp
    popq %rbx
    retq
.seh_endproc
