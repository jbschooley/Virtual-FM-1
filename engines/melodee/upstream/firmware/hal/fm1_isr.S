/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* IRQ entry wrappers: save what a C call may
 * clobber, call the plain C body, restore, csync, rti. Our clang never emits
 * the csync stock ISRs have before rti, so ISRs are not written in C directly. */
	.macro FM1_ISR name, body
	.section .text.\name,"ax",@progbits
	.globl \name
\name:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	call \body
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
	.endm

	FM1_ISR isr_timer5, fm1_timer5_irq
/* Envelope around the C prologue/body/epilogue, including nested TIMER5.
 * Register save/restore and the short timestamp footer remain outside this
 * span; diagnostics add a conservative wrapper/nesting allowance. */
	.section .audio_text,"ax",@progbits
	.globl isr_alnk0
isr_alnk0:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	r0 = 0x10804
	r0 = [r0]
	r1 = fm1_audio_irq_start_ticks
	[r1] = r0
	call fm1_alnk0_irq
	r0 = 0x10804
	r0 = [r0]
	r1 = fm1_audio_irq_start_ticks
	r1 = [r1]
	r0 -= r1
	r1 = fm1_audio_irq_elapsed_ticks
	[r1] = r0
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
