/*
 * Copyright (c) 2010 Sascha Hauer <s.hauer@pengutronix.de>
 * Copyright (C) 2005-2009 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "../imx-drm.h"
#include "imx-ipu-v3.h"
#include "ipu-prv.h"

#define DC_MAP_CONF_PTR(n)	(0x108 + ((n) & ~0x1) * 2)
#define DC_MAP_CONF_VAL(n)	(0x144 + ((n) & ~0x1) * 2)

#define DC_EVT_NF		0
#define DC_EVT_NL		1
#define DC_EVT_EOF		2
#define DC_EVT_NFIELD		3
#define DC_EVT_EOL		4
#define DC_EVT_EOFIELD		5
#define DC_EVT_NEW_ADDR		6
#define DC_EVT_NEW_CHAN		7
#define DC_EVT_NEW_DATA		8

#define DC_EVT_NEW_ADDR_W_0	0
#define DC_EVT_NEW_ADDR_W_1	1
#define DC_EVT_NEW_CHAN_W_0	2
#define DC_EVT_NEW_CHAN_W_1	3
#define DC_EVT_NEW_DATA_W_0	4
#define DC_EVT_NEW_DATA_W_1	5
#define DC_EVT_NEW_ADDR_R_0	6
#define DC_EVT_NEW_ADDR_R_1	7
#define DC_EVT_NEW_CHAN_R_0	8
#define DC_EVT_NEW_CHAN_R_1	9
#define DC_EVT_NEW_DATA_R_0	10
#define DC_EVT_NEW_DATA_R_1	11

#define DC_WR_CH_CONF		0x0
#define DC_WR_CH_ADDR		0x4
#define DC_RL_CH(evt)		(8 + ((evt) & ~0x1) * 2)

#define DC_GEN			0xd4
#define DC_DISP_CONF1(disp)	(0xd8 + (disp) * 4)
#define DC_DISP_CONF2(disp)	(0xe8 + (disp) * 4)
#define DC_STAT			0x1c8

#define WROD(lf)		(0x18 | ((lf) << 1))
#define WRG			0x01
#define WCLK			0xc9

#define SYNC_WAVE 0
#define NULL_WAVE (-1)

#define DC_GEN_SYNC_1_6_SYNC	(2 << 1)
#define DC_GEN_SYNC_PRIORITY_1	(1 << 7)

#define DC_WR_CH_CONF_WORD_SIZE_8		(0 << 0)
#define DC_WR_CH_CONF_WORD_SIZE_16		(1 << 0)
#define DC_WR_CH_CONF_WORD_SIZE_24		(2 << 0)
#define DC_WR_CH_CONF_WORD_SIZE_32		(3 << 0)
#define DC_WR_CH_CONF_DISP_ID_PARALLEL(i)	(((i) & 0x1) << 3)
#define DC_WR_CH_CONF_DISP_ID_SERIAL		(2 << 3)
#define DC_WR_CH_CONF_DISP_ID_ASYNC		(3 << 4)
#define DC_WR_CH_CONF_FIELD_MODE		(1 << 9)
#define DC_WR_CH_CONF_PROG_TYPE_NORMAL		(4 << 5)
#define DC_WR_CH_CONF_PROG_TYPE_MASK		(7 << 5)
#define DC_WR_CH_CONF_PROG_DI_ID		(1 << 2)
#define DC_WR_CH_CONF_PROG_DISP_ID(i)		(((i) & 0x1) << 3)

#define IPU_DC_NUM_CHANNELS	10

struct ipu_dc_priv;

enum ipu_dc_map {
	IPU_DC_MAP_RGB24,
	IPU_DC_MAP_RGB565,
	IPU_DC_MAP_GBR24, /* TVEv2 */
};

struct ipu_dc {
	/* The display interface number assigned to this dc channel */
	unsigned int		di;
	void __iomem		*base;
	struct ipu_dc_priv	*priv;
	int			chno;
	bool			in_use;
};

struct ipu_dc_priv {
	void __iomem		*dc_reg;
	void __iomem		*dc_tmpl_reg;
	struct ipu_soc		*ipu;
	struct device		*dev;
	struct ipu_dc		channels[IPU_DC_NUM_CHANNELS];
	struct mutex		mutex;
};

