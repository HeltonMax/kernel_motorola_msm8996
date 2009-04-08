/*
 * SH7206 Setup
 *
 *  Copyright (C) 2006  Yoshinori Sato
 *  Copyright (C) 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7,
	PINT0, PINT1, PINT2, PINT3, PINT4, PINT5, PINT6, PINT7,
	ADC_ADI0, ADC_ADI1,

	DMAC0, DMAC1, DMAC2, DMAC3, DMAC4, DMAC5, DMAC6, DMAC7,

	MTU0_ABCD, MTU0_VEF, MTU1_AB, MTU1_VU, MTU2_AB, MTU2_VU,
	MTU3_ABCD, MTU4_ABCD, MTU5, POE2_12, MTU3S_ABCD, MTU4S_ABCD, MTU5S,
	IIC3,

	CMT0, CMT1, BSC, WDT,

	MTU2_TCI3V, MTU2_TCI4V, MTU2S_TCI3V, MTU2S_TCI4V,

	POE2_OEI3,

	SCIF0, SCIF1, SCIF2, SCIF3,

	/* interrupt groups */
	PINT,
};

static struct intc_vect vectors[] __initdata = {
	INTC_IRQ(IRQ0, 64), INTC_IRQ(IRQ1, 65),
	INTC_IRQ(IRQ2, 66), INTC_IRQ(IRQ3, 67),
	INTC_IRQ(IRQ4, 68), INTC_IRQ(IRQ5, 69),
	INTC_IRQ(IRQ6, 70), INTC_IRQ(IRQ7, 71),
	INTC_IRQ(PINT0, 80), INTC_IRQ(PINT1, 81),
	INTC_IRQ(PINT2, 82), INTC_IRQ(PINT3, 83),
	INTC_IRQ(PINT4, 84), INTC_IRQ(PINT5, 85),
	INTC_IRQ(PINT6, 86), INTC_IRQ(PINT7, 87),
	INTC_IRQ(ADC_ADI0, 92), INTC_IRQ(ADC_ADI1, 96),
	INTC_IRQ(DMAC0, 108), INTC_IRQ(DMAC0, 109),
	INTC_IRQ(DMAC1, 112), INTC_IRQ(DMAC1, 113),
	INTC_IRQ(DMAC2, 116), INTC_IRQ(DMAC2, 117),
	INTC_IRQ(DMAC3, 120), INTC_IRQ(DMAC3, 121),
	INTC_IRQ(DMAC4, 124), INTC_IRQ(DMAC4, 125),
	INTC_IRQ(DMAC5, 128), INTC_IRQ(DMAC5, 129),
	INTC_IRQ(DMAC6, 132), INTC_IRQ(DMAC6, 133),
	INTC_IRQ(DMAC7, 136), INTC_IRQ(DMAC7, 137),
	INTC_IRQ(CMT0, 140), INTC_IRQ(CMT1, 144),
	INTC_IRQ(BSC, 148), INTC_IRQ(WDT, 152),
	INTC_IRQ(MTU0_ABCD, 156), INTC_IRQ(MTU0_ABCD, 157),
	INTC_IRQ(MTU0_ABCD, 158), INTC_IRQ(MTU0_ABCD, 159),
	INTC_IRQ(MTU0_VEF, 160), INTC_IRQ(MTU0_VEF, 161),
	INTC_IRQ(MTU0_VEF, 162),
	INTC_IRQ(MTU1_AB, 164), INTC_IRQ(MTU1_AB, 165),
	INTC_IRQ(MTU1_VU, 168), INTC_IRQ(MTU1_VU, 169),
	INTC_IRQ(MTU2_AB, 172), INTC_IRQ(MTU2_AB, 173),
	INTC_IRQ(MTU2_VU, 176), INTC_IRQ(MTU2_VU, 177),
	INTC_IRQ(MTU3_ABCD, 180), INTC_IRQ(MTU3_ABCD, 181),
	INTC_IRQ(MTU3_ABCD, 182), INTC_IRQ(MTU3_ABCD, 183),
	INTC_IRQ(MTU2_TCI3V, 184),
	INTC_IRQ(MTU4_ABCD, 188), INTC_IRQ(MTU4_ABCD, 189),
	INTC_IRQ(MTU4_ABCD, 190), INTC_IRQ(MTU4_ABCD, 191),
	INTC_IRQ(MTU2_TCI4V, 192),
	INTC_IRQ(MTU5, 196), INTC_IRQ(MTU5, 197),
	INTC_IRQ(MTU5, 198),
	INTC_IRQ(POE2_12, 200), INTC_IRQ(POE2_12, 201),
	INTC_IRQ(MTU3S_ABCD, 204), INTC_IRQ(MTU3S_ABCD, 205),
	INTC_IRQ(MTU3S_ABCD, 206), INTC_IRQ(MTU3S_ABCD, 207),
	INTC_IRQ(MTU2S_TCI3V, 208),
	INTC_IRQ(MTU4S_ABCD, 212), INTC_IRQ(MTU4S_ABCD, 213),
	INTC_IRQ(MTU4S_ABCD, 214), INTC_IRQ(MTU4S_ABCD, 215),
	INTC_IRQ(MTU2S_TCI4V, 216),
	INTC_IRQ(MTU5S, 220), INTC_IRQ(MTU5S, 221),
	INTC_IRQ(MTU5S, 222),
	INTC_IRQ(POE2_OEI3, 224),
	INTC_IRQ(IIC3, 228), INTC_IRQ(IIC3, 229),
	INTC_IRQ(IIC3, 230), INTC_IRQ(IIC3, 231),
	INTC_IRQ(IIC3, 232),
	INTC_IRQ(SCIF0, 240), INTC_IRQ(SCIF0, 241),
	INTC_IRQ(SCIF0, 242), INTC_IRQ(SCIF0, 243),
	INTC_IRQ(SCIF1, 244), INTC_IRQ(SCIF1, 245),
	INTC_IRQ(SCIF1, 246), INTC_IRQ(SCIF1, 247),
	INTC_IRQ(SCIF2, 248), INTC_IRQ(SCIF2, 249),
	INTC_IRQ(SCIF2, 250), INTC_IRQ(SCIF2, 251),
	INTC_IRQ(SCIF3, 252), INTC_IRQ(SCIF3, 253),
	INTC_IRQ(SCIF3, 254), INTC_IRQ(SCIF3, 255),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(PINT, PINT0, PINT1, PINT2, PINT3,
		   PINT4, PINT5, PINT6, PINT7),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xfffe0818, 0, 16, 4, /* IPR01 */ { IRQ0, IRQ1, IRQ2, IRQ3 } },
	{ 0xfffe081a, 0, 16, 4, /* IPR02 */ { IRQ4, IRQ5, IRQ6, IRQ7 } },
	{ 0xfffe0820, 0, 16, 4, /* IPR05 */ { PINT, 0, ADC_ADI0, ADC_ADI1 } },
	{ 0xfffe0c00, 0, 16, 4, /* IPR06 */ { DMAC0, DMAC1, DMAC2, DMAC3 } },
	{ 0xfffe0c02, 0, 16, 4, /* IPR07 */ { DMAC4, DMAC5, DMAC6, DMAC7 } },
	{ 0xfffe0c04, 0, 16, 4, /* IPR08 */ { CMT0, CMT1, BSC, WDT } },
	{ 0xfffe0c06, 0, 16, 4, /* IPR09 */ { MTU0_ABCD, MTU0_VEF,
					      MTU1_AB, MTU1_VU } },
	{ 0xfffe0c08, 0, 16, 4, /* IPR10 */ { MTU2_AB, MTU2_VU,
					      MTU3_ABCD, MTU2_TCI3V } },
	{ 0xfffe0c0a, 0, 16, 4, /* IPR11 */ { MTU4_ABCD, MTU2_TCI4V,
					      MTU5, POE2_12 } },
	{ 0xfffe0c0c, 0, 16, 4, /* IPR12 */ { MTU3S_ABCD, MTU2S_TCI3V,
					      MTU4S_ABCD, MTU2S_TCI4V } },
	{ 0xfffe0c0e, 0, 16, 4, /* IPR13 */ { MTU5S, POE2_OEI3, IIC3, 0 } },
	{ 0xfffe0c10, 0, 16, 4, /* IPR14 */ { SCIF0, SCIF1, SCIF2, SCIF3 } },
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xfffe0808, 0, 16, /* PINTER */
	  { 0, 0, 0, 0, 0, 0, 0, 0,
	    PINT7, PINT6, PINT5, PINT4, PINT3, PINT2, PINT1, PINT0 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7206", vectors, groups,
			 mask_registers, prio_registers, NULL);

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xfffe8000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 240, 240, 240, 240 },
	}, {
		.mapbase	= 0xfffe8800,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 244, 244, 244, 244 },
	}, {
		.mapbase	= 0xfffe9000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 248, 248, 248, 248 },
	}, {
		.mapbase	= 0xfffe9800,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 252, 252, 252, 252 },
	}, {
		.flags = 0,
	}
};

static struct platform_device sci_device = {
	.name		= "sh-sci",
	.id		= -1,
	.dev		= {
		.platform_data	= sci_platform_data,
	},
};

static struct platform_device *sh7206_devices[] __initdata = {
	&sci_device,
};

static int __init sh7206_devices_setup(void)
{
	return platform_add_devices(sh7206_devices,
				    ARRAY_SIZE(sh7206_devices));
}
__initcall(sh7206_devices_setup);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}
