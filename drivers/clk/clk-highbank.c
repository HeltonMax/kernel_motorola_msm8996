/*
 * Copyright 2011-2012 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>

extern void __iomem *sregs_base;

#define HB_PLL_LOCK_500		0x20000000
#define HB_PLL_LOCK		0x10000000
#define HB_PLL_DIVF_SHIFT	20
#define HB_PLL_DIVF_MASK	0x0ff00000
#define HB_PLL_DIVQ_SHIFT	16
#define HB_PLL_DIVQ_MASK	0x00070000
#define HB_PLL_DIVR_SHIFT	8
#define HB_PLL_DIVR_MASK	0x00001f00
#define HB_PLL_RANGE_SHIFT	4
#define HB_PLL_RANGE_MASK	0x00000070
#define HB_PLL_BYPASS		0x00000008
#define HB_PLL_RESET		0x00000004
#define HB_PLL_EXT_BYPASS	0x00000002
#define HB_PLL_EXT_ENA		0x00000001

#define HB_PLL_VCO_MIN_FREQ	2133000000
#define HB_PLL_MAX_FREQ		HB_PLL_VCO_MIN_FREQ
#define HB_PLL_MIN_FREQ		(HB_PLL_VCO_MIN_FREQ / 64)

#define HB_A9_BCLK_DIV_MASK	0x00000006
#define HB_A9_BCLK_DIV_SHIFT	1
#define HB_A9_PCLK_DIV		0x00000001

struct hb_clk {
        struct clk_hw	hw;
	void __iomem	*reg;
	char *parent_name;
};
#define to_hb_clk(p) container_of(p, struct hb_clk, hw)

static int clk_pll_prepare(struct clk_hw *hwclk)
	{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	u32 reg;

	reg = readl(hbclk->reg);
	reg &= ~HB_PLL_RESET;
	writel(reg, hbclk->reg);

	while ((readl(hbclk->reg) & HB_PLL_LOCK) == 0)
		;
	while ((readl(hbclk->reg) & HB_PLL_LOCK_500) == 0)
		;

	return 0;
}

static void clk_pll_unprepare(struct clk_hw *hwclk)
{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	u32 reg;

	reg = readl(hbclk->reg);
	reg |= HB_PLL_RESET;
	writel(reg, hbclk->reg);
}

static int clk_pll_enable(struct clk_hw *hwclk)
{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	u32 reg;

	reg = readl(hbclk->reg);
	reg |= HB_PLL_EXT_ENA;
	writel(reg, hbclk->reg);

	return 0;
}

static void clk_pll_disable(struct clk_hw *hwclk)
{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	u32 reg;

	reg = readl(hbclk->reg);
	reg &= ~HB_PLL_EXT_ENA;
	writel(reg, hbclk->reg);
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	unsigned long divf, divq, vco_freq, reg;

	reg = readl(hbclk->reg);
	if (reg & HB_PLL_EXT_BYPASS)
		return parent_rate;

	divf = (reg & HB_PLL_DIVF_MASK) >> HB_PLL_DIVF_SHIFT;
	divq = (reg & HB_PLL_DIVQ_MASK) >> HB_PLL_DIVQ_SHIFT;
	vco_freq = parent_rate * (divf + 1);

	return vco_freq / (1 << divq);
}

static void clk_pll_calc(unsigned long rate, unsigned long ref_freq,
			u32 *pdivq, u32 *pdivf)
{
	u32 divq, divf;
	unsigned long vco_freq;

	if (rate < HB_PLL_MIN_FREQ)
		rate = HB_PLL_MIN_FREQ;
	if (rate > HB_PLL_MAX_FREQ)
		rate = HB_PLL_MAX_FREQ;

	for (divq = 1; divq <= 6; divq++) {
		if ((rate * (1 << divq)) >= HB_PLL_VCO_MIN_FREQ)
			break;
	}

	vco_freq = rate * (1 << divq);
	divf = (vco_freq + (ref_freq / 2)) / ref_freq;
	divf--;

	*pdivq = divq;
	*pdivf = divf;
}

static long clk_pll_round_rate(struct clk_hw *hwclk, unsigned long rate,
			       unsigned long *parent_rate)
{
	u32 divq, divf;
	unsigned long ref_freq = *parent_rate;

	clk_pll_calc(rate, ref_freq, &divq, &divf);

	return (ref_freq * (divf + 1)) / (1 << divq);
}

static int clk_pll_set_rate(struct clk_hw *hwclk, unsigned long rate,
			    unsigned long parent_rate)
{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	u32 divq, divf;
	u32 reg;

	clk_pll_calc(rate, parent_rate, &divq, &divf);

	reg = readl(hbclk->reg);
	if (divf != ((reg & HB_PLL_DIVF_MASK) >> HB_PLL_DIVF_SHIFT)) {
		/* Need to re-lock PLL, so put it into bypass mode */
		reg |= HB_PLL_EXT_BYPASS;
		writel(reg | HB_PLL_EXT_BYPASS, hbclk->reg);

		writel(reg | HB_PLL_RESET, hbclk->reg);
		reg &= ~(HB_PLL_DIVF_MASK | HB_PLL_DIVQ_MASK);
		reg |= (divf << HB_PLL_DIVF_SHIFT) | (divq << HB_PLL_DIVQ_SHIFT);
		writel(reg | HB_PLL_RESET, hbclk->reg);
		writel(reg, hbclk->reg);

		while ((readl(hbclk->reg) & HB_PLL_LOCK) == 0)
			;
		while ((readl(hbclk->reg) & HB_PLL_LOCK_500) == 0)
			;
		reg |= HB_PLL_EXT_ENA;
		reg &= ~HB_PLL_EXT_BYPASS;
	} else {
		reg &= ~HB_PLL_DIVQ_MASK;
		reg |= divq << HB_PLL_DIVQ_SHIFT;
	}
	writel(reg, hbclk->reg);

	return 0;
}