static void dc_link_event(struct ipu_dc *dc, int event, int addr, int priority)
{
	u32 reg;

	reg = readl(dc->base + DC_RL_CH(event));
	reg &= ~(0xffff << (16 * (event & 0x1)));
	reg |= ((addr << 8) | priority) << (16 * (event & 0x1));
	writel(reg, dc->base + DC_RL_CH(event));
}

static void dc_write_tmpl(struct ipu_dc *dc, int word, u32 opcode, u32 operand,
		int map, int wave, int glue, int sync, int stop)
{
	struct ipu_dc_priv *priv = dc->priv;
	u32 reg1, reg2;

	if (opcode == WCLK) {
		reg1 = (operand << 20) & 0xfff00000;
		reg2 = operand >> 12 | opcode << 1 | stop << 9;
	} else if (opcode == WRG) {
		reg1 = sync | glue << 4 | ++wave << 11 | ((operand << 15) & 0xffff8000);
		reg2 = operand >> 17 | opcode << 7 | stop << 9;
	} else {
		reg1 = sync | glue << 4 | ++wave << 11 | ++map << 15 | ((operand << 20) & 0xfff00000);
		reg2 = operand >> 12 | opcode << 4 | stop << 9;
	}
	writel(reg1, priv->dc_tmpl_reg + word * 8);
	writel(reg2, priv->dc_tmpl_reg + word * 8 + 4);
}

static int ipu_pixfmt_to_map(u32 fmt)
{
	switch (fmt) {
	case V4L2_PIX_FMT_RGB24:
		return IPU_DC_MAP_RGB24;
	case V4L2_PIX_FMT_RGB565:
		return IPU_DC_MAP_RGB565;
	case IPU_PIX_FMT_GBR24:
		return IPU_DC_MAP_GBR24;
	default:
		return -EINVAL;
	}
}

int ipu_dc_init_sync(struct ipu_dc *dc, struct ipu_di *di, bool interlaced,
		u32 pixel_fmt, u32 width)
{
	struct ipu_dc_priv *priv = dc->priv;
	u32 reg = 0, map;

	dc->di = ipu_di_get_num(di);

	map = ipu_pixfmt_to_map(pixel_fmt);
	if (map < 0) {
		dev_dbg(priv->dev, "IPU_DISP: No MAP\n");
		return -EINVAL;
	}

	if (interlaced) {
		dc_link_event(dc, DC_EVT_NL, 0, 3);
		dc_link_event(dc, DC_EVT_EOL, 0, 2);
		dc_link_event(dc, DC_EVT_NEW_DATA, 0, 1);

		/* Init template microcode */
		dc_write_tmpl(dc, 0, WROD(0), 0, map, SYNC_WAVE, 0, 8, 1);
	} else {
		if (dc->di) {
			dc_link_event(dc, DC_EVT_NL, 2, 3);
			dc_link_event(dc, DC_EVT_EOL, 3, 2);
			dc_link_event(dc, DC_EVT_NEW_DATA, 1, 1);
			/* Init template microcode */
			dc_write_tmpl(dc, 2, WROD(0), 0, map, SYNC_WAVE, 8, 5, 1);
			dc_write_tmpl(dc, 3, WROD(0), 0, map, SYNC_WAVE, 4, 5, 0);
			dc_write_tmpl(dc, 4, WRG, 0, map, NULL_WAVE, 0, 0, 1);
			dc_write_tmpl(dc, 1, WROD(0), 0, map, SYNC_WAVE, 0, 5, 1);
		} else {
			dc_link_event(dc, DC_EVT_NL, 5, 3);
			dc_link_event(dc, DC_EVT_EOL, 6, 2);
			dc_link_event(dc, DC_EVT_NEW_DATA, 8, 1);
			/* Init template microcode */
			dc_write_tmpl(dc, 5, WROD(0), 0, map, SYNC_WAVE, 8, 5, 1);
			dc_write_tmpl(dc, 6, WROD(0), 0, map, SYNC_WAVE, 4, 5, 0);
			dc_write_tmpl(dc, 7, WRG, 0, map, NULL_WAVE, 0, 0, 1);
			dc_write_tmpl(dc, 8, WROD(0), 0, map, SYNC_WAVE, 0, 5, 1);
		}
	}
	dc_link_event(dc, DC_EVT_NF, 0, 0);
	dc_link_event(dc, DC_EVT_NFIELD, 0, 0);
	dc_link_event(dc, DC_EVT_EOF, 0, 0);
	dc_link_event(dc, DC_EVT_EOFIELD, 0, 0);
	dc_link_event(dc, DC_EVT_NEW_CHAN, 0, 0);
	dc_link_event(dc, DC_EVT_NEW_ADDR, 0, 0);

	reg = readl(dc->base + DC_WR_CH_CONF);
	if (interlaced)
		reg |= DC_WR_CH_CONF_FIELD_MODE;
	else
		reg &= ~DC_WR_CH_CONF_FIELD_MODE;
	writel(reg, dc->base + DC_WR_CH_CONF);

	writel(0x0, dc->base + DC_WR_CH_ADDR);
	writel(width, priv->dc_reg + DC_DISP_CONF2(dc->di));

	ipu_module_enable(priv->ipu, IPU_CONF_DC_EN);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_dc_init_sync);

