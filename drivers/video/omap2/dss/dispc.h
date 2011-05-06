/*
 * linux/drivers/video/omap2/dss/dispc.h
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Archit Taneja <archit@ti.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __OMAP2_DISPC_REG_H
#define __OMAP2_DISPC_REG_H

struct dispc_reg { u16 idx; };

#define DISPC_REG(idx)			((const struct dispc_reg) { idx })

/* DISPC common registers */
#define DISPC_REVISION			DISPC_REG(0x0000)
#define DISPC_SYSCONFIG			DISPC_REG(0x0010)
#define DISPC_SYSSTATUS			DISPC_REG(0x0014)
#define DISPC_IRQSTATUS			DISPC_REG(0x0018)
#define DISPC_IRQENABLE			DISPC_REG(0x001C)
#define DISPC_CONTROL			DISPC_REG(0x0040)
#define DISPC_CONFIG			DISPC_REG(0x0044)
#define DISPC_CAPABLE			DISPC_REG(0x0048)
#define DISPC_LINE_STATUS		DISPC_REG(0x005C)
#define DISPC_LINE_NUMBER		DISPC_REG(0x0060)
#define DISPC_GLOBAL_ALPHA		DISPC_REG(0x0074)
#define DISPC_CONTROL2			DISPC_REG(0x0238)
#define DISPC_CONFIG2			DISPC_REG(0x0620)
#define DISPC_DIVISOR			DISPC_REG(0x0804)

/* DISPC overlay registers */
#define DISPC_OVL_BA0(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_BA0_OFFSET(n))
#define DISPC_OVL_BA1(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_BA1_OFFSET(n))
#define DISPC_OVL_POSITION(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_POS_OFFSET(n))
#define DISPC_OVL_SIZE(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_SIZE_OFFSET(n))
#define DISPC_OVL_ATTRIBUTES(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_ATTR_OFFSET(n))
#define DISPC_OVL_FIFO_THRESHOLD(n)	DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_FIFO_THRESH_OFFSET(n))
#define DISPC_OVL_FIFO_SIZE_STATUS(n)	DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_FIFO_SIZE_STATUS_OFFSET(n))
#define DISPC_OVL_ROW_INC(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_ROW_INC_OFFSET(n))
#define DISPC_OVL_PIXEL_INC(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_PIX_INC_OFFSET(n))
#define DISPC_OVL_WINDOW_SKIP(n)	DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_WINDOW_SKIP_OFFSET(n))
#define DISPC_OVL_TABLE_BA(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_TABLE_BA_OFFSET(n))
#define DISPC_OVL_FIR(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_FIR_OFFSET(n))
#define DISPC_OVL_PICTURE_SIZE(n)	DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_PIC_SIZE_OFFSET(n))
#define DISPC_OVL_ACCU0(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_ACCU0_OFFSET(n))
#define DISPC_OVL_ACCU1(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_ACCU1_OFFSET(n))
#define DISPC_OVL_FIR_COEF_H(n, i)	DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_FIR_COEF_H_OFFSET(n, i))
#define DISPC_OVL_FIR_COEF_HV(n, i)	DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_FIR_COEF_HV_OFFSET(n, i))
#define DISPC_OVL_CONV_COEF(n, i)	DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_CONV_COEF_OFFSET(n, i))
#define DISPC_OVL_FIR_COEF_V(n, i)	DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_FIR_COEF_V_OFFSET(n, i))
#define DISPC_OVL_PRELOAD(n)		DISPC_REG(DISPC_OVL_BASE(n) + \
					DISPC_PRELOAD_OFFSET(n))

/* DISPC manager/channel specific registers */
static inline struct dispc_reg DISPC_DEFAULT_COLOR(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x004C);
	case OMAP_DSS_CHANNEL_DIGIT:
		return DISPC_REG(0x0050);
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x03AC);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_TRANS_COLOR(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x0054);
	case OMAP_DSS_CHANNEL_DIGIT:
		return DISPC_REG(0x0058);
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x03B0);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_TIMING_H(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x0064);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x0400);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_TIMING_V(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x0068);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x0404);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_POL_FREQ(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x006C);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x0408);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_DIVISORo(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x0070);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x040C);
	default:
		BUG();
	}
}

