.global expl
.type expl,@function
expl:
	fldt 8(%rsp)
	mov 16(%rsp), %ax
	or $0x8000, %ax
	sub $0xbfdf, %ax
	cmp $45, %ax
	jbe 2f
	test %ax, %ax
	fld1
	js 1f
	fscale
	fstp %st(1)
	ret
1:	faddp
	ret
2:	fldl2e
	subq $48, %rsp
	fmul %st(1),%st
	fld %st(0)
	fstpt (%rsp)
	fstpt 16(%rsp)
	fstpt 32(%rsp)
	call exp2l@PLT
	fld %st(0)
	fstpt (%rsp)
	cmpw $0x7fff, 8(%rsp)
	je 1f
	fldt 32(%rsp)
	fldt 16(%rsp)
	fld %st(1)
	movq $0x41f0000000100000,%rax
	pushq %rax
	fldl (%rsp)
	fmulp
	fld %st(2)
	fsub %st(1), %st
	faddp
	fld %st(2)
	fsub %st(1), %st
	movq $0x3ff7154765200000,%rax
	pushq %rax
	fldl (%rsp)
	fld %st(2)
	fmul %st(1), %st
	fsubp %st, %st(4)
	fmul %st(1), %st
	faddp %st, %st(3)
	movq $0x3de705fc2f000000,%rax
	pushq %rax
	fldl (%rsp)
	fmul %st, %st(2)
	fmulp %st, %st(1)
	fxch %st(2)
	faddp
	faddp
	movq $0xbfbe,%rax
	pushq %rax
	movq $0x82f0025f2dc582ee,%rax
	pushq %rax
	fldt (%rsp)
	addq $40,%rsp
	fmulp %st, %st(2)
	faddp
	f2xm1
	fmul %st(1), %st
	faddp
1:	addq $48, %rsp
	ret