void ipu_dc_enable_channel(struct ipu_dc *dc)
{
	int di;
	u32 reg;

	di = dc->di;

	reg = readl(dc->base + DC_WR_CH_CONF);
	reg |= DC_WR_CH_CONF_PROG_TYPE_NORMAL;
	writel(reg, dc->base + DC_WR_CH_CONF);
}
EXPORT_SYMBOL_GPL(ipu_dc_enable_channel);

void ipu_dc_disable_channel(struct ipu_dc *dc)
{
	struct ipu_dc_priv *priv = dc->priv;
	u32 val;
	int irq = 0, timeout = 50;

	if (dc->chno == 1)
		irq = IPU_IRQ_DC_FC_1;
	else if (dc->chno == 5)
		irq = IPU_IRQ_DP_SF_END;
	else
		return;

	/* should wait for the interrupt here */
	mdelay(50);

	if (dc->di == 0)
		val = 0x00000002;
	else
		val = 0x00000020;

	/* Wait for DC triple buffer to empty */
	while ((readl(priv->dc_reg + DC_STAT) & val) != val) {
		msleep(2);
		timeout -= 2;
		if (timeout <= 0)
			break;
	}

	val = readl(dc->base + DC_WR_CH_CONF);
	val &= ~DC_WR_CH_CONF_PROG_TYPE_MASK;
	writel(val, dc->base + DC_WR_CH_CONF);
}
EXPORT_SYMBOL_GPL(ipu_dc_disable_channel);

static void ipu_dc_map_config(struct ipu_dc_priv *priv, enum ipu_dc_map map,
		int byte_num, int offset, int mask)
{
	int ptr = map * 3 + byte_num;
	u32 reg;

	reg = readl(priv->dc_reg + DC_MAP_CONF_VAL(ptr));
	reg &= ~(0xffff << (16 * (ptr & 0x1)));
	reg |= ((offset << 8) | mask) << (16 * (ptr & 0x1));
	writel(reg, priv->dc_reg + DC_MAP_CONF_VAL(ptr));

	reg = readl(priv->dc_reg + DC_MAP_CONF_PTR(map));
	reg &= ~(0x1f << ((16 * (map & 0x1)) + (5 * byte_num)));
	reg |= ptr << ((16 * (map & 0x1)) + (5 * byte_num));
	writel(reg, priv->dc_reg + DC_MAP_CONF_PTR(map));
}

static void ipu_dc_map_clear(struct ipu_dc_priv *priv, int map)
{
	u32 reg = readl(priv->dc_reg + DC_MAP_CONF_PTR(map));

	writel(reg & ~(0xffff << (16 * (map & 0x1))),
		     priv->dc_reg + DC_MAP_CONF_PTR(map));
}

struct ipu_dc *ipu_dc_get(struct ipu_soc *ipu, int channel)
{
	struct ipu_dc_priv *priv = ipu->dc_priv;
	struct ipu_dc *dc;

