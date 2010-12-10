/*
 * pm.c - Common OMAP2+ power management-related code
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/err.h>

#include <plat/omap-pm.h>
#include <plat/omap_device.h>
#include <plat/common.h>
#include <plat/voltage.h>

#include "powerdomain.h"
#include "clockdomain.h"
#include "pm.h"

static struct omap_device_pm_latency *pm_lats;

static struct device *mpu_dev;
static struct device *iva_dev;
static struct device *l3_dev;
static struct device *dsp_dev;

struct device *omap2_get_mpuss_device(void)
{
	WARN_ON_ONCE(!mpu_dev);
	return mpu_dev;
}

struct device *omap2_get_iva_device(void)
{
	WARN_ON_ONCE(!iva_dev);
	return iva_dev;
}

struct device *omap2_get_l3_device(void)
{
	WARN_ON_ONCE(!l3_dev);
	return l3_dev;
}

struct device *omap4_get_dsp_device(void)
{
	WARN_ON_ONCE(!dsp_dev);
	return dsp_dev;
}
EXPORT_SYMBOL(omap4_get_dsp_device);

/* static int _init_omap_device(struct omap_hwmod *oh, void *user) */
static int _init_omap_device(char *name, struct device **new_dev)
{
	struct omap_hwmod *oh;
	struct omap_device *od;

	oh = omap_hwmod_lookup(name);
	if (WARN(!oh, "%s: could not find omap_hwmod for %s\n",
		 __func__, name))
		return -ENODEV;

	od = omap_device_build(oh->name, 0, oh, NULL, 0, pm_lats, 0, false);
	if (WARN(IS_ERR(od), "%s: could not build omap_device for %s\n",
		 __func__, name))
		return -ENODEV;

	*new_dev = &od->pdev.dev;

	return 0;
}

/*
 * Build omap_devices for processors and bus.
 */
static void omap2_init_processor_devices(void)
{
	_init_omap_device("mpu", &mpu_dev);
	_init_omap_device("iva", &iva_dev);
	if (cpu_is_omap44xx()) {
		_init_omap_device("l3_main_1", &l3_dev);
		_init_omap_device("dsp", &dsp_dev);
	} else {
		_init_omap_device("l3_main", &l3_dev);
	}
}

/* Types of sleep_switch used in omap_set_pwrdm_state */
#define FORCEWAKEUP_SWITCH	0
#define LOWPOWERSTATE_SWITCH	1

/*
 * This sets pwrdm state (other than mpu & core. Currently only ON &
 * RET are supported.
 */
int omap_set_pwrdm_state(struct powerdomain *pwrdm, u32 state)
{
	u32 cur_state;
	int sleep_switch = 0;
	int ret = 0;

	if (pwrdm == NULL || IS_ERR(pwrdm))
		return -EINVAL;

	while (!(pwrdm->pwrsts & (1 << state))) {
		if (state == PWRDM_POWER_OFF)
			return ret;
		state--;
	}

	cur_state = pwrdm_read_next_pwrst(pwrdm);
	if (cur_state == state)
		return ret;

	if (pwrdm_read_pwrst(pwrdm) < PWRDM_POWER_ON) {
		if ((pwrdm_read_pwrst(pwrdm) > state) &&
			(pwrdm->flags & PWRDM_HAS_LOWPOWERSTATECHANGE)) {
			sleep_switch = LOWPOWERSTATE_SWITCH;
		} else {
			omap2_clkdm_wakeup(pwrdm->pwrdm_clkdms[0]);
			pwrdm_wait_transition(pwrdm);
			sleep_switch = FORCEWAKEUP_SWITCH;
		}
	}

	ret = pwrdm_set_next_pwrst(pwrdm, state);
	if (ret) {
		printk(KERN_ERR "Unable to set state of powerdomain: %s\n",
		       pwrdm->name);
		goto err;
	}

	switch (sleep_switch) {
	case FORCEWAKEUP_SWITCH:
		if (pwrdm->pwrdm_clkdms[0]->flags & CLKDM_CAN_ENABLE_AUTO)
			omap2_clkdm_allow_idle(pwrdm->pwrdm_clkdms[0]);
		else
			omap2_clkdm_sleep(pwrdm->pwrdm_clkdms[0]);
		break;
	case LOWPOWERSTATE_SWITCH:
		pwrdm_set_lowpwrstchange(pwrdm);
		break;
	default:
		return ret;
	}

	pwrdm_wait_transition(pwrdm);
	pwrdm_state_switch(pwrdm);
err:
	return ret;
}

static int __init omap2_common_pm_init(void)
{
	omap2_init_processor_devices();
	omap_pm_if_init();

	return 0;
}
postcore_initcall(omap2_common_pm_init);

static int __init omap2_common_pm_late_init(void)
{
	/* Init the OMAP TWL parameters */
	omap3_twl_init();
	/* Init the voltage layer */
	omap_voltage_late_init();
	/* Smartreflex device init */
	omap_devinit_smartreflex();

	return 0;
}
late_initcall(omap2_common_pm_late_init);
