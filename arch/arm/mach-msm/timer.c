/*
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/mach/time.h>
#include <asm/hardware/gic.h>
#include <asm/localtimer.h>

#include <mach/msm_iomap.h>
#include <mach/cpu.h>
#include <mach/board.h>

#define TIMER_MATCH_VAL         0x0000
#define TIMER_COUNT_VAL         0x0004
#define TIMER_ENABLE            0x0008
#define TIMER_ENABLE_CLR_ON_MATCH_EN    BIT(1)
#define TIMER_ENABLE_EN                 BIT(0)
#define TIMER_CLEAR             0x000C
#define DGT_CLK_CTL             0x0034
#define DGT_CLK_CTL_DIV_4	0x3

#define GPT_HZ 32768

#define MSM_GLOBAL_TIMER MSM_CLOCK_GPT

/* TODO: Remove these ifdefs */
#if defined(CONFIG_ARCH_QSD8X50)
#define DGT_HZ (19200000 / 4) /* 19.2 MHz / 4 by default */
#define MSM_DGT_SHIFT (0)
#elif defined(CONFIG_ARCH_MSM7X30)
#define DGT_HZ (24576000 / 4) /* 24.576 MHz (LPXO) / 4 by default */
#define MSM_DGT_SHIFT (0)
#elif defined(CONFIG_ARCH_MSM8X60) || defined(CONFIG_ARCH_MSM8960)
#define DGT_HZ (27000000 / 4) /* 27 MHz (PXO) / 4 by default */
#define MSM_DGT_SHIFT (0)
#else
#define DGT_HZ 19200000 /* 19.2 MHz or 600 KHz after shift */
#define MSM_DGT_SHIFT (5)
#endif

struct msm_clock {
	struct clock_event_device   clockevent;
	struct clocksource          clocksource;
	unsigned int		    irq;
	void __iomem                *regbase;
	uint32_t                    freq;
	uint32_t                    shift;
	void __iomem                *global_counter;
	void __iomem                *local_counter;
	union {
		struct clock_event_device		*evt;
		struct clock_event_device __percpu	**percpu_evt;
	};		
};

enum {
	MSM_CLOCK_GPT,
	MSM_CLOCK_DGT,
	NR_TIMERS,
};


static struct msm_clock msm_clocks[];

static struct msm_clock *clockevent_to_clock(struct clock_event_device *evt);

static irqreturn_t msm_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = *(struct clock_event_device **)dev_id;
	if (evt->event_handler == NULL)
		return IRQ_HANDLED;
	/* Stop the timer tick */
	if (evt->mode == CLOCK_EVT_MODE_ONESHOT) {
		struct msm_clock *clock = clockevent_to_clock(evt);
		u32 ctrl = readl_relaxed(clock->regbase + TIMER_ENABLE);
		ctrl &= ~TIMER_ENABLE_EN;
		writel_relaxed(ctrl, clock->regbase + TIMER_ENABLE);
	}
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static cycle_t msm_read_timer_count(struct clocksource *cs)
{
	struct msm_clock *clk = container_of(cs, struct msm_clock, clocksource);

	/*
	 * Shift timer count down by a constant due to unreliable lower bits
	 * on some targets.
	 */
	return readl(clk->global_counter) >> clk->shift;
}

static struct msm_clock *clockevent_to_clock(struct clock_event_device *evt)
{
#ifdef CONFIG_SMP
	int i;
	for (i = 0; i < NR_TIMERS; i++)
		if (evt == &(msm_clocks[i].clockevent))
			return &msm_clocks[i];
	return &msm_clocks[MSM_GLOBAL_TIMER];
#else
	return container_of(evt, struct msm_clock, clockevent);
#endif
}

static int msm_timer_set_next_event(unsigned long cycles,
				    struct clock_event_device *evt)
{
	struct msm_clock *clock = clockevent_to_clock(evt);
	u32 match = cycles << clock->shift;
	u32 ctrl = readl_relaxed(clock->regbase + TIMER_ENABLE);

	writel_relaxed(0, clock->regbase + TIMER_CLEAR);
	writel_relaxed(match, clock->regbase + TIMER_MATCH_VAL);
	writel_relaxed(ctrl | TIMER_ENABLE_EN, clock->regbase + TIMER_ENABLE);
	return 0;
}

static void msm_timer_set_mode(enum clock_event_mode mode,
			      struct clock_event_device *evt)
{
	struct msm_clock *clock = clockevent_to_clock(evt);
	u32 ctrl;

	ctrl = readl_relaxed(clock->regbase + TIMER_ENABLE);
	ctrl &= ~(TIMER_ENABLE_EN | TIMER_ENABLE_CLR_ON_MATCH_EN);

	switch (mode) {
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* Timer is enabled in set_next_event */
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		break;
	}
	writel_relaxed(ctrl, clock->regbase + TIMER_ENABLE);
}

static struct msm_clock msm_clocks[] = {
	[MSM_CLOCK_GPT] = {
		.clockevent = {
			.name           = "gp_timer",
			.features       = CLOCK_EVT_FEAT_ONESHOT,
			.shift          = 32,
			.rating         = 200,
			.set_next_event = msm_timer_set_next_event,
			.set_mode       = msm_timer_set_mode,
		},
		.irq = INT_GP_TIMER_EXP,
		.freq = GPT_HZ,
	},
	[MSM_CLOCK_DGT] = {
		.clocksource = {
			.name           = "dg_timer",
			.rating         = 300,
			.read           = msm_read_timer_count,
			.mask           = CLOCKSOURCE_MASK((32 - MSM_DGT_SHIFT)),
			.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
		},
		.freq = DGT_HZ >> MSM_DGT_SHIFT,
		.shift = MSM_DGT_SHIFT,
	}
};

