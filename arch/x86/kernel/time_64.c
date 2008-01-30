/*
 *  "High Precision Event Timer" based timekeeping.
 *
 *  Copyright (c) 1991,1992,1995  Linus Torvalds
 *  Copyright (c) 1994  Alan Modra
 *  Copyright (c) 1995  Markus Kuhn
 *  Copyright (c) 1996  Ingo Molnar
 *  Copyright (c) 1998  Andrea Arcangeli
 *  Copyright (c) 2002,2006  Vojtech Pavlik
 *  Copyright (c) 2003  Andi Kleen
 *  RTC support code taken from arch/i386/kernel/timers/time_hpet.c
 */

#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/time.h>

#include <asm/i8253.h>
#include <asm/hpet.h>
#include <asm/nmi.h>
#include <asm/vgtod.h>

volatile unsigned long __jiffies __section_jiffies = INITIAL_JIFFIES;

unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	/* Assume the lock function has either no stack frame or a copy
	   of flags from PUSHF
	   Eflags always has bits 22 and up cleared unlike kernel addresses. */
	if (!user_mode(regs) && in_lock_functions(pc)) {
		unsigned long *sp = (unsigned long *)regs->sp;
		if (sp[0] >> 22)
			return sp[0];
		if (sp[1] >> 22)
			return sp[1];
	}
	return pc;
}
EXPORT_SYMBOL(profile_pc);

static irqreturn_t timer_event_interrupt(int irq, void *dev_id)
{
	add_pda(irq0_irqs, 1);

	global_clock_event->event_handler(global_clock_event);

	return IRQ_HANDLED;
}

/* calibrate_cpu is used on systems with fixed rate TSCs to determine
 * processor frequency */
#define TICK_COUNT 100000000
static unsigned int __init tsc_calibrate_cpu_khz(void)
{
	int tsc_start, tsc_now;
	int i, no_ctr_free;
	unsigned long evntsel3 = 0, pmc3 = 0, pmc_now = 0;
	unsigned long flags;

	for (i = 0; i < 4; i++)
		if (avail_to_resrv_perfctr_nmi_bit(i))
			break;
	no_ctr_free = (i == 4);
	if (no_ctr_free) {
		i = 3;
		rdmsrl(MSR_K7_EVNTSEL3, evntsel3);
		wrmsrl(MSR_K7_EVNTSEL3, 0);
		rdmsrl(MSR_K7_PERFCTR3, pmc3);
	} else {
		reserve_perfctr_nmi(MSR_K7_PERFCTR0 + i);
		reserve_evntsel_nmi(MSR_K7_EVNTSEL0 + i);
	}
	local_irq_save(flags);
	/* start meauring cycles, incrementing from 0 */
	wrmsrl(MSR_K7_PERFCTR0 + i, 0);
	wrmsrl(MSR_K7_EVNTSEL0 + i, 1 << 22 | 3 << 16 | 0x76);
	rdtscl(tsc_start);
	do {
		rdmsrl(MSR_K7_PERFCTR0 + i, pmc_now);
		tsc_now = get_cycles_sync();
	} while ((tsc_now - tsc_start) < TICK_COUNT);

	local_irq_restore(flags);
	if (no_ctr_free) {
		wrmsrl(MSR_K7_EVNTSEL3, 0);
		wrmsrl(MSR_K7_PERFCTR3, pmc3);
		wrmsrl(MSR_K7_EVNTSEL3, evntsel3);
	} else {
		release_perfctr_nmi(MSR_K7_PERFCTR0 + i);
		release_evntsel_nmi(MSR_K7_EVNTSEL0 + i);
	}

	return pmc_now * tsc_khz / (tsc_now - tsc_start);
}

static struct irqaction irq0 = {
	.handler	= timer_event_interrupt,
	.flags		= IRQF_DISABLED | IRQF_IRQPOLL | IRQF_NOBALANCING,
	.mask		= CPU_MASK_NONE,
	.name		= "timer"
};

void __init time_init(void)
{
	if (!hpet_enable())
		setup_pit_timer();

	setup_irq(0, &irq0);

	tsc_calibrate();

	cpu_khz = tsc_khz;
	if (cpu_has(&boot_cpu_data, X86_FEATURE_CONSTANT_TSC) &&
		boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
		boot_cpu_data.x86 == 16)
		cpu_khz = tsc_calibrate_cpu_khz();

	if (unsynchronized_tsc())
		mark_tsc_unstable("TSCs unsynchronized");

	if (cpu_has(&boot_cpu_data, X86_FEATURE_RDTSCP))
		vgetcpu_mode = VGETCPU_RDTSCP;
	else
		vgetcpu_mode = VGETCPU_LSL;

	printk(KERN_INFO "time.c: Detected %d.%03d MHz processor.\n",
		cpu_khz / 1000, cpu_khz % 1000);
	init_tsc_clocksource();
}
