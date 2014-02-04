/* Copyright (c) 2010-2014 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

extern struct smp_operations qcom_smp_ops;

static const char * const qcom_dt_match[] __initconst = {
	"qcom,msm8660-surf",
	"qcom,msm8960-cdp",
	NULL
};

static const char * const apq8074_dt_match[] __initconst = {
	"qcom,apq8074-dragonboard",
	NULL
};

DT_MACHINE_START(QCOM_DT, "Qualcomm (Flattened Device Tree)")
	.smp = smp_ops(qcom_smp_ops),
	.dt_compat = qcom_dt_match,
MACHINE_END

DT_MACHINE_START(APQ_DT, "Qualcomm (Flattened Device Tree)")
	.dt_compat = apq8074_dt_match,
MACHINE_END
