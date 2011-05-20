/*
 * Driver for basic memory-mapped GPIO controllers.
 *
 * Copyright 2008 MontaVista Software, Inc.
 * Copyright 2008,2010 Anton Vorontsov <cbouatmailru@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * ....``.```~~~~````.`.`.`.`.```````'',,,.........`````......`.......
 * ...``                                                         ```````..
 * ..The simplest form of a GPIO controller that the driver supports is``
 *  `.just a single "data" register, where GPIO state can be read and/or `
 *    `,..written. ,,..``~~~~ .....``.`.`.~~.```.`.........``````.```````
 *        `````````
                                    ___
_/~~|___/~|   . ```~~~~~~       ___/___\___     ,~.`.`.`.`````.~~...,,,,...
__________|~$@~~~        %~    /o*o*o*o*o*o\   .. Implementing such a GPIO .
o        `                     ~~~~\___/~~~~    ` controller in FPGA is ,.`
                                                 `....trivial..'~`.```.```
 *                                                    ```````
 *  .```````~~~~`..`.``.``.
 * .  The driver supports  `...       ,..```.`~~~```````````````....````.``,,
 * .   big-endian notation, just`.  .. A bit more sophisticated controllers ,
 *  . register the device with -be`. .with a pair of set/clear-bit registers ,
 *   `.. suffix.  ```~~`````....`.`   . affecting the data register and the .`
 *     ``.`.``...```                  ```.. output pins are also supported.`
 *                        ^^             `````.`````````.,``~``~``~~``````
 *                                                   .                  ^^
 *   ,..`.`.`...````````````......`.`.`.`.`.`..`.`.`..
 * .. The expectation is that in at least some cases .    ,-~~~-,
 *  .this will be used with roll-your-own ASIC/FPGA .`     \   /
 *  .logic in Verilog or VHDL. ~~~`````````..`````~~`       \ /
 *  ..````````......```````````                             \o_
 *                                                           |
 *                              ^^                          / \
 *
 *           ...`````~~`.....``.`..........``````.`.``.```........``.
 *            `  8, 16, 32 and 64 bits registers are supported, and``.
 *            . the number of GPIOs is determined by the width of   ~
 *             .. the registers. ,............```.`.`..`.`.~~~.`.`.`~
 *               `.......````.```
 */

#include <linux/init.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/log2.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/basic_mmio_gpio.h>

struct bgpio_chip {
	struct gpio_chip gc;

	unsigned long (*read_reg)(void __iomem *reg);
	void (*write_reg)(void __iomem *reg, unsigned long data);

	void __iomem *reg_dat;
	void __iomem *reg_set;
	void __iomem *reg_clr;

	/* Number of bits (GPIOs): <register width> * 8. */
	int bits;

	/*
	 * Some GPIO controllers work with the big-endian bits notation,
	 * e.g. in a 8-bits register, GPIO7 is the least significant bit.
	 */
	unsigned long (*pin2mask)(struct bgpio_chip *bgc, unsigned int pin);

	/*
	 * Used to lock bgpio_chip->data. Also, this is needed to keep
	 * shadowed and real data registers writes together.
	 */
	spinlock_t lock;

	/* Shadowed data register to clear/set bits safely. */
	unsigned long data;
};

static struct bgpio_chip *to_bgpio_chip(struct gpio_chip *gc)
{
	return container_of(gc, struct bgpio_chip, gc);
}

static void bgpio_write8(void __iomem *reg, unsigned long data)
{
	__raw_writeb(data, reg);
}

static unsigned long bgpio_read8(void __iomem *reg)
{
	return __raw_readb(reg);
}

static void bgpio_write16(void __iomem *reg, unsigned long data)
{
	__raw_writew(data, reg);
}

static unsigned long bgpio_read16(void __iomem *reg)
{
	return __raw_readw(reg);
}

static void bgpio_write32(void __iomem *reg, unsigned long data)
{
	__raw_writel(data, reg);
}

static unsigned long bgpio_read32(void __iomem *reg)
{
	return __raw_readl(reg);
}

#if BITS_PER_LONG >= 64
static void bgpio_write64(void __iomem *reg, unsigned long data)
{
	__raw_writeq(data, reg);
}

static unsigned long bgpio_read64(void __iomem *reg)
{
	return __raw_readq(reg);
}
#endif /* BITS_PER_LONG >= 64 */

static unsigned long bgpio_pin2mask(struct bgpio_chip *bgc, unsigned int pin)
{
	return 1 << pin;
}

static unsigned long bgpio_pin2mask_be(struct bgpio_chip *bgc,
				       unsigned int pin)
{
	return 1 << (bgc->bits - 1 - pin);
}

static int bgpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);

	return bgc->read_reg(bgc->reg_dat) & bgc->pin2mask(bgc, gpio);
}

static void bgpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	unsigned long mask = bgc->pin2mask(bgc, gpio);
	unsigned long flags;

	if (bgc->reg_set) {
		if (val)
			bgc->write_reg(bgc->reg_set, mask);
		else
			bgc->write_reg(bgc->reg_clr, mask);
		return;
	}

	spin_lock_irqsave(&bgc->lock, flags);

	if (val)
		bgc->data |= mask;
	else
		bgc->data &= ~mask;

	bgc->write_reg(bgc->reg_dat, bgc->data);

	spin_unlock_irqrestore(&bgc->lock, flags);
}

