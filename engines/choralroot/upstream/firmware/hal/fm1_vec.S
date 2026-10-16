/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 128 fatal-vector stubs, 6 bytes each: VEC[n] = fm1_fatal_stubs + 6*n.
 * The call leaves rets = stub + 6, so common code recovers n. A fault about the second core resumes
 * (fm1_cpu1.h fm1_c1_fault_c, after Melodee PR 21, Kerem Kilic / Melodee, GPL-3.0). */
	.section .text.fm1vec,"ax",@progbits
	.globl fm1_fatal_stubs
	.balign 2
fm1_fatal_stubs:
	.rept 128
	[--sp] = {rets}
	call fm1_fatal_common
	.endr

	.section .text.fm1vec_common,"ax",@progbits
	.globl fm1_fatal_common
fm1_fatal_common:
	trigger
	[--sp] = {sp, ssp, usp, icfg, psr, rets, retx, rete, reti}
	[--sp] = {r15-r0}
	r0 = sp				/* frame: r0..r15, reti, rete, retx, rets(stub+6), psr, icfg, usp, ssp, sp, rets(orig) */
	call fm1_c1_fault_c		/* fm1_cpu1.h: a fault about core 1 is not a crash; else fm1_fault_c */
	/* fm1_c1_fault_c returns only after a fault about core 1 taken here (core 1 now held): resume as an ISR
	 * wrapper does (after Melodee PR 21, Kerem Kilic / Melodee, GPL-3.0). reti, the interrupted rets and psr move
	 * to the frame's last three words (f[23..25]), where {psr, rets, reti} = [sp++] reads them; it also leaves sp
	 * where the exception found it. */
	r0 = [sp+64]			/* f[16] reti */
	r1 = [sp+100]			/* f[25] the interrupted rets */
	r2 = [sp+80]			/* f[20] psr */
	[sp+92] = r0
	[sp+96] = r1
	[sp+100] = r2
	{r15-r0} = [sp++]
	sp += 28			/* f[16..22] */
	{psr, rets, reti} = [sp++]
	csync
	rti

	/* a build without the second core (CR_CPU1 0): every fault is the crash report */
	.section .text.fm1vec_c1weak,"ax",@progbits
	.weak fm1_c1_fault_c
fm1_c1_fault_c:
	call fm1_fault_c
1:	goto 1b