static const struct clk_ops clk_pll_ops = {
	.prepare = clk_pll_prepare,
	.unprepare = clk_pll_unprepare,
	.enable = clk_pll_enable,
	.disable = clk_pll_disable,
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_round_rate,
	.set_rate = clk_pll_set_rate,
};

static unsigned long clk_cpu_periphclk_recalc_rate(struct clk_hw *hwclk,
						   unsigned long parent_rate)
{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	u32 div = (readl(hbclk->reg) & HB_A9_PCLK_DIV) ? 8 : 4;
	return parent_rate / div;
}

static const struct clk_ops a9periphclk_ops = {
	.recalc_rate = clk_cpu_periphclk_recalc_rate,
};

static unsigned long clk_cpu_a9bclk_recalc_rate(struct clk_hw *hwclk,
						unsigned long parent_rate)
{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	u32 div = (readl(hbclk->reg) & HB_A9_BCLK_DIV_MASK) >> HB_A9_BCLK_DIV_SHIFT;

	return parent_rate / (div + 2);
}

static const struct clk_ops a9bclk_ops = {
	.recalc_rate = clk_cpu_a9bclk_recalc_rate,
};

static unsigned long clk_periclk_recalc_rate(struct clk_hw *hwclk,
					     unsigned long parent_rate)
{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	u32 div;

	div = readl(hbclk->reg) & 0x1f;
	div++;
	div *= 2;

	return parent_rate / div;
}

static long clk_periclk_round_rate(struct clk_hw *hwclk, unsigned long rate,
				   unsigned long *parent_rate)
{
	u32 div;

	div = *parent_rate / rate;
	div++;
	div &= ~0x1;

	return *parent_rate / div;
}

static int clk_periclk_set_rate(struct clk_hw *hwclk, unsigned long rate,
				unsigned long parent_rate)
{
	struct hb_clk *hbclk = to_hb_clk(hwclk);
	u32 div;

	div = parent_rate / rate;
	if (div & 0x1)
		return -EINVAL;

	writel(div >> 1, hbclk->reg);
	return 0;
}

static const struct clk_ops periclk_ops = {
	.recalc_rate = clk_periclk_recalc_rate,
	.round_rate = clk_periclk_round_rate,
	.set_rate = clk_periclk_set_rate,
};

static __init struct clk *hb_clk_init(struct device_node *node, const struct clk_ops *ops)
{
	u32 reg;
	struct clk *clk;
	struct hb_clk *hb_clk;
	const char *clk_name = node->name;
	const char *parent_name;
	struct clk_init_data init;
	int rc;

	rc = of_property_read_u32(node, "reg", &reg);
	if (WARN_ON(rc))
		return NULL;

	hb_clk = kzalloc(sizeof(*hb_clk), GFP_KERNEL);
	if (WARN_ON(!hb_clk))
		return NULL;

	hb_clk->reg = sregs_base + reg;

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;
	parent_name = of_clk_get_parent_name(node, 0);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	hb_clk->hw.init = &init;

	clk = clk_register(NULL, &hb_clk->hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(hb_clk);
		return NULL;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	return clk;
}

static void __init hb_pll_init(struct device_node *node)
{
	hb_clk_init(node, &clk_pll_ops);
}

static void __init hb_a9periph_init(struct device_node *node)
{
	hb_clk_init(node, &a9periphclk_ops);
}

static void __init hb_a9bus_init(struct device_node *node)
{
	struct clk *clk = hb_clk_init(node, &a9bclk_ops);
	clk_prepare_enable(clk);
}

static void __init hb_emmc_init(struct device_node *node)
{
	hb_clk_init(node, &periclk_ops);
}

static const __initconst struct of_device_id clk_match[] = {
	{ .compatible = "fixed-clock", .data = of_fixed_clk_setup, },
	{ .compatible = "calxeda,hb-pll-clock", .data = hb_pll_init, },
	{ .compatible = "calxeda,hb-a9periph-clock", .data = hb_a9periph_init, },
	{ .compatible = "calxeda,hb-a9bus-clock", .data = hb_a9bus_init, },
	{ .compatible = "calxeda,hb-emmc-clock", .data = hb_emmc_init, },
	{}
};

void __init highbank_clocks_init(void)
{
	of_clk_init(clk_match);
}