/* Named as DISPC_SIZE_LCD, DISPC_SIZE_DIGIT and DISPC_SIZE_LCD2 in TRM */
static inline struct dispc_reg DISPC_SIZE_MGR(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x007C);
	case OMAP_DSS_CHANNEL_DIGIT:
		return DISPC_REG(0x0078);
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x03CC);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_DATA_CYCLE1(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x01D4);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x03C0);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_DATA_CYCLE2(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x01D8);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x03C4);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_DATA_CYCLE3(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x01DC);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x03C8);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_CPR_COEF_R(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x0220);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x03BC);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_CPR_COEF_G(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x0224);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x03B8);
	default:
		BUG();
	}
}

static inline struct dispc_reg DISPC_CPR_COEF_B(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return DISPC_REG(0x0228);
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
	case OMAP_DSS_CHANNEL_LCD2:
		return DISPC_REG(0x03B4);
	default:
		BUG();
	}
}

/* DISPC overlay register base addresses */
static inline u16 DISPC_OVL_BASE(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0080;
	case OMAP_DSS_VIDEO1:
		return 0x00BC;
	case OMAP_DSS_VIDEO2:
		return 0x014C;
	default:
		BUG();
	}
}

/* DISPC overlay register offsets */
static inline u16 DISPC_BA0_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0000;
	default:
		BUG();
	}
}

static inline u16 DISPC_BA1_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0004;
	default:
		BUG();
	}
}

static inline u16 DISPC_POS_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0008;
	default:
		BUG();
	}
}

static inline u16 DISPC_SIZE_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x000C;
	default:
		BUG();
	}
}

static inline u16 DISPC_ATTR_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0020;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0010;
	default:
		BUG();
	}
}

static inline u16 DISPC_FIFO_THRESH_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0024;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0014;
	default:
		BUG();
	}
}

static inline u16 DISPC_FIFO_SIZE_STATUS_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0028;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0018;
	default:
		BUG();
	}
}

static inline u16 DISPC_ROW_INC_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x002C;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x001C;
	default:
		BUG();
	}
}

static inline u16 DISPC_PIX_INC_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0030;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0020;
	default:
		BUG();
	}
}

static inline u16 DISPC_WINDOW_SKIP_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0034;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		BUG();
	default:
		BUG();
	}
}

static inline u16 DISPC_TABLE_BA_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0038;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		BUG();
	default:
		BUG();
	}
}

static inline u16 DISPC_FIR_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0024;
	default:
		BUG();
	}
}

static inline u16 DISPC_PIC_SIZE_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0028;
	default:
		BUG();
	}
}


static inline u16 DISPC_ACCU0_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x002C;
	default:
		BUG();
	}
}

static inline u16 DISPC_ACCU1_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0030;
	default:
		BUG();
	}
}

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
static inline u16 DISPC_FIR_COEF_H_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0034 + i * 0x8;
	default:
		BUG();
	}
}

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
static inline u16 DISPC_FIR_COEF_HV_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0038 + i * 0x8;
	default:
		BUG();
	}
}

/* coef index i = {0, 1, 2, 3, 4,} */
static inline u16 DISPC_CONV_COEF_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0074 + i * 0x4;
	default:
		BUG();
	}
}

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
static inline u16 DISPC_FIR_COEF_V_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
	case OMAP_DSS_VIDEO1:
		return 0x0124 + i * 0x4;
	case OMAP_DSS_VIDEO2:
		return 0x00B4 + i * 0x4;
	default:
		BUG();
	}
}

static inline u16 DISPC_PRELOAD_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x01AC;
	case OMAP_DSS_VIDEO1:
		return 0x0174;
	case OMAP_DSS_VIDEO2:
		return 0x00E8;
	default:
		BUG();
	}
}
#endif