	if (channel >= IPU_DC_NUM_CHANNELS)
		return ERR_PTR(-ENODEV);

	dc = &priv->channels[channel];

	mutex_lock(&priv->mutex);

	if (dc->in_use) {
		mutex_unlock(&priv->mutex);
		return ERR_PTR(-EBUSY);
	}

	dc->in_use = 1;

	mutex_unlock(&priv->mutex);

	return dc;
}
EXPORT_SYMBOL_GPL(ipu_dc_get);

void ipu_dc_put(struct ipu_dc *dc)
{
	struct ipu_dc_priv *priv = dc->priv;

	mutex_lock(&priv->mutex);
	dc->in_use = 0;
	mutex_unlock(&priv->mutex);
}
EXPORT_SYMBOL_GPL(ipu_dc_put);

int ipu_dc_init(struct ipu_soc *ipu, struct device *dev,
		unsigned long base, unsigned long template_base)
{
	struct ipu_dc_priv *priv;
	static int channel_offsets[] = { 0, 0x1c, 0x38, 0x54, 0x58, 0x5c,
		0x78, 0, 0x94, 0xb4};
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->mutex);

	priv->dev = dev;
	priv->ipu = ipu;
	priv->dc_reg = devm_ioremap(dev, base, PAGE_SIZE);
	priv->dc_tmpl_reg = devm_ioremap(dev, template_base, PAGE_SIZE);
	if (!priv->dc_reg || !priv->dc_tmpl_reg)
		return -ENOMEM;

	for (i = 0; i < IPU_DC_NUM_CHANNELS; i++) {
		priv->channels[i].chno = i;
		priv->channels[i].priv = priv;
		priv->channels[i].base = priv->dc_reg + channel_offsets[i];
	}

	writel(DC_WR_CH_CONF_WORD_SIZE_24 | DC_WR_CH_CONF_DISP_ID_PARALLEL(1) |
			DC_WR_CH_CONF_PROG_DI_ID,
			priv->channels[1].base + DC_WR_CH_CONF);
	writel(DC_WR_CH_CONF_WORD_SIZE_24 | DC_WR_CH_CONF_DISP_ID_PARALLEL(0),
			priv->channels[5].base + DC_WR_CH_CONF);

	writel(DC_GEN_SYNC_1_6_SYNC | DC_GEN_SYNC_PRIORITY_1, priv->dc_reg + DC_GEN);

	ipu->dc_priv = priv;

	dev_dbg(dev, "DC base: 0x%08lx template base: 0x%08lx\n",
			base, template_base);

	/* rgb24 */
	ipu_dc_map_clear(priv, IPU_DC_MAP_RGB24);
	ipu_dc_map_config(priv, IPU_DC_MAP_RGB24, 0, 7, 0xff); /* blue */
	ipu_dc_map_config(priv, IPU_DC_MAP_RGB24, 1, 15, 0xff); /* green */
	ipu_dc_map_config(priv, IPU_DC_MAP_RGB24, 2, 23, 0xff); /* red */

	/* rgb565 */
	ipu_dc_map_clear(priv, IPU_DC_MAP_RGB565);
	ipu_dc_map_config(priv, IPU_DC_MAP_RGB565, 0, 4, 0xf8); /* blue */
	ipu_dc_map_config(priv, IPU_DC_MAP_RGB565, 1, 10, 0xfc); /* green */
	ipu_dc_map_config(priv, IPU_DC_MAP_RGB565, 2, 15, 0xf8); /* red */

	/* gbr24 */
	ipu_dc_map_clear(priv, IPU_DC_MAP_GBR24);
	ipu_dc_map_config(priv, IPU_DC_MAP_GBR24, 2, 15, 0xff); /* green */
	ipu_dc_map_config(priv, IPU_DC_MAP_GBR24, 1, 7, 0xff); /* blue */
	ipu_dc_map_config(priv, IPU_DC_MAP_GBR24, 0, 23, 0xff); /* red */

	return 0;
}

void ipu_dc_exit(struct ipu_soc *ipu)
{
}