static void __init msm_timer_init(void)
{
	struct msm_clock *clock;
	struct clock_event_device *ce = &msm_clocks[MSM_CLOCK_GPT].clockevent;
	struct clocksource *cs = &msm_clocks[MSM_CLOCK_DGT].clocksource;
	int res;
	int global_offset = 0;


	if (cpu_is_msm7x01()) {
		msm_clocks[MSM_CLOCK_GPT].regbase = MSM_CSR_BASE;
		msm_clocks[MSM_CLOCK_DGT].regbase = MSM_CSR_BASE + 0x10;
	} else if (cpu_is_msm7x30()) {
		msm_clocks[MSM_CLOCK_GPT].regbase = MSM_CSR_BASE + 0x04;
		msm_clocks[MSM_CLOCK_DGT].regbase = MSM_CSR_BASE + 0x24;
	} else if (cpu_is_qsd8x50()) {
		msm_clocks[MSM_CLOCK_GPT].regbase = MSM_CSR_BASE;
		msm_clocks[MSM_CLOCK_DGT].regbase = MSM_CSR_BASE + 0x10;
	} else if (cpu_is_msm8x60() || cpu_is_msm8960()) {
		msm_clocks[MSM_CLOCK_GPT].regbase = MSM_TMR_BASE + 0x04;
		msm_clocks[MSM_CLOCK_DGT].regbase = MSM_TMR_BASE + 0x24;

		/* Use CPU0's timer as the global timer. */
		global_offset = MSM_TMR0_BASE - MSM_TMR_BASE;
	} else
		BUG();

#ifdef CONFIG_ARCH_MSM_SCORPIONMP
	writel(DGT_CLK_CTL_DIV_4, MSM_TMR_BASE + DGT_CLK_CTL);
#endif

	clock = &msm_clocks[MSM_CLOCK_GPT];
	clock->local_counter = clock->regbase + TIMER_COUNT_VAL;

	writel_relaxed(0, clock->regbase + TIMER_ENABLE);
	writel_relaxed(0, clock->regbase + TIMER_CLEAR);
	writel_relaxed(~0, clock->regbase + TIMER_MATCH_VAL);
	ce->mult = div_sc(clock->freq, NSEC_PER_SEC, ce->shift);
	/*
	 * allow at least 10 seconds to notice that the timer
	 * wrapped
	 */
	ce->max_delta_ns =
		clockevent_delta2ns(0xf0000000 >> clock->shift, ce);
	/* 4 gets rounded down to 3 */
	ce->min_delta_ns = clockevent_delta2ns(4, ce);
	ce->cpumask = cpumask_of(0);

	ce->irq = clock->irq;
	if (cpu_is_msm8x60() || cpu_is_msm8960()) {
		clock->percpu_evt = alloc_percpu(struct clock_event_device *);
		if (!clock->percpu_evt) {
			pr_err("memory allocation failed for %s\n", ce->name);
			goto err;
		}

		*__this_cpu_ptr(clock->percpu_evt) = ce;
		res = request_percpu_irq(ce->irq, msm_timer_interrupt,
					 ce->name, clock->percpu_evt);
		if (!res)
			enable_percpu_irq(ce->irq, 0);
	} else {
		clock->evt = ce;
		res = request_irq(ce->irq, msm_timer_interrupt,
				  IRQF_TIMER | IRQF_NOBALANCING |
				  IRQF_TRIGGER_RISING, ce->name, &clock->evt);
	}

	if (res)
		pr_err("request_irq failed for %s\n", ce->name);

	clockevents_register_device(ce);
err:
	clock = &msm_clocks[MSM_CLOCK_DGT];
	clock->local_counter = clock->regbase + TIMER_COUNT_VAL;
	clock->global_counter = clock->local_counter + global_offset;
	writel_relaxed(TIMER_ENABLE_EN, clock->regbase + TIMER_ENABLE);
	res = clocksource_register_hz(cs, clock->freq);
	if (res)
		pr_err("clocksource_register failed for %s\n", cs->name);
}

#ifdef CONFIG_LOCAL_TIMERS
int __cpuinit local_timer_setup(struct clock_event_device *evt)
{
	static bool local_timer_inited;
	struct msm_clock *clock = &msm_clocks[MSM_GLOBAL_TIMER];

	/* Use existing clock_event for cpu 0 */
	if (!smp_processor_id())
		return 0;

	if (!local_timer_inited) {
		writel(0, clock->regbase  + TIMER_ENABLE);
		writel(0, clock->regbase + TIMER_CLEAR);
		writel(~0, clock->regbase + TIMER_MATCH_VAL);
		local_timer_inited = true;
	}
	evt->irq = clock->irq;
	evt->name = "local_timer";
	evt->features = CLOCK_EVT_FEAT_ONESHOT;
	evt->rating = clock->clockevent.rating;
	evt->set_mode = msm_timer_set_mode;
	evt->set_next_event = msm_timer_set_next_event;
	evt->shift = clock->clockevent.shift;
	evt->mult = div_sc(clock->freq, NSEC_PER_SEC, evt->shift);
	evt->max_delta_ns =
		clockevent_delta2ns(0xf0000000 >> clock->shift, evt);
	evt->min_delta_ns = clockevent_delta2ns(4, evt);

	*__this_cpu_ptr(clock->percpu_evt) = evt;
	enable_percpu_irq(evt->irq, 0);

	clockevents_register_device(evt);
	return 0;
}

void local_timer_stop(struct clock_event_device *evt)
{
	evt->set_mode(CLOCK_EVT_MODE_UNUSED, evt);
	disable_percpu_irq(evt->irq);
}
#endif /* CONFIG_LOCAL_TIMERS */

struct sys_timer msm_timer = {
	.init = msm_timer_init
};
