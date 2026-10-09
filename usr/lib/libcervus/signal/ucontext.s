	.text

	.globl getcontext
	.type getcontext, @function
getcontext:
	movq %rbx, 128(%rdi)
	movq %rbp, 120(%rdi)
	movq %r12, 72(%rdi)
	movq %r13, 80(%rdi)
	movq %r14, 88(%rdi)
	movq %r15, 96(%rdi)
	movq %rdi, 104(%rdi)
	movq %rsi, 112(%rdi)
	movq %rdx, 136(%rdi)
	movq %rcx, 152(%rdi)
	movq %r8, 40(%rdi)
	movq %r9, 48(%rdi)
	movq $0, 144(%rdi)
	movq (%rsp), %rcx
	movq %rcx, 168(%rdi)
	leaq 8(%rsp), %rcx
	movq %rcx, 160(%rdi)
	leaq 320(%rdi), %rcx
	movq %rcx, 224(%rdi)
	fnstcw (%rcx)
	stmxcsr 24(%rcx)
	subq $8, %rsp
	leaq 296(%rdi), %rdx
	xorl %esi, %esi
	xorl %edi, %edi
	call sigprocmask@PLT
	addq $8, %rsp
	testl %eax, %eax
	jnz 1f
	ret
1:	movl $-1, %eax
	ret
	.size getcontext, .-getcontext

	.globl setcontext
	.type setcontext, @function
setcontext:
	pushq %rdi
	leaq 296(%rdi), %rsi
	xorl %edx, %edx
	movl $2, %edi
	call sigprocmask@PLT
	popq %rdx
	testl %eax, %eax
	jnz 1f
	jmp __cervus_ctx_load
1:	movl $-1, %eax
	ret
	.size setcontext, .-setcontext

	.globl swapcontext
	.type swapcontext, @function
swapcontext:
	movq %rbx, 128(%rdi)
	movq %rbp, 120(%rdi)
	movq %r12, 72(%rdi)
	movq %r13, 80(%rdi)
	movq %r14, 88(%rdi)
	movq %r15, 96(%rdi)
	movq %rdi, 104(%rdi)
	movq %rsi, 112(%rdi)
	movq %rdx, 136(%rdi)
	movq %rcx, 152(%rdi)
	movq %r8, 40(%rdi)
	movq %r9, 48(%rdi)
	movq $0, 144(%rdi)
	movq (%rsp), %rcx
	movq %rcx, 168(%rdi)
	leaq 8(%rsp), %rcx
	movq %rcx, 160(%rdi)
	leaq 320(%rdi), %rcx
	movq %rcx, 224(%rdi)
	fnstcw (%rcx)
	stmxcsr 24(%rcx)
	pushq %rsi
	leaq 296(%rdi), %rdx
	leaq 296(%rsi), %rsi
	movl $2, %edi
	call sigprocmask@PLT
	popq %rdx
	testl %eax, %eax
	jnz 1f
	jmp __cervus_ctx_load
1:	movl $-1, %eax
	ret
	.size swapcontext, .-swapcontext

	.type __cervus_ctx_load, @function
__cervus_ctx_load:
	movq 224(%rdx), %rcx
	testq %rcx, %rcx
	jz 1f
	fldcw (%rcx)
	ldmxcsr 24(%rcx)
1:	movq 160(%rdx), %rsp
	pushq 168(%rdx)
	movq 128(%rdx), %rbx
	movq 120(%rdx), %rbp
	movq 72(%rdx), %r12
	movq 80(%rdx), %r13
	movq 88(%rdx), %r14
	movq 96(%rdx), %r15
	movq 104(%rdx), %rdi
	movq 112(%rdx), %rsi
	movq 152(%rdx), %rcx
	movq 40(%rdx), %r8
	movq 48(%rdx), %r9
	movq 144(%rdx), %rax
	movq 136(%rdx), %rdx
	ret
	.size __cervus_ctx_load, .-__cervus_ctx_load

	.globl __cervus_ctx_start
	.hidden __cervus_ctx_start
	.type __cervus_ctx_start, @function
__cervus_ctx_start:
	testq %rbx, %rbx
	jz 1f
	movq %rbx, %rdi
	call setcontext@PLT
1:	xorl %edi, %edi
	call exit@PLT
	hlt
	.size __cervus_ctx_start, .-__cervus_ctx_start

	.section .note.GNU-stack,"",@progbits
