/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_imx_keypad_data_entry_single(soc, _size)			\
	{								\
		.iobase = soc ## _KPP_BASE_ADDR,			\
		.iosize = _size,					\
		.irq = soc ## _INT_KPP,					\
	}

#ifdef CONFIG_SOC_IMX21
const struct imx_imx_keypad_data imx21_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX21, SZ_16);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX25
const struct imx_imx_keypad_data imx25_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX25, SZ_16K);
#endif /* ifdef CONFIG_SOC_IMX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_imx_keypad_data imx27_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX27, SZ_16);
#endif /* ifdef CONFIG_SOC_IMX27 */

struct platform_device *__init imx_add_imx_keypad(
		const struct imx_imx_keypad_data *data,
		const struct matrix_keymap_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("imx-keypad", -1,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata));
}
