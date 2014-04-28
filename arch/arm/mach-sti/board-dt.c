/*
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited.
 * Author(s): Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/of_platform.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>

#include "smp.h"

static const char *stih41x_dt_match[] __initdata = {
	"st,stih415",
	"st,stih416",
	NULL
};

DT_MACHINE_START(STM, "STiH415/416 SoC with Flattened Device Tree")
	.dt_compat	= stih41x_dt_match,
	.l2c_aux_val	= L2C_AUX_CTRL_SHARED_OVERRIDE |
			  L310_AUX_CTRL_DATA_PREFETCH |
			  L310_AUX_CTRL_INSTR_PREFETCH |
			  L2C_AUX_CTRL_WAY_SIZE(4),
	.l2c_aux_mask	= 0xc0000fff,
	.smp		= smp_ops(sti_smp_ops),
MACHINE_END
