/* linux/arch/arm/mach-exynos4/cpuidle.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/time.h>

#include <asm/proc-fns.h>
#include <asm/smp_scu.h>
#include <asm/suspend.h>
#include <asm/unified.h>
#include <asm/cpuidle.h>
#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/pmu.h>

#include <plat/cpu.h>

#define REG_DIRECTGO_ADDR	(samsung_rev() == EXYNOS4210_REV_1_1 ? \
			S5P_INFORM7 : (samsung_rev() == EXYNOS4210_REV_1_0 ? \
			(S5P_VA_SYSRAM + 0x24) : S5P_INFORM0))
#define REG_DIRECTGO_FLAG	(samsung_rev() == EXYNOS4210_REV_1_1 ? \
			S5P_INFORM6 : (samsung_rev() == EXYNOS4210_REV_1_0 ? \
			(S5P_VA_SYSRAM + 0x20) : S5P_INFORM1))

#define S5P_CHECK_AFTR		0xFCBA0D10

static int exynos4_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index);

static struct cpuidle_state exynos4_cpuidle_set[] __initdata = {
	[0] = ARM_CPUIDLE_WFI_STATE,
	[1] = {
		.enter			= exynos4_enter_lowpower,
		.exit_latency		= 300,
		.target_residency	= 100000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C1",
		.desc			= "ARM power down",
	},
};

static DEFINE_PER_CPU(struct cpuidle_device, exynos4_cpuidle_device);

static struct cpuidle_driver exynos4_idle_driver = {
	.name			= "exynos4_idle",
	.owner			= THIS_MODULE,
	.en_core_tk_irqen	= 1,
};

/* Ext-GIC nIRQ/nFIQ is the only wakeup source in AFTR */
static void exynos4_set_wakeupmask(void)
{
	__raw_writel(0x0000ff3e, S5P_WAKEUP_MASK);
}

static unsigned int g_pwr_ctrl, g_diag_reg;

static void save_cpu_arch_register(void)
{
	/*read power control register*/
	asm("mrc p15, 0, %0, c15, c0, 0" : "=r"(g_pwr_ctrl) : : "cc");
	/*read diagnostic register*/
	asm("mrc p15, 0, %0, c15, c0, 1" : "=r"(g_diag_reg) : : "cc");
	return;
}

static void restore_cpu_arch_register(void)
{
	/*write power control register*/
	asm("mcr p15, 0, %0, c15, c0, 0" : : "r"(g_pwr_ctrl) : "cc");
	/*write diagnostic register*/
	asm("mcr p15, 0, %0, c15, c0, 1" : : "r"(g_diag_reg) : "cc");
	return;
}

static int idle_finisher(unsigned long flags)
{
	cpu_do_idle();
	return 1;
}

static int exynos4_enter_core0_aftr(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	unsigned long tmp;

	exynos4_set_wakeupmask();

	/* Set value of power down register for aftr mode */
	exynos_sys_powerdown_conf(SYS_AFTR);

	__raw_writel(virt_to_phys(s3c_cpu_resume), REG_DIRECTGO_ADDR);
	__raw_writel(S5P_CHECK_AFTR, REG_DIRECTGO_FLAG);

	save_cpu_arch_register();

	/* Setting Central Sequence Register for power down mode */
	tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~S5P_CENTRAL_LOWPWR_CFG;
	__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);

	cpu_pm_enter();
	cpu_suspend(0, idle_finisher);

#ifdef CONFIG_SMP
	scu_enable(S5P_VA_SCU);
#endif
	cpu_pm_exit();

	restore_cpu_arch_register();

	/*
	 * If PMU failed while entering sleep mode, WFI will be
	 * ignored by PMU and then exiting cpu_do_idle().
	 * S5P_CENTRAL_LOWPWR_CFG bit will not be set automatically
	 * in this situation.
	 */
	tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	if (!(tmp & S5P_CENTRAL_LOWPWR_CFG)) {
		tmp |= S5P_CENTRAL_LOWPWR_CFG;
		__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);
	}

	/* Clear wakeup state register */
	__raw_writel(0x0, S5P_WAKEUP_STAT);

	return index;
}

static int exynos4_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int new_index = index;

	/* This mode only can be entered when other core's are offline */
	if (num_online_cpus() > 1)
		new_index = drv->safe_state_index;

	if (new_index == 0)
		return arm_cpuidle_simple_enter(dev, drv, new_index);
	else
		return exynos4_enter_core0_aftr(dev, drv, new_index);
}

static void __init exynos5_core_down_clk(void)
{
	unsigned int tmp;

	/*
	 * Enable arm clock down (in idle) and set arm divider
	 * ratios in WFI/WFE state.
	 */
	tmp = PWR_CTRL1_CORE2_DOWN_RATIO | \
	      PWR_CTRL1_CORE1_DOWN_RATIO | \
	      PWR_CTRL1_DIV2_DOWN_EN	 | \
	      PWR_CTRL1_DIV1_DOWN_EN	 | \
	      PWR_CTRL1_USE_CORE1_WFE	 | \
	      PWR_CTRL1_USE_CORE0_WFE	 | \
	      PWR_CTRL1_USE_CORE1_WFI	 | \
	      PWR_CTRL1_USE_CORE0_WFI;
	__raw_writel(tmp, EXYNOS5_PWR_CTRL1);

	/*
	 * Enable arm clock up (on exiting idle). Set arm divider
	 * ratios when not in idle along with the standby duration
	 * ratios.
	 */
	tmp = PWR_CTRL2_DIV2_UP_EN	 | \
	      PWR_CTRL2_DIV1_UP_EN	 | \
	      PWR_CTRL2_DUR_STANDBY2_VAL | \
	      PWR_CTRL2_DUR_STANDBY1_VAL | \
	      PWR_CTRL2_CORE2_UP_RATIO	 | \
	      PWR_CTRL2_CORE1_UP_RATIO;
	__raw_writel(tmp, EXYNOS5_PWR_CTRL2);
}

static int __init exynos4_init_cpuidle(void)
{
	int i, max_cpuidle_state, cpu_id;
	struct cpuidle_device *device;
	struct cpuidle_driver *drv = &exynos4_idle_driver;

	if (soc_is_exynos5250())
		exynos5_core_down_clk();

	/* Setup cpuidle driver */
	drv->state_count = (sizeof(exynos4_cpuidle_set) /
				       sizeof(struct cpuidle_state));
	max_cpuidle_state = drv->state_count;
	for (i = 0; i < max_cpuidle_state; i++) {
		memcpy(&drv->states[i], &exynos4_cpuidle_set[i],
				sizeof(struct cpuidle_state));
	}
	drv->safe_state_index = 0;
	cpuidle_register_driver(&exynos4_idle_driver);

	for_each_cpu(cpu_id, cpu_online_mask) {
		device = &per_cpu(exynos4_cpuidle_device, cpu_id);
		device->cpu = cpu_id;

		if (cpu_id == 0)
			device->state_count = (sizeof(exynos4_cpuidle_set) /
					       sizeof(struct cpuidle_state));
		else
			device->state_count = 1;	/* Support IDLE only */

		if (cpuidle_register_device(device)) {
			printk(KERN_ERR "CPUidle register device failed\n,");
			return -EIO;
		}
	}

	return 0;
}
device_initcall(exynos4_init_cpuidle);