static int bgpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	return 0;
}

static int bgpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	bgpio_set(gc, gpio, val);
	return 0;
}

static int bgpio_setup_accessors(struct platform_device *pdev,
				 struct bgpio_chip *bgc)
{
	const struct platform_device_id *platid = platform_get_device_id(pdev);

	switch (bgc->bits) {
	case 8:
		bgc->read_reg	= bgpio_read8;
		bgc->write_reg	= bgpio_write8;
		break;
	case 16:
		bgc->read_reg	= bgpio_read16;
		bgc->write_reg	= bgpio_write16;
		break;
	case 32:
		bgc->read_reg	= bgpio_read32;
		bgc->write_reg	= bgpio_write32;
		break;
#if BITS_PER_LONG >= 64
	case 64:
		bgc->read_reg	= bgpio_read64;
		bgc->write_reg	= bgpio_write64;
		break;
#endif /* BITS_PER_LONG >= 64 */
	default:
		dev_err(&pdev->dev, "unsupported data width %u bits\n",
			bgc->bits);
		return -EINVAL;
	}

	bgc->pin2mask = strcmp(platid->name, "basic-mmio-gpio-be") ?
		bgpio_pin2mask : bgpio_pin2mask_be;

	return 0;
}

static int __devinit bgpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bgpio_pdata *pdata = dev_get_platdata(dev);
	struct bgpio_chip *bgc;
	struct resource *res_dat;
	struct resource *res_set;
	struct resource *res_clr;
	resource_size_t dat_sz;
	int bits;
	int ret;

	res_dat = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dat");
	if (!res_dat)
		return -EINVAL;

	dat_sz = resource_size(res_dat);
	if (!is_power_of_2(dat_sz))
		return -EINVAL;

	bits = dat_sz * 8;
	if (bits > BITS_PER_LONG)
		return -EINVAL;

	bgc = devm_kzalloc(dev, sizeof(*bgc), GFP_KERNEL);
	if (!bgc)
		return -ENOMEM;

	bgc->reg_dat = devm_ioremap(dev, res_dat->start, dat_sz);
	if (!bgc->reg_dat)
		return -ENOMEM;

	res_set = platform_get_resource_byname(pdev, IORESOURCE_MEM, "set");
	res_clr = platform_get_resource_byname(pdev, IORESOURCE_MEM, "clr");
	if (res_set && res_clr) {
		if (resource_size(res_set) != resource_size(res_clr) ||
				resource_size(res_set) != dat_sz)
			return -EINVAL;

		bgc->reg_set = devm_ioremap(dev, res_set->start, dat_sz);
		bgc->reg_clr = devm_ioremap(dev, res_clr->start, dat_sz);
		if (!bgc->reg_set || !bgc->reg_clr)
			return -ENOMEM;
	} else if (res_set || res_clr) {
		return -EINVAL;
	}

	spin_lock_init(&bgc->lock);

	bgc->bits = bits;
	ret = bgpio_setup_accessors(pdev, bgc);
	if (ret)
		return ret;

	bgc->data = bgc->read_reg(bgc->reg_dat);
	bgc->gc.ngpio = bits;
	bgc->gc.direction_input = bgpio_dir_in;
	bgc->gc.direction_output = bgpio_dir_out;
	bgc->gc.get = bgpio_get;
	bgc->gc.set = bgpio_set;
	bgc->gc.dev = dev;
	bgc->gc.label = dev_name(dev);

	if (pdata)
		bgc->gc.base = pdata->base;
	else
		bgc->gc.base = -1;

	dev_set_drvdata(dev, bgc);

	ret = gpiochip_add(&bgc->gc);
	if (ret)
		dev_err(dev, "gpiochip_add() failed: %d\n", ret);

	return ret;
}

static int __devexit bgpio_remove(struct platform_device *pdev)
{
	struct bgpio_chip *bgc = dev_get_drvdata(&pdev->dev);

	return gpiochip_remove(&bgc->gc);
}

static const struct platform_device_id bgpio_id_table[] = {
	{ "basic-mmio-gpio", },
	{ "basic-mmio-gpio-be", },
	{},
};
MODULE_DEVICE_TABLE(platform, bgpio_id_table);

static struct platform_driver bgpio_driver = {
	.driver = {
		.name = "basic-mmio-gpio",
	},
	.id_table = bgpio_id_table,
	.probe = bgpio_probe,
	.remove = __devexit_p(bgpio_remove),
};

static int __init bgpio_init(void)
{
	return platform_driver_register(&bgpio_driver);
}
module_init(bgpio_init);

static void __exit bgpio_exit(void)
{
	platform_driver_unregister(&bgpio_driver);
}
module_exit(bgpio_exit);

MODULE_DESCRIPTION("Driver for basic memory-mapped GPIO controllers");
MODULE_AUTHOR("Anton Vorontsov <cbouatmailru@gmail.com>");
MODULE_LICENSE("GPL");
