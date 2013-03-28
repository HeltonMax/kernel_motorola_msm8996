/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *          Daniel Willerud <daniel.willerud@stericsson.com> for ST-Ericsson
 *
 * AB8500 peripheral regulators
 *
 * AB8500 supports the following regulators:
 *   VAUX1/2/3, VINTCORE, VTVOUT, VUSB, VAUDIO, VAMIC1/2, VDMIC, VANA
 *
 * AB8505 supports the following regulators:
 *   VAUX1/2/3/4/5/6, VINTCORE, VADC, VUSB, VAUDIO, VAMIC1/2, VDMIC, VANA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/ab8500.h>
#include <linux/slab.h>

/**
 * struct ab8500_regulator_info - ab8500 regulator information
 * @dev: device pointer
 * @desc: regulator description
 * @regulator_dev: regulator device
 * @is_enabled: status of regulator (on/off)
 * @load_lp_uA: maximum load in idle (low power) mode
 * @update_bank: bank to control on/off
 * @update_reg: register to control on/off
 * @update_mask: mask to enable/disable and set mode of regulator
 * @update_val: bits holding the regulator current mode
 * @update_val_idle: bits to enable the regulator in idle (low power) mode
 * @update_val_normal: bits to enable the regulator in normal (high power) mode
 * @voltage_bank: bank to control regulator voltage
 * @voltage_reg: register to control regulator voltage
 * @voltage_mask: mask to control regulator voltage
 * @voltage_shift: shift to control regulator voltage
 */
struct ab8500_regulator_info {
	struct device		*dev;
	struct regulator_desc	desc;
	struct regulator_dev	*regulator;
	bool is_enabled;
	int load_lp_uA;
	u8 update_bank;
	u8 update_reg;
	u8 update_mask;
	u8 update_val;
	u8 update_val_idle;
	u8 update_val_normal;
	u8 voltage_bank;
	u8 voltage_reg;
	u8 voltage_mask;
	u8 voltage_shift;
};

/* voltage tables for the vauxn/vintcore supplies */
static const unsigned int ldo_vauxn_voltages[] = {
	1100000,
	1200000,
	1300000,
	1400000,
	1500000,
	1800000,
	1850000,
	1900000,
	2500000,
	2650000,
	2700000,
	2750000,
	2800000,
	2900000,
	3000000,
	3300000,
};

static const unsigned int ldo_vaux3_voltages[] = {
	1200000,
	1500000,
	1800000,
	2100000,
	2500000,
	2750000,
	2790000,
	2910000,
};

static const int ldo_vaux56_voltages[] = {
	1800000,
	1050000,
	1100000,
	1200000,
	1500000,
	2200000,
	2500000,
	2790000,
};

static const int ldo_vaux3_ab8540_voltages[] = {
	1200000,
	1500000,
	1800000,
	2100000,
	2500000,
	2750000,
	2790000,
	2910000,
	3050000,
};

static const unsigned int ldo_vintcore_voltages[] = {
	1200000,
	1225000,
	1250000,
	1275000,
	1300000,
	1325000,
	1350000,
};

static const int ldo_sdio_voltages[] = {
	1160000,
	1050000,
	1100000,
	1500000,
	1800000,
	2200000,
	2910000,
	3050000,
};

static const unsigned int fixed_1200000_voltage[] = {
	1200000,
};

static const unsigned int fixed_1800000_voltage[] = {
	1800000,
};

static const unsigned int fixed_2000000_voltage[] = {
	2000000,
};

static const unsigned int fixed_2050000_voltage[] = {
	2050000,
};

static const unsigned int fixed_3300000_voltage[] = {
	3300000,
};

static int ab8500_regulator_enable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, info->update_val);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't set enable bits for regulator\n");
		return ret;
	}

	info->is_enabled = true;

	dev_vdbg(rdev_get_dev(rdev),
		"%s-enable (bank, reg, mask, value): 0x%x, 0x%x, 0x%x, 0x%x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, info->update_val);

	return ret;
}

static int ab8500_regulator_disable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, 0x0);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't set disable bits for regulator\n");
		return ret;
	}

	info->is_enabled = false;

	dev_vdbg(rdev_get_dev(rdev),
		"%s-disable (bank, reg, mask, value): 0x%x, 0x%x, 0x%x, 0x%x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, 0x0);

	return ret;
}

static unsigned int ab8500_regulator_get_optimum_mode(
		struct regulator_dev *rdev, int input_uV,
		int output_uV, int load_uA)
{
	unsigned int mode;

	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	if (load_uA <= info->load_lp_uA)
		mode = REGULATOR_MODE_IDLE;
	else
		mode = REGULATOR_MODE_NORMAL;

	return mode;
}

static int ab8500_regulator_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	int ret;
	u8 update_val;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		update_val = info->update_val_normal;
		break;
	case REGULATOR_MODE_IDLE:
		update_val = info->update_val_idle;
		break;
	default:
		return -EINVAL;
	}

	/* ab8500 regulators share mode and enable in the same register bits.
	   off = 0b00
	   low power mode= 0b11
	   full powermode = 0b01
	   (HW control mode = 0b10)
	   Thus we don't write to the register when regulator is disabled.
	*/
	if (info->is_enabled) {
		ret = abx500_mask_and_set_register_interruptible(info->dev,
			info->update_bank, info->update_reg,
			info->update_mask, update_val);
		if (ret < 0) {
			dev_err(rdev_get_dev(rdev),
				"couldn't set regulator mode\n");
			return ret;
		}

		dev_vdbg(rdev_get_dev(rdev),
			"%s-set_mode (bank, reg, mask, value): "
			"0x%x, 0x%x, 0x%x, 0x%x\n",
			info->desc.name, info->update_bank, info->update_reg,
			info->update_mask, update_val);
	}

	info->update_val = update_val;

	return 0;
}

static unsigned int ab8500_regulator_get_mode(struct regulator_dev *rdev)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	if (info->update_val == info->update_val_normal)
		ret = REGULATOR_MODE_NORMAL;
	else if (info->update_val == info->update_val_idle)
		ret = REGULATOR_MODE_IDLE;
	else
		ret = -EINVAL;

	return ret;
}

static int ab8500_regulator_is_enabled(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_get_register_interruptible(info->dev,
		info->update_bank, info->update_reg, &regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read 0x%x register\n", info->update_reg);
		return ret;
	}

	dev_vdbg(rdev_get_dev(rdev),
		"%s-is_enabled (bank, reg, mask, value): 0x%x, 0x%x, 0x%x,"
		" 0x%x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, regval);

	if (regval & info->update_mask)
		info->is_enabled = true;
	else
		info->is_enabled = false;

	return info->is_enabled;
}

static int ab8500_regulator_get_voltage_sel(struct regulator_dev *rdev)
{
	int ret, val;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_get_register_interruptible(info->dev,
			info->voltage_bank, info->voltage_reg, &regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read voltage reg for regulator\n");
		return ret;
	}

	dev_vdbg(rdev_get_dev(rdev),
		"%s-get_voltage (bank, reg, mask, shift, value): "
		"0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
		info->desc.name, info->voltage_bank,
		info->voltage_reg, info->voltage_mask,
		info->voltage_shift, regval);

	val = regval & info->voltage_mask;
	return val >> info->voltage_shift;
}

static int ab8500_regulator_set_voltage_sel(struct regulator_dev *rdev,
					    unsigned selector)
{
	int ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	/* set the registers for the request */
	regval = (u8)selector << info->voltage_shift;
	ret = abx500_mask_and_set_register_interruptible(info->dev,
			info->voltage_bank, info->voltage_reg,
			info->voltage_mask, regval);
	if (ret < 0)
		dev_err(rdev_get_dev(rdev),
		"couldn't set voltage reg for regulator\n");

	dev_vdbg(rdev_get_dev(rdev),
		"%s-set_voltage (bank, reg, mask, value): 0x%x, 0x%x, 0x%x,"
		" 0x%x\n",
		info->desc.name, info->voltage_bank, info->voltage_reg,
		info->voltage_mask, regval);

	return ret;
}

static struct regulator_ops ab8500_regulator_volt_mode_ops = {
	.enable			= ab8500_regulator_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.get_optimum_mode	= ab8500_regulator_get_optimum_mode,
	.set_mode		= ab8500_regulator_set_mode,
	.get_mode		= ab8500_regulator_get_mode,
	.get_voltage_sel 	= ab8500_regulator_get_voltage_sel,
	.set_voltage_sel	= ab8500_regulator_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_table,
};

static struct regulator_ops ab8500_regulator_mode_ops = {
	.enable			= ab8500_regulator_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.get_optimum_mode	= ab8500_regulator_get_optimum_mode,
	.set_mode		= ab8500_regulator_set_mode,
	.get_mode		= ab8500_regulator_get_mode,
	.list_voltage		= regulator_list_voltage_linear,
};

static struct regulator_ops ab8500_regulator_ops = {
	.enable			= ab8500_regulator_enable,
	.disable		= ab8500_regulator_disable,
	.is_enabled		= ab8500_regulator_is_enabled,
	.list_voltage		= regulator_list_voltage_linear,
};

/* AB8500 regulator information */
static struct ab8500_regulator_info
		ab8500_regulator_info[AB8500_NUM_REGULATORS] = {
	/*
	 * Variable Voltage Regulators
	 *   name, min mV, max mV,
	 *   update bank, reg, mask, enable val
	 *   volt bank, reg, mask
	 */
	[AB8500_LDO_AUX1] = {
		.desc = {
			.name		= "LDO-AUX1",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX1,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
			.volt_table	= ldo_vauxn_voltages,
			.enable_time	= 200,
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1f,
		.voltage_mask		= 0x0f,
	},
	[AB8500_LDO_AUX2] = {
		.desc = {
			.name		= "LDO-AUX2",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX2,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
			.volt_table	= ldo_vauxn_voltages,
			.enable_time	= 200,
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x20,
		.voltage_mask		= 0x0f,
	},
	[AB8500_LDO_AUX3] = {
		.desc = {
			.name		= "LDO-AUX3",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX3,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux3_voltages),
			.volt_table	= ldo_vaux3_voltages,
			.enable_time	= 450,
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x0a,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x21,
		.voltage_mask		= 0x07,
	},
	[AB8500_LDO_INTCORE] = {
		.desc = {
			.name		= "LDO-INTCORE",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_INTCORE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vintcore_voltages),
			.volt_table	= ldo_vintcore_voltages,
			.enable_time	= 750,
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x44,
		.update_val		= 0x44,
		.update_val_idle	= 0x44,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x80,
		.voltage_mask		= 0x38,
		.voltage_shift		= 3,
	},

	/*
	 * Fixed Voltage Regulators
	 *   name, fixed mV,
	 *   update bank, reg, mask, enable val
	 */
	[AB8500_LDO_TVOUT] = {
		.desc = {
			.name		= "LDO-TVOUT",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_TVOUT,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2000000_voltage,
			.enable_time	= 500,
		},
		.load_lp_uA		= 1000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x82,
		.update_val		= 0x02,
		.update_val_idle	= 0x82,
		.update_val_normal	= 0x02,
	},
	[AB8500_LDO_AUDIO] = {
		.desc = {
			.name		= "LDO-AUDIO",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUDIO,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 140,
			.volt_table	= fixed_2000000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x02,
		.update_val		= 0x02,
	},
	[AB8500_LDO_ANAMIC1] = {
		.desc = {
			.name		= "LDO-ANAMIC1",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 500,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x08,
		.update_val		= 0x08,
	},
	[AB8500_LDO_ANAMIC2] = {
		.desc = {
			.name		= "LDO-ANAMIC2",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 500,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x10,
		.update_val		= 0x10,
	},
	[AB8500_LDO_DMIC] = {
		.desc = {
			.name		= "LDO-DMIC",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_DMIC,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 420,
			.volt_table	= fixed_1800000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x04,
		.update_val		= 0x04,
	},

	/*
	 * Regulators with fixed voltage and normal/idle modes
	 */
	[AB8500_LDO_ANA] = {
		.desc = {
			.name		= "LDO-ANA",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANA,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.enable_time	= 140,
			.volt_table	= fixed_1200000_voltage,
		},
		.load_lp_uA		= 1000,
		.update_bank		= 0x04,
		.update_reg		= 0x06,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
	},
};

/* AB8505 regulator information */
static struct ab8500_regulator_info
		ab8505_regulator_info[AB8505_NUM_REGULATORS] = {
	/*
	 * Variable Voltage Regulators
	 *   name, min mV, max mV,
	 *   update bank, reg, mask, enable val
	 *   volt bank, reg, mask, table, table length
	 */
	[AB8505_LDO_AUX1] = {
		.desc = {
			.name		= "LDO-AUX1",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX1,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8505_LDO_AUX2] = {
		.desc = {
			.name		= "LDO-AUX2",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX2,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x20,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8505_LDO_AUX3] = {
		.desc = {
			.name		= "LDO-AUX3",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX3,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux3_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x0a,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x21,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux3_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux3_voltages),
	},
	[AB8505_LDO_AUX4] = {
		.desc = {
			.name		= "LDO-AUX4",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_AUX4,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.load_lp_uA		= 5000,
		/* values for Vaux4Regu register */
		.update_bank		= 0x04,
		.update_reg		= 0x2e,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		/* values for Vaux4SEL register */
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x2f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8505_LDO_AUX5] = {
		.desc = {
			.name		= "LDO-AUX5",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX5,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux56_voltages),
		},
		.load_lp_uA		= 2000,
		/* values for CtrlVaux5 register */
		.update_bank		= 0x01,
		.update_reg		= 0x55,
		.update_mask		= 0x18,
		.update_val		= 0x10,
		.update_val_idle	= 0x18,
		.update_val_normal	= 0x10,
		.voltage_bank		= 0x01,
		.voltage_reg		= 0x55,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux56_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux56_voltages),
	},
	[AB8505_LDO_AUX6] = {
		.desc = {
			.name		= "LDO-AUX6",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX6,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux56_voltages),
		},
		.load_lp_uA		= 2000,
		/* values for CtrlVaux6 register */
		.update_bank		= 0x01,
		.update_reg		= 0x56,
		.update_mask		= 0x18,
		.update_val		= 0x10,
		.update_val_idle	= 0x18,
		.update_val_normal	= 0x10,
		.voltage_bank		= 0x01,
		.voltage_reg		= 0x56,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux56_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux56_voltages),
	},
	[AB8505_LDO_INTCORE] = {
		.desc = {
			.name		= "LDO-INTCORE",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_INTCORE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vintcore_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x44,
		.update_val		= 0x04,
		.update_val_idle	= 0x44,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x80,
		.voltage_mask		= 0x38,
		.voltages		= ldo_vintcore_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vintcore_voltages),
		.voltage_shift		= 3,
	},

	/*
	 * Fixed Voltage Regulators
	 *   name, fixed mV,
	 *   update bank, reg, mask, enable val
	 */
	[AB8505_LDO_ADC] = {
		.desc = {
			.name		= "LDO-ADC",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_ADC,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2000000_voltage,
		},
		.delay			= 10000,
		.load_lp_uA		= 1000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x82,
		.update_val		= 0x02,
		.update_val_idle	= 0x82,
		.update_val_normal	= 0x02,
	},
	[AB8505_LDO_USB] = {
		.desc = {
			.name           = "LDO-USB",
			.ops            = &ab8500_regulator_mode_ops,
			.type           = REGULATOR_VOLTAGE,
			.id             = AB9540_LDO_USB,
			.owner          = THIS_MODULE,
			.n_voltages     = 1,
			.volt_table	= fixed_3300000_voltage,
		},
		.update_bank            = 0x03,
		.update_reg             = 0x82,
		.update_mask            = 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
	},
	[AB8505_LDO_AUDIO] = {
		.desc = {
			.name		= "LDO-AUDIO",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUDIO,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2000000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x02,
		.update_val		= 0x02,
	},
	[AB8505_LDO_ANAMIC1] = {
		.desc = {
			.name		= "LDO-ANAMIC1",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x08,
		.update_val		= 0x08,
	},
	[AB8505_LDO_ANAMIC2] = {
		.desc = {
			.name		= "LDO-ANAMIC2",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x10,
		.update_val		= 0x10,
	},
	[AB8505_LDO_AUX8] = {
		.desc = {
			.name		= "LDO-AUX8",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8505_LDO_AUX8,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_1800000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x04,
		.update_val		= 0x04,
	},
	/*
	 * Regulators with fixed voltage and normal/idle modes
	 */
	[AB8505_LDO_ANA] = {
		.desc = {
			.name		= "LDO-ANA",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANA,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_1200000_voltage,
		},
		.load_lp_uA		= 1000,
		.update_bank		= 0x04,
		.update_reg		= 0x06,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
	},
};

/* AB9540 regulator information */
static struct ab8500_regulator_info
		ab9540_regulator_info[AB9540_NUM_REGULATORS] = {
	/*
	 * Variable Voltage Regulators
	 *   name, min mV, max mV,
	 *   update bank, reg, mask, enable val
	 *   volt bank, reg, mask, table, table length
	 */
	[AB9540_LDO_AUX1] = {
		.desc = {
			.name		= "LDO-AUX1",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX1,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB9540_LDO_AUX2] = {
		.desc = {
			.name		= "LDO-AUX2",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX2,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x20,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB9540_LDO_AUX3] = {
		.desc = {
			.name		= "LDO-AUX3",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX3,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux3_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x0a,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x21,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux3_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux3_voltages),
	},
	[AB9540_LDO_AUX4] = {
		.desc = {
			.name		= "LDO-AUX4",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_AUX4,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.load_lp_uA		= 5000,
		/* values for Vaux4Regu register */
		.update_bank		= 0x04,
		.update_reg		= 0x2e,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		/* values for Vaux4SEL register */
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x2f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB9540_LDO_INTCORE] = {
		.desc = {
			.name		= "LDO-INTCORE",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_INTCORE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vintcore_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x44,
		.update_val		= 0x44,
		.update_val_idle	= 0x44,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x80,
		.voltage_mask		= 0x38,
		.voltages		= ldo_vintcore_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vintcore_voltages),
		.voltage_shift		= 3,
	},

	/*
	 * Fixed Voltage Regulators
	 *   name, fixed mV,
	 *   update bank, reg, mask, enable val
	 */
	[AB9540_LDO_TVOUT] = {
		.desc = {
			.name		= "LDO-TVOUT",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_TVOUT,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2000000_voltage,
		},
		.delay			= 10000,
		.load_lp_uA		= 1000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x82,
		.update_val		= 0x02,
		.update_val_idle	= 0x82,
		.update_val_normal	= 0x02,
	},
	[AB9540_LDO_USB] = {
		.desc = {
			.name           = "LDO-USB",
			.ops            = &ab8500_regulator_ops,
			.type           = REGULATOR_VOLTAGE,
			.id             = AB9540_LDO_USB,
			.owner          = THIS_MODULE,
			.n_voltages     = 1,
			.volt_table	= fixed_3300000_voltage,
		},
		.update_bank            = 0x03,
		.update_reg             = 0x82,
		.update_mask            = 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
	},
	[AB9540_LDO_AUDIO] = {
		.desc = {
			.name		= "LDO-AUDIO",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUDIO,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2000000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x02,
		.update_val		= 0x02,
	},
	[AB9540_LDO_ANAMIC1] = {
		.desc = {
			.name		= "LDO-ANAMIC1",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x08,
		.update_val		= 0x08,
	},
	[AB9540_LDO_ANAMIC2] = {
		.desc = {
			.name		= "LDO-ANAMIC2",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x10,
		.update_val		= 0x10,
	},
	[AB9540_LDO_DMIC] = {
		.desc = {
			.name		= "LDO-DMIC",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_DMIC,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_1800000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x04,
		.update_val		= 0x04,
	},

	/*
	 * Regulators with fixed voltage and normal/idle modes
	 */
	[AB9540_LDO_ANA] = {
		.desc = {
			.name		= "LDO-ANA",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANA,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_1200000_voltage,
		},
		.load_lp_uA		= 1000,
		.update_bank		= 0x04,
		.update_reg		= 0x06,
		.update_mask		= 0x0c,
		.update_val		= 0x08,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x08,
	},
};

/* AB8540 regulator information */
static struct ab8500_regulator_info
		ab8540_regulator_info[AB8540_NUM_REGULATORS] = {
	/*
	 * Variable Voltage Regulators
	 *   name, min mV, max mV,
	 *   update bank, reg, mask, enable val
	 *   volt bank, reg, mask, table, table length
	 */
	[AB8540_LDO_AUX1] = {
		.desc = {
			.name		= "LDO-AUX1",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX1,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x1f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8540_LDO_AUX2] = {
		.desc = {
			.name		= "LDO-AUX2",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX2,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x09,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x20,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8540_LDO_AUX3] = {
		.desc = {
			.name		= "LDO-AUX3",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUX3,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vaux3_ab8540_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x04,
		.update_reg		= 0x0a,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x21,
		.voltage_mask		= 0x07,
		.voltages		= ldo_vaux3_ab8540_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vaux3_ab8540_voltages),
	},
	[AB8540_LDO_AUX4] = {
		.desc = {
			.name		= "LDO-AUX4",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB9540_LDO_AUX4,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vauxn_voltages),
		},
		.load_lp_uA		= 5000,
		/* values for Vaux4Regu register */
		.update_bank		= 0x04,
		.update_reg		= 0x2e,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_idle	= 0x03,
		.update_val_normal	= 0x01,
		/* values for Vaux4SEL register */
		.voltage_bank		= 0x04,
		.voltage_reg		= 0x2f,
		.voltage_mask		= 0x0f,
		.voltages		= ldo_vauxn_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vauxn_voltages),
	},
	[AB8540_LDO_INTCORE] = {
		.desc = {
			.name		= "LDO-INTCORE",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_INTCORE,
			.owner		= THIS_MODULE,
			.n_voltages	= ARRAY_SIZE(ldo_vintcore_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x44,
		.update_val		= 0x44,
		.update_val_idle	= 0x44,
		.update_val_normal	= 0x04,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x80,
		.voltage_mask		= 0x38,
		.voltages		= ldo_vintcore_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_vintcore_voltages),
		.voltage_shift		= 3,
	},

	/*
	 * Fixed Voltage Regulators
	 *   name, fixed mV,
	 *   update bank, reg, mask, enable val
	 */
	[AB8540_LDO_TVOUT] = {
		.desc = {
			.name		= "LDO-TVOUT",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_TVOUT,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.delay			= 10000,
		.load_lp_uA		= 1000,
		.update_bank		= 0x03,
		.update_reg		= 0x80,
		.update_mask		= 0x82,
		.update_val		= 0x02,
		.update_val_idle	= 0x82,
		.update_val_normal	= 0x02,
	},
	[AB8540_LDO_AUDIO] = {
		.desc = {
			.name		= "LDO-AUDIO",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_AUDIO,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2000000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x02,
		.update_val		= 0x02,
	},
	[AB8540_LDO_ANAMIC1] = {
		.desc = {
			.name		= "LDO-ANAMIC1",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x08,
		.update_val		= 0x08,
	},
	[AB8540_LDO_ANAMIC2] = {
		.desc = {
			.name		= "LDO-ANAMIC2",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANAMIC2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table	= fixed_2050000_voltage,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x10,
		.update_val		= 0x10,
	},
	[AB8540_LDO_DMIC] = {
		.desc = {
			.name		= "LDO-DMIC",
			.ops		= &ab8500_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_DMIC,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.update_bank		= 0x03,
		.update_reg		= 0x83,
		.update_mask		= 0x04,
		.update_val		= 0x04,
	},

	/*
	 * Regulators with fixed voltage and normal/idle modes
	 */
	[AB8540_LDO_ANA] = {
		.desc = {
			.name		= "LDO-ANA",
			.ops		= &ab8500_regulator_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_LDO_ANA,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
			.volt_table     = fixed_1200000_voltage,
		},
		.load_lp_uA		= 1000,
		.update_bank		= 0x04,
		.update_reg		= 0x06,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_idle	= 0x0c,
		.update_val_normal	= 0x04,
	},
	[AB8540_LDO_SDIO] = {
		.desc = {
			.name		= "LDO-SDIO",
			.ops		= &ab8500_regulator_volt_mode_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8540_LDO_SDIO,
			.owner		= THIS_MODULE,
			.n_voltages = ARRAY_SIZE(ldo_sdio_voltages),
		},
		.load_lp_uA		= 5000,
		.update_bank		= 0x03,
		.update_reg		= 0x88,
		.update_mask		= 0x30,
		.update_val		= 0x10,
		.update_val_idle	= 0x30,
		.update_val_normal	= 0x10,
		.voltage_bank		= 0x03,
		.voltage_reg		= 0x88,
		.voltage_mask		= 0x07,
		.voltages		= ldo_sdio_voltages,
		.voltages_len		= ARRAY_SIZE(ldo_sdio_voltages),
	},
};

struct ab8500_reg_init {
	u8 bank;
	u8 addr;
	u8 mask;
};

#define REG_INIT(_id, _bank, _addr, _mask)	\
	[_id] = {				\
		.bank = _bank,			\
		.addr = _addr,			\
		.mask = _mask,			\
	}

/* AB8500 register init */
static struct ab8500_reg_init ab8500_reg_init[] = {
	/*
	 * 0x30, VanaRequestCtrl
	 * 0xc0, VextSupply1RequestCtrl
	 */
	REG_INIT(AB8500_REGUREQUESTCTRL2,	0x03, 0x04, 0xf0),
	/*
	 * 0x03, VextSupply2RequestCtrl
	 * 0x0c, VextSupply3RequestCtrl
	 * 0x30, Vaux1RequestCtrl
	 * 0xc0, Vaux2RequestCtrl
	 */
	REG_INIT(AB8500_REGUREQUESTCTRL3,	0x03, 0x05, 0xff),
	/*
	 * 0x03, Vaux3RequestCtrl
	 * 0x04, SwHPReq
	 */
	REG_INIT(AB8500_REGUREQUESTCTRL4,	0x03, 0x06, 0x07),
	/*
	 * 0x08, VanaSysClkReq1HPValid
	 * 0x20, Vaux1SysClkReq1HPValid
	 * 0x40, Vaux2SysClkReq1HPValid
	 * 0x80, Vaux3SysClkReq1HPValid
	 */
	REG_INIT(AB8500_REGUSYSCLKREQ1HPVALID1,	0x03, 0x07, 0xe8),
	/*
	 * 0x10, VextSupply1SysClkReq1HPValid
	 * 0x20, VextSupply2SysClkReq1HPValid
	 * 0x40, VextSupply3SysClkReq1HPValid
	 */
	REG_INIT(AB8500_REGUSYSCLKREQ1HPVALID2,	0x03, 0x08, 0x70),
	/*
	 * 0x08, VanaHwHPReq1Valid
	 * 0x20, Vaux1HwHPReq1Valid
	 * 0x40, Vaux2HwHPReq1Valid
	 * 0x80, Vaux3HwHPReq1Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ1VALID1,	0x03, 0x09, 0xe8),
	/*
	 * 0x01, VextSupply1HwHPReq1Valid
	 * 0x02, VextSupply2HwHPReq1Valid
	 * 0x04, VextSupply3HwHPReq1Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ1VALID2,	0x03, 0x0a, 0x07),
	/*
	 * 0x08, VanaHwHPReq2Valid
	 * 0x20, Vaux1HwHPReq2Valid
	 * 0x40, Vaux2HwHPReq2Valid
	 * 0x80, Vaux3HwHPReq2Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ2VALID1,	0x03, 0x0b, 0xe8),
	/*
	 * 0x01, VextSupply1HwHPReq2Valid
	 * 0x02, VextSupply2HwHPReq2Valid
	 * 0x04, VextSupply3HwHPReq2Valid
	 */
	REG_INIT(AB8500_REGUHWHPREQ2VALID2,	0x03, 0x0c, 0x07),
	/*
	 * 0x20, VanaSwHPReqValid
	 * 0x80, Vaux1SwHPReqValid
	 */
	REG_INIT(AB8500_REGUSWHPREQVALID1,	0x03, 0x0d, 0xa0),
	/*
	 * 0x01, Vaux2SwHPReqValid
	 * 0x02, Vaux3SwHPReqValid
	 * 0x04, VextSupply1SwHPReqValid
	 * 0x08, VextSupply2SwHPReqValid
	 * 0x10, VextSupply3SwHPReqValid
	 */
	REG_INIT(AB8500_REGUSWHPREQVALID2,	0x03, 0x0e, 0x1f),
	/*
	 * 0x02, SysClkReq2Valid1
	 * 0x04, SysClkReq3Valid1
	 * 0x08, SysClkReq4Valid1
	 * 0x10, SysClkReq5Valid1
	 * 0x20, SysClkReq6Valid1
	 * 0x40, SysClkReq7Valid1
	 * 0x80, SysClkReq8Valid1
	 */
	REG_INIT(AB8500_REGUSYSCLKREQVALID1,	0x03, 0x0f, 0xfe),
	/*
	 * 0x02, SysClkReq2Valid2
	 * 0x04, SysClkReq3Valid2
	 * 0x08, SysClkReq4Valid2
	 * 0x10, SysClkReq5Valid2
	 * 0x20, SysClkReq6Valid2
	 * 0x40, SysClkReq7Valid2
	 * 0x80, SysClkReq8Valid2
	 */
	REG_INIT(AB8500_REGUSYSCLKREQVALID2,	0x03, 0x10, 0xfe),
	/*
	 * 0x02, VTVoutEna
	 * 0x04, Vintcore12Ena
	 * 0x38, Vintcore12Sel
	 * 0x40, Vintcore12LP
	 * 0x80, VTVoutLP
	 */
	REG_INIT(AB8500_REGUMISC1,		0x03, 0x80, 0xfe),
	/*
	 * 0x02, VaudioEna
	 * 0x04, VdmicEna
	 * 0x08, Vamic1Ena
	 * 0x10, Vamic2Ena
	 */
	REG_INIT(AB8500_VAUDIOSUPPLY,		0x03, 0x83, 0x1e),
	/*
	 * 0x01, Vamic1_dzout
	 * 0x02, Vamic2_dzout
	 */
	REG_INIT(AB8500_REGUCTRL1VAMIC,		0x03, 0x84, 0x03),
	/*
	 * 0x03, VpllRegu (NOTE! PRCMU register bits)
	 * 0x0c, VanaRegu
	 */
	REG_INIT(AB8500_VPLLVANAREGU,		0x04, 0x06, 0x0f),
	/*
	 * 0x01, VrefDDREna
	 * 0x02, VrefDDRSleepMode
	 */
	REG_INIT(AB8500_VREFDDR,		0x04, 0x07, 0x03),
	/*
	 * 0x03, VextSupply1Regu
	 * 0x0c, VextSupply2Regu
	 * 0x30, VextSupply3Regu
	 * 0x40, ExtSupply2Bypass
	 * 0x80, ExtSupply3Bypass
	 */
	REG_INIT(AB8500_EXTSUPPLYREGU,		0x04, 0x08, 0xff),
	/*
	 * 0x03, Vaux1Regu
	 * 0x0c, Vaux2Regu
	 */
	REG_INIT(AB8500_VAUX12REGU,		0x04, 0x09, 0x0f),
	/*
	 * 0x03, Vaux3Regu
	 */
	REG_INIT(AB8500_VRF1VAUX3REGU,		0x04, 0x0a, 0x03),
	/*
	 * 0x0f, Vaux1Sel
	 */
	REG_INIT(AB8500_VAUX1SEL,		0x04, 0x1f, 0x0f),
	/*
	 * 0x0f, Vaux2Sel
	 */
	REG_INIT(AB8500_VAUX2SEL,		0x04, 0x20, 0x0f),
	/*
	 * 0x07, Vaux3Sel
	 */
	REG_INIT(AB8500_VRF1VAUX3SEL,		0x04, 0x21, 0x07),
	/*
	 * 0x01, VextSupply12LP
	 */
	REG_INIT(AB8500_REGUCTRL2SPARE,		0x04, 0x22, 0x01),
	/*
	 * 0x04, Vaux1Disch
	 * 0x08, Vaux2Disch
	 * 0x10, Vaux3Disch
	 * 0x20, Vintcore12Disch
	 * 0x40, VTVoutDisch
	 * 0x80, VaudioDisch
	 */
	REG_INIT(AB8500_REGUCTRLDISCH,		0x04, 0x43, 0xfc),
	/*
	 * 0x02, VanaDisch
	 * 0x04, VdmicPullDownEna
	 * 0x10, VdmicDisch
	 */
	REG_INIT(AB8500_REGUCTRLDISCH2,		0x04, 0x44, 0x16),
};

/* AB8505 register init */
static struct ab8500_reg_init ab8505_reg_init[] = {
	/*
	 * 0x03, VarmRequestCtrl
	 * 0x0c, VsmpsCRequestCtrl
	 * 0x30, VsmpsARequestCtrl
	 * 0xc0, VsmpsBRequestCtrl
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL1,	0x03, 0x03, 0xff),
	/*
	 * 0x03, VsafeRequestCtrl
	 * 0x0c, VpllRequestCtrl
	 * 0x30, VanaRequestCtrl
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL2,	0x03, 0x04, 0x3f),
	/*
	 * 0x30, Vaux1RequestCtrl
	 * 0xc0, Vaux2RequestCtrl
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL3,	0x03, 0x05, 0xf0),
	/*
	 * 0x03, Vaux3RequestCtrl
	 * 0x04, SwHPReq
	 */
	REG_INIT(AB8505_REGUREQUESTCTRL4,	0x03, 0x06, 0x07),
	/*
	 * 0x01, VsmpsASysClkReq1HPValid
	 * 0x02, VsmpsBSysClkReq1HPValid
	 * 0x04, VsafeSysClkReq1HPValid
	 * 0x08, VanaSysClkReq1HPValid
	 * 0x10, VpllSysClkReq1HPValid
	 * 0x20, Vaux1SysClkReq1HPValid
	 * 0x40, Vaux2SysClkReq1HPValid
	 * 0x80, Vaux3SysClkReq1HPValid
	 */
	REG_INIT(AB8505_REGUSYSCLKREQ1HPVALID1,	0x03, 0x07, 0xff),
	/*
	 * 0x01, VsmpsCSysClkReq1HPValid
	 * 0x02, VarmSysClkReq1HPValid
	 * 0x04, VbbSysClkReq1HPValid
	 * 0x08, VsmpsMSysClkReq1HPValid
	 */
	REG_INIT(AB8505_REGUSYSCLKREQ1HPVALID2,	0x03, 0x08, 0x0f),
	/*
	 * 0x01, VsmpsAHwHPReq1Valid
	 * 0x02, VsmpsBHwHPReq1Valid
	 * 0x04, VsafeHwHPReq1Valid
	 * 0x08, VanaHwHPReq1Valid
	 * 0x10, VpllHwHPReq1Valid
	 * 0x20, Vaux1HwHPReq1Valid
	 * 0x40, Vaux2HwHPReq1Valid
	 * 0x80, Vaux3HwHPReq1Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ1VALID1,	0x03, 0x09, 0xff),
	/*
	 * 0x08, VsmpsMHwHPReq1Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ1VALID2,	0x03, 0x0a, 0x08),
	/*
	 * 0x01, VsmpsAHwHPReq2Valid
	 * 0x02, VsmpsBHwHPReq2Valid
	 * 0x04, VsafeHwHPReq2Valid
	 * 0x08, VanaHwHPReq2Valid
	 * 0x10, VpllHwHPReq2Valid
	 * 0x20, Vaux1HwHPReq2Valid
	 * 0x40, Vaux2HwHPReq2Valid
	 * 0x80, Vaux3HwHPReq2Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ2VALID1,	0x03, 0x0b, 0xff),
	/*
	 * 0x08, VsmpsMHwHPReq2Valid
	 */
	REG_INIT(AB8505_REGUHWHPREQ2VALID2,	0x03, 0x0c, 0x08),
	/*
	 * 0x01, VsmpsCSwHPReqValid
	 * 0x02, VarmSwHPReqValid
	 * 0x04, VsmpsASwHPReqValid
	 * 0x08, VsmpsBSwHPReqValid
	 * 0x10, VsafeSwHPReqValid
	 * 0x20, VanaSwHPReqValid
	 * 0x40, VpllSwHPReqValid
	 * 0x80, Vaux1SwHPReqValid
	 */
	REG_INIT(AB8505_REGUSWHPREQVALID1,	0x03, 0x0d, 0xff),
	/*
	 * 0x01, Vaux2SwHPReqValid
	 * 0x02, Vaux3SwHPReqValid
	 * 0x20, VsmpsMSwHPReqValid
	 */
	REG_INIT(AB8505_REGUSWHPREQVALID2,	0x03, 0x0e, 0x23),
	/*
	 * 0x02, SysClkReq2Valid1
	 * 0x04, SysClkReq3Valid1
	 * 0x08, SysClkReq4Valid1
	 */
	REG_INIT(AB8505_REGUSYSCLKREQVALID1,	0x03, 0x0f, 0x0e),
	/*
	 * 0x02, SysClkReq2Valid2
	 * 0x04, SysClkReq3Valid2
	 * 0x08, SysClkReq4Valid2
	 */
	REG_INIT(AB8505_REGUSYSCLKREQVALID2,	0x03, 0x10, 0x0e),
	/*
	 * 0x01, Vaux4SwHPReqValid
	 * 0x02, Vaux4HwHPReq2Valid
	 * 0x04, Vaux4HwHPReq1Valid
	 * 0x08, Vaux4SysClkReq1HPValid
	 */
	REG_INIT(AB8505_REGUVAUX4REQVALID,	0x03, 0x11, 0x0f),
	/*
	 * 0x02, VadcEna
	 * 0x04, VintCore12Ena
	 * 0x38, VintCore12Sel
	 * 0x40, VintCore12LP
	 * 0x80, VadcLP
	 */
	REG_INIT(AB8505_REGUMISC1,		0x03, 0x80, 0xfe),
	/*
	 * 0x02, VaudioEna
	 * 0x04, VdmicEna
	 * 0x08, Vamic1Ena
	 * 0x10, Vamic2Ena
	 */
	REG_INIT(AB8505_VAUDIOSUPPLY,		0x03, 0x83, 0x1e),
	/*
	 * 0x01, Vamic1_dzout
	 * 0x02, Vamic2_dzout
	 */
	REG_INIT(AB8505_REGUCTRL1VAMIC,		0x03, 0x84, 0x03),
	/*
	 * 0x03, VsmpsARegu
	 * 0x0c, VsmpsASelCtrl
	 * 0x10, VsmpsAAutoMode
	 * 0x20, VsmpsAPWMMode
	 */
	REG_INIT(AB8505_VSMPSAREGU,		0x04, 0x03, 0x3f),
	/*
	 * 0x03, VsmpsBRegu
	 * 0x0c, VsmpsBSelCtrl
	 * 0x10, VsmpsBAutoMode
	 * 0x20, VsmpsBPWMMode
	 */
	REG_INIT(AB8505_VSMPSBREGU,		0x04, 0x04, 0x3f),
	/*
	 * 0x03, VsafeRegu
	 * 0x0c, VsafeSelCtrl
	 * 0x10, VsafeAutoMode
	 * 0x20, VsafePWMMode
	 */
	REG_INIT(AB8505_VSAFEREGU,		0x04, 0x05, 0x3f),
	/*
	 * 0x03, VpllRegu (NOTE! PRCMU register bits)
	 * 0x0c, VanaRegu
	 */
	REG_INIT(AB8505_VPLLVANAREGU,		0x04, 0x06, 0x0f),
	/*
	 * 0x03, VextSupply1Regu
	 * 0x0c, VextSupply2Regu
	 * 0x30, VextSupply3Regu
	 * 0x40, ExtSupply2Bypass
	 * 0x80, ExtSupply3Bypass
	 */
	REG_INIT(AB8505_EXTSUPPLYREGU,		0x04, 0x08, 0xff),
	/*
	 * 0x03, Vaux1Regu
	 * 0x0c, Vaux2Regu
	 */
	REG_INIT(AB8505_VAUX12REGU,		0x04, 0x09, 0x0f),
	/*
	 * 0x0f, Vaux3Regu
	 */
	REG_INIT(AB8505_VRF1VAUX3REGU,		0x04, 0x0a, 0x0f),
	/*
	 * 0x3f, VsmpsASel1
	 */
	REG_INIT(AB8505_VSMPSASEL1,		0x04, 0x13, 0x3f),
	/*
	 * 0x3f, VsmpsASel2
	 */
	REG_INIT(AB8505_VSMPSASEL2,		0x04, 0x14, 0x3f),
	/*
	 * 0x3f, VsmpsASel3
	 */
	REG_INIT(AB8505_VSMPSASEL3,		0x04, 0x15, 0x3f),
	/*
	 * 0x3f, VsmpsBSel1
	 */
	REG_INIT(AB8505_VSMPSBSEL1,		0x04, 0x17, 0x3f),
	/*
	 * 0x3f, VsmpsBSel2
	 */
	REG_INIT(AB8505_VSMPSBSEL2,		0x04, 0x18, 0x3f),
	/*
	 * 0x3f, VsmpsBSel3
	 */
	REG_INIT(AB8505_VSMPSBSEL3,		0x04, 0x19, 0x3f),
	/*
	 * 0x7f, VsafeSel1
	 */
	REG_INIT(AB8505_VSAFESEL1,		0x04, 0x1b, 0x7f),
	/*
	 * 0x3f, VsafeSel2
	 */
	REG_INIT(AB8505_VSAFESEL2,		0x04, 0x1c, 0x7f),
	/*
	 * 0x3f, VsafeSel3
	 */
	REG_INIT(AB8505_VSAFESEL3,		0x04, 0x1d, 0x7f),
	/*
	 * 0x0f, Vaux1Sel
	 */
	REG_INIT(AB8505_VAUX1SEL,		0x04, 0x1f, 0x0f),
	/*
	 * 0x0f, Vaux2Sel
	 */
	REG_INIT(AB8505_VAUX2SEL,		0x04, 0x20, 0x0f),
	/*
	 * 0x07, Vaux3Sel
	 * 0x30, VRF1Sel
	 */
	REG_INIT(AB8505_VRF1VAUX3SEL,		0x04, 0x21, 0x37),
	/*
	 * 0x03, Vaux4RequestCtrl
	 */
	REG_INIT(AB8505_VAUX4REQCTRL,		0x04, 0x2d, 0x03),
	/*
	 * 0x03, Vaux4Regu
	 */
	REG_INIT(AB8505_VAUX4REGU,		0x04, 0x2e, 0x03),
	/*
	 * 0x0f, Vaux4Sel
	 */
	REG_INIT(AB8505_VAUX4SEL,		0x04, 0x2f, 0x0f),
	/*
	 * 0x04, Vaux1Disch
	 * 0x08, Vaux2Disch
	 * 0x10, Vaux3Disch
	 * 0x20, Vintcore12Disch
	 * 0x40, VTVoutDisch
	 * 0x80, VaudioDisch
	 */
	REG_INIT(AB8505_REGUCTRLDISCH,		0x04, 0x43, 0xfc),
	/*
	 * 0x02, VanaDisch
	 * 0x04, VdmicPullDownEna
	 * 0x10, VdmicDisch
	 */
	REG_INIT(AB8505_REGUCTRLDISCH2,		0x04, 0x44, 0x16),
	/*
	 * 0x01, Vaux4Disch
	 */
	REG_INIT(AB8505_REGUCTRLDISCH3,		0x04, 0x48, 0x01),
	/*
	 * 0x07, Vaux5Sel
	 * 0x08, Vaux5LP
	 * 0x10, Vaux5Ena
	 * 0x20, Vaux5Disch
	 * 0x40, Vaux5DisSfst
	 * 0x80, Vaux5DisPulld
	 */
	REG_INIT(AB8505_CTRLVAUX5,		0x01, 0x55, 0xff),
	/*
	 * 0x07, Vaux6Sel
	 * 0x08, Vaux6LP
	 * 0x10, Vaux6Ena
	 * 0x80, Vaux6DisPulld
	 */
	REG_INIT(AB8505_CTRLVAUX6,		0x01, 0x56, 0x9f),
};

/* AB9540 register init */
static struct ab8500_reg_init ab9540_reg_init[] = {
	/*
	 * 0x03, VarmRequestCtrl
	 * 0x0c, VapeRequestCtrl
	 * 0x30, Vsmps1RequestCtrl
	 * 0xc0, Vsmps2RequestCtrl
	 */
	REG_INIT(AB9540_REGUREQUESTCTRL1,	0x03, 0x03, 0xff),
	/*
	 * 0x03, Vsmps3RequestCtrl
	 * 0x0c, VpllRequestCtrl
	 * 0x30, VanaRequestCtrl
	 * 0xc0, VextSupply1RequestCtrl
	 */
	REG_INIT(AB9540_REGUREQUESTCTRL2,	0x03, 0x04, 0xff),
	/*
	 * 0x03, VextSupply2RequestCtrl
	 * 0x0c, VextSupply3RequestCtrl
	 * 0x30, Vaux1RequestCtrl
	 * 0xc0, Vaux2RequestCtrl
	 */
	REG_INIT(AB9540_REGUREQUESTCTRL3,	0x03, 0x05, 0xff),
	/*
	 * 0x03, Vaux3RequestCtrl
	 * 0x04, SwHPReq
	 */
	REG_INIT(AB9540_REGUREQUESTCTRL4,	0x03, 0x06, 0x07),
	/*
	 * 0x01, Vsmps1SysClkReq1HPValid
	 * 0x02, Vsmps2SysClkReq1HPValid
	 * 0x04, Vsmps3SysClkReq1HPValid
	 * 0x08, VanaSysClkReq1HPValid
	 * 0x10, VpllSysClkReq1HPValid
	 * 0x20, Vaux1SysClkReq1HPValid
	 * 0x40, Vaux2SysClkReq1HPValid
	 * 0x80, Vaux3SysClkReq1HPValid
	 */
	REG_INIT(AB9540_REGUSYSCLKREQ1HPVALID1,	0x03, 0x07, 0xff),
	/*
	 * 0x01, VapeSysClkReq1HPValid
	 * 0x02, VarmSysClkReq1HPValid
	 * 0x04, VbbSysClkReq1HPValid
	 * 0x08, VmodSysClkReq1HPValid
	 * 0x10, VextSupply1SysClkReq1HPValid
	 * 0x20, VextSupply2SysClkReq1HPValid
	 * 0x40, VextSupply3SysClkReq1HPValid
	 */
	REG_INIT(AB9540_REGUSYSCLKREQ1HPVALID2,	0x03, 0x08, 0x7f),
	/*
	 * 0x01, Vsmps1HwHPReq1Valid
	 * 0x02, Vsmps2HwHPReq1Valid
	 * 0x04, Vsmps3HwHPReq1Valid
	 * 0x08, VanaHwHPReq1Valid
	 * 0x10, VpllHwHPReq1Valid
	 * 0x20, Vaux1HwHPReq1Valid
	 * 0x40, Vaux2HwHPReq1Valid
	 * 0x80, Vaux3HwHPReq1Valid
	 */
	REG_INIT(AB9540_REGUHWHPREQ1VALID1,	0x03, 0x09, 0xff),
	/*
	 * 0x01, VextSupply1HwHPReq1Valid
	 * 0x02, VextSupply2HwHPReq1Valid
	 * 0x04, VextSupply3HwHPReq1Valid
	 * 0x08, VmodHwHPReq1Valid
	 */
	REG_INIT(AB9540_REGUHWHPREQ1VALID2,	0x03, 0x0a, 0x0f),
	/*
	 * 0x01, Vsmps1HwHPReq2Valid
	 * 0x02, Vsmps2HwHPReq2Valid
	 * 0x03, Vsmps3HwHPReq2Valid
	 * 0x08, VanaHwHPReq2Valid
	 * 0x10, VpllHwHPReq2Valid
	 * 0x20, Vaux1HwHPReq2Valid
	 * 0x40, Vaux2HwHPReq2Valid
	 * 0x80, Vaux3HwHPReq2Valid
	 */
	REG_INIT(AB9540_REGUHWHPREQ2VALID1,	0x03, 0x0b, 0xff),
	/*
	 * 0x01, VextSupply1HwHPReq2Valid
	 * 0x02, VextSupply2HwHPReq2Valid
	 * 0x04, VextSupply3HwHPReq2Valid
	 * 0x08, VmodHwHPReq2Valid
	 */
	REG_INIT(AB9540_REGUHWHPREQ2VALID2,	0x03, 0x0c, 0x0f),
	/*
	 * 0x01, VapeSwHPReqValid
	 * 0x02, VarmSwHPReqValid
	 * 0x04, Vsmps1SwHPReqValid
	 * 0x08, Vsmps2SwHPReqValid
	 * 0x10, Vsmps3SwHPReqValid
	 * 0x20, VanaSwHPReqValid
	 * 0x40, VpllSwHPReqValid
	 * 0x80, Vaux1SwHPReqValid
	 */
	REG_INIT(AB9540_REGUSWHPREQVALID1,	0x03, 0x0d, 0xff),
	/*
	 * 0x01, Vaux2SwHPReqValid
	 * 0x02, Vaux3SwHPReqValid
	 * 0x04, VextSupply1SwHPReqValid
	 * 0x08, VextSupply2SwHPReqValid
	 * 0x10, VextSupply3SwHPReqValid
	 * 0x20, VmodSwHPReqValid
	 */
	REG_INIT(AB9540_REGUSWHPREQVALID2,	0x03, 0x0e, 0x3f),
	/*
	 * 0x02, SysClkReq2Valid1
	 * ...
	 * 0x80, SysClkReq8Valid1
	 */
	REG_INIT(AB9540_REGUSYSCLKREQVALID1,	0x03, 0x0f, 0xfe),
	/*
	 * 0x02, SysClkReq2Valid2
	 * ...
	 * 0x80, SysClkReq8Valid2
	 */
	REG_INIT(AB9540_REGUSYSCLKREQVALID2,	0x03, 0x10, 0xfe),
	/*
	 * 0x01, Vaux4SwHPReqValid
	 * 0x02, Vaux4HwHPReq2Valid
	 * 0x04, Vaux4HwHPReq1Valid
	 * 0x08, Vaux4SysClkReq1HPValid
	 */
	REG_INIT(AB9540_REGUVAUX4REQVALID,	0x03, 0x11, 0x0f),
	/*
	 * 0x02, VTVoutEna
	 * 0x04, Vintcore12Ena
	 * 0x38, Vintcore12Sel
	 * 0x40, Vintcore12LP
	 * 0x80, VTVoutLP
	 */
	REG_INIT(AB9540_REGUMISC1,		0x03, 0x80, 0xfe),
	/*
	 * 0x02, VaudioEna
	 * 0x04, VdmicEna
	 * 0x08, Vamic1Ena
	 * 0x10, Vamic2Ena
	 */
	REG_INIT(AB9540_VAUDIOSUPPLY,		0x03, 0x83, 0x1e),
	/*
	 * 0x01, Vamic1_dzout
	 * 0x02, Vamic2_dzout
	 */
	REG_INIT(AB9540_REGUCTRL1VAMIC,		0x03, 0x84, 0x03),
	/*
	 * 0x03, Vsmps1Regu
	 * 0x0c, Vsmps1SelCtrl
	 * 0x10, Vsmps1AutoMode
	 * 0x20, Vsmps1PWMMode
	 */
	REG_INIT(AB9540_VSMPS1REGU,		0x04, 0x03, 0x3f),
	/*
	 * 0x03, Vsmps2Regu
	 * 0x0c, Vsmps2SelCtrl
	 * 0x10, Vsmps2AutoMode
	 * 0x20, Vsmps2PWMMode
	 */
	REG_INIT(AB9540_VSMPS2REGU,		0x04, 0x04, 0x3f),
	/*
	 * 0x03, Vsmps3Regu
	 * 0x0c, Vsmps3SelCtrl
	 * NOTE! PRCMU register
	 */
	REG_INIT(AB9540_VSMPS3REGU,		0x04, 0x05, 0x0f),
	/*
	 * 0x03, VpllRegu
	 * 0x0c, VanaRegu
	 */
	REG_INIT(AB9540_VPLLVANAREGU,		0x04, 0x06, 0x0f),
	/*
	 * 0x03, VextSupply1Regu
	 * 0x0c, VextSupply2Regu
	 * 0x30, VextSupply3Regu
	 * 0x40, ExtSupply2Bypass
	 * 0x80, ExtSupply3Bypass
	 */
	REG_INIT(AB9540_EXTSUPPLYREGU,		0x04, 0x08, 0xff),
	/*
	 * 0x03, Vaux1Regu
	 * 0x0c, Vaux2Regu
	 */
	REG_INIT(AB9540_VAUX12REGU,		0x04, 0x09, 0x0f),
	/*
	 * 0x0c, Vrf1Regu
	 * 0x03, Vaux3Regu
	 */
	REG_INIT(AB9540_VRF1VAUX3REGU,		0x04, 0x0a, 0x0f),
	/*
	 * 0x3f, Vsmps1Sel1
	 */
	REG_INIT(AB9540_VSMPS1SEL1,		0x04, 0x13, 0x3f),
	/*
	 * 0x3f, Vsmps1Sel2
	 */
	REG_INIT(AB9540_VSMPS1SEL2,		0x04, 0x14, 0x3f),
	/*
	 * 0x3f, Vsmps1Sel3
	 */
	REG_INIT(AB9540_VSMPS1SEL3,		0x04, 0x15, 0x3f),
	/*
	 * 0x3f, Vsmps2Sel1
	 */
	REG_INIT(AB9540_VSMPS2SEL1,		0x04, 0x17, 0x3f),
	/*
	 * 0x3f, Vsmps2Sel2
	 */
	REG_INIT(AB9540_VSMPS2SEL2,		0x04, 0x18, 0x3f),
	/*
	 * 0x3f, Vsmps2Sel3
	 */
	REG_INIT(AB9540_VSMPS2SEL3,		0x04, 0x19, 0x3f),
	/*
	 * 0x7f, Vsmps3Sel1
	 * NOTE! PRCMU register
	 */
	REG_INIT(AB9540_VSMPS3SEL1,             0x04, 0x1b, 0x7f),
	/*
	 * 0x7f, Vsmps3Sel2
	 * NOTE! PRCMU register
	 */
	REG_INIT(AB9540_VSMPS3SEL2,             0x04, 0x1c, 0x7f),
	/*
	 * 0x0f, Vaux1Sel
	 */
	REG_INIT(AB9540_VAUX1SEL,		0x04, 0x1f, 0x0f),
	/*
	 * 0x0f, Vaux2Sel
	 */
	REG_INIT(AB9540_VAUX2SEL,		0x04, 0x20, 0x0f),
	/*
	 * 0x07, Vaux3Sel
	 * 0x30, Vrf1Sel
	 */
	REG_INIT(AB9540_VRF1VAUX3SEL,		0x04, 0x21, 0x37),
	/*
	 * 0x01, VextSupply12LP
	 */
	REG_INIT(AB9540_REGUCTRL2SPARE,		0x04, 0x22, 0x01),
	/*
	 * 0x03, Vaux4RequestCtrl
	 */
	REG_INIT(AB9540_VAUX4REQCTRL,		0x04, 0x2d, 0x03),
	/*
	 * 0x03, Vaux4Regu
	 */
	REG_INIT(AB9540_VAUX4REGU,		0x04, 0x2e, 0x03),
	/*
	 * 0x08, Vaux4Sel
	 */
	REG_INIT(AB9540_VAUX4SEL,		0x04, 0x2f, 0x0f),
	/*
	 * 0x01, VpllDisch
	 * 0x02, Vrf1Disch
	 * 0x04, Vaux1Disch
	 * 0x08, Vaux2Disch
	 * 0x10, Vaux3Disch
	 * 0x20, Vintcore12Disch
	 * 0x40, VTVoutDisch
	 * 0x80, VaudioDisch
	 */
	REG_INIT(AB9540_REGUCTRLDISCH,		0x04, 0x43, 0xff),
	/*
	 * 0x01, VsimDisch
	 * 0x02, VanaDisch
	 * 0x04, VdmicPullDownEna
	 * 0x08, VpllPullDownEna
	 * 0x10, VdmicDisch
	 */
	REG_INIT(AB9540_REGUCTRLDISCH2,		0x04, 0x44, 0x1f),
	/*
	 * 0x01, Vaux4Disch
	 */
	REG_INIT(AB9540_REGUCTRLDISCH3,		0x04, 0x48, 0x01),
};

/* AB8540 register init */
static struct ab8500_reg_init ab8540_reg_init[] = {
	/*
	 * 0x01, VSimSycClkReq1Valid
	 * 0x02, VSimSycClkReq2Valid
	 * 0x04, VSimSycClkReq3Valid
	 * 0x08, VSimSycClkReq4Valid
	 * 0x10, VSimSycClkReq5Valid
	 * 0x20, VSimSycClkReq6Valid
	 * 0x40, VSimSycClkReq7Valid
	 * 0x80, VSimSycClkReq8Valid
	 */
	REG_INIT(AB8540_VSIMSYSCLKCTRL,		0x02, 0x33, 0xff),
	/*
	 * 0x03, VarmRequestCtrl
	 * 0x0c, VapeRequestCtrl
	 * 0x30, Vsmps1RequestCtrl
	 * 0xc0, Vsmps2RequestCtrl
	 */
	REG_INIT(AB8540_REGUREQUESTCTRL1,	0x03, 0x03, 0xff),
	/*
	 * 0x03, Vsmps3RequestCtrl
	 * 0x0c, VpllRequestCtrl
	 * 0x30, VanaRequestCtrl
	 * 0xc0, VextSupply1RequestCtrl
	 */
	REG_INIT(AB8540_REGUREQUESTCTRL2,	0x03, 0x04, 0xff),
	/*
	 * 0x03, VextSupply2RequestCtrl
	 * 0x0c, VextSupply3RequestCtrl
	 * 0x30, Vaux1RequestCtrl
	 * 0xc0, Vaux2RequestCtrl
	 */
	REG_INIT(AB8540_REGUREQUESTCTRL3,	0x03, 0x05, 0xff),
	/*
	 * 0x03, Vaux3RequestCtrl
	 * 0x04, SwHPReq
	 */
	REG_INIT(AB8540_REGUREQUESTCTRL4,	0x03, 0x06, 0x07),
	/*
	 * 0x01, Vsmps1SysClkReq1HPValid
	 * 0x02, Vsmps2SysClkReq1HPValid
	 * 0x04, Vsmps3SysClkReq1HPValid
	 * 0x08, VanaSysClkReq1HPValid
	 * 0x10, VpllSysClkReq1HPValid
	 * 0x20, Vaux1SysClkReq1HPValid
	 * 0x40, Vaux2SysClkReq1HPValid
	 * 0x80, Vaux3SysClkReq1HPValid
	 */
	REG_INIT(AB8540_REGUSYSCLKREQ1HPVALID1,	0x03, 0x07, 0xff),
	/*
	 * 0x01, VapeSysClkReq1HPValid
	 * 0x02, VarmSysClkReq1HPValid
	 * 0x04, VbbSysClkReq1HPValid
	 * 0x10, VextSupply1SysClkReq1HPValid
	 * 0x20, VextSupply2SysClkReq1HPValid
	 * 0x40, VextSupply3SysClkReq1HPValid
	 */
	REG_INIT(AB8540_REGUSYSCLKREQ1HPVALID2,	0x03, 0x08, 0x77),
	/*
	 * 0x01, Vsmps1HwHPReq1Valid
	 * 0x02, Vsmps2HwHPReq1Valid
	 * 0x04, Vsmps3HwHPReq1Valid
	 * 0x08, VanaHwHPReq1Valid
	 * 0x10, VpllHwHPReq1Valid
	 * 0x20, Vaux1HwHPReq1Valid
	 * 0x40, Vaux2HwHPReq1Valid
	 * 0x80, Vaux3HwHPReq1Valid
	 */
	REG_INIT(AB8540_REGUHWHPREQ1VALID1,	0x03, 0x09, 0xff),
	/*
	 * 0x01, VextSupply1HwHPReq1Valid
	 * 0x02, VextSupply2HwHPReq1Valid
	 * 0x04, VextSupply3HwHPReq1Valid
	 */
	REG_INIT(AB8540_REGUHWHPREQ1VALID2,	0x03, 0x0a, 0x07),
	/*
	 * 0x01, Vsmps1HwHPReq2Valid
	 * 0x02, Vsmps2HwHPReq2Valid
	 * 0x03, Vsmps3HwHPReq2Valid
	 * 0x08, VanaHwHPReq2Valid
	 * 0x10, VpllHwHPReq2Valid
	 * 0x20, Vaux1HwHPReq2Valid
	 * 0x40, Vaux2HwHPReq2Valid
	 * 0x80, Vaux3HwHPReq2Valid
	 */
	REG_INIT(AB8540_REGUHWHPREQ2VALID1,	0x03, 0x0b, 0xff),
	/*
	 * 0x01, VextSupply1HwHPReq2Valid
	 * 0x02, VextSupply2HwHPReq2Valid
	 * 0x04, VextSupply3HwHPReq2Valid
	 */
	REG_INIT(AB8540_REGUHWHPREQ2VALID2,	0x03, 0x0c, 0x07),
	/*
	 * 0x01, VapeSwHPReqValid
	 * 0x02, VarmSwHPReqValid
	 * 0x04, Vsmps1SwHPReqValid
	 * 0x08, Vsmps2SwHPReqValid
	 * 0x10, Vsmps3SwHPReqValid
	 * 0x20, VanaSwHPReqValid
	 * 0x40, VpllSwHPReqValid
	 * 0x80, Vaux1SwHPReqValid
	 */
	REG_INIT(AB8540_REGUSWHPREQVALID1,	0x03, 0x0d, 0xff),
	/*
	 * 0x01, Vaux2SwHPReqValid
	 * 0x02, Vaux3SwHPReqValid
	 * 0x04, VextSupply1SwHPReqValid
	 * 0x08, VextSupply2SwHPReqValid
	 * 0x10, VextSupply3SwHPReqValid
	 */
	REG_INIT(AB8540_REGUSWHPREQVALID2,	0x03, 0x0e, 0x1f),
	/*
	 * 0x02, SysClkReq2Valid1
	 * ...
	 * 0x80, SysClkReq8Valid1
	 */
	REG_INIT(AB8540_REGUSYSCLKREQVALID1,	0x03, 0x0f, 0xff),
	/*
	 * 0x02, SysClkReq2Valid2
	 * ...
	 * 0x80, SysClkReq8Valid2
	 */
	REG_INIT(AB8540_REGUSYSCLKREQVALID2,	0x03, 0x10, 0xff),
	/*
	 * 0x01, Vaux4SwHPReqValid
	 * 0x02, Vaux4HwHPReq2Valid
	 * 0x04, Vaux4HwHPReq1Valid
	 * 0x08, Vaux4SysClkReq1HPValid
	 */
	REG_INIT(AB8540_REGUVAUX4REQVALID,	0x03, 0x11, 0x0f),
	/*
	 * 0x01, Vaux5SwHPReqValid
	 * 0x02, Vaux5HwHPReq2Valid
	 * 0x04, Vaux5HwHPReq1Valid
	 * 0x08, Vaux5SysClkReq1HPValid
	 */
	REG_INIT(AB8540_REGUVAUX5REQVALID,	0x03, 0x12, 0x0f),
	/*
	 * 0x01, Vaux6SwHPReqValid
	 * 0x02, Vaux6HwHPReq2Valid
	 * 0x04, Vaux6HwHPReq1Valid
	 * 0x08, Vaux6SysClkReq1HPValid
	 */
	REG_INIT(AB8540_REGUVAUX6REQVALID,	0x03, 0x13, 0x0f),
	/*
	 * 0x01, VclkbSwHPReqValid
	 * 0x02, VclkbHwHPReq2Valid
	 * 0x04, VclkbHwHPReq1Valid
	 * 0x08, VclkbSysClkReq1HPValid
	 */
	REG_INIT(AB8540_REGUVCLKBREQVALID,	0x03, 0x14, 0x0f),
	/*
	 * 0x01, Vrf1SwHPReqValid
	 * 0x02, Vrf1HwHPReq2Valid
	 * 0x04, Vrf1HwHPReq1Valid
	 * 0x08, Vrf1SysClkReq1HPValid
	 */
	REG_INIT(AB8540_REGUVRF1REQVALID,	0x03, 0x15, 0x0f),
	/*
	 * 0x02, VTVoutEna
	 * 0x04, Vintcore12Ena
	 * 0x38, Vintcore12Sel
	 * 0x40, Vintcore12LP
	 * 0x80, VTVoutLP
	 */
	REG_INIT(AB8540_REGUMISC1,		0x03, 0x80, 0xfe),
	/*
	 * 0x02, VaudioEna
	 * 0x04, VdmicEna
	 * 0x08, Vamic1Ena
	 * 0x10, Vamic2Ena
	 * 0x20, Vamic12LP
	 * 0xC0, VdmicSel
	 */
	REG_INIT(AB8540_VAUDIOSUPPLY,		0x03, 0x83, 0xfe),
	/*
	 * 0x01, Vamic1_dzout
	 * 0x02, Vamic2_dzout
	 */
	REG_INIT(AB8540_REGUCTRL1VAMIC,		0x03, 0x84, 0x03),
	/*
	 * 0x07, VHSICSel
	 * 0x08, VHSICOffState
	 * 0x10, VHSIEna
	 * 0x20, VHSICLP
	 */
	REG_INIT(AB8540_VHSIC,			0x03, 0x87, 0x3f),
	/*
	 * 0x07, VSDIOSel
	 * 0x08, VSDIOOffState
	 * 0x10, VSDIOEna
	 * 0x20, VSDIOLP
	 */
	REG_INIT(AB8540_VSDIO,			0x03, 0x88, 0x3f),
	/*
	 * 0x03, Vsmps1Regu
	 * 0x0c, Vsmps1SelCtrl
	 * 0x10, Vsmps1AutoMode
	 * 0x20, Vsmps1PWMMode
	 */
	REG_INIT(AB8540_VSMPS1REGU,		0x04, 0x03, 0x3f),
	/*
	 * 0x03, Vsmps2Regu
	 * 0x0c, Vsmps2SelCtrl
	 * 0x10, Vsmps2AutoMode
	 * 0x20, Vsmps2PWMMode
	 */
	REG_INIT(AB8540_VSMPS2REGU,		0x04, 0x04, 0x3f),
	/*
	 * 0x03, Vsmps3Regu
	 * 0x0c, Vsmps3SelCtrl
	 * 0x10, Vsmps3AutoMode
	 * 0x20, Vsmps3PWMMode
	 * NOTE! PRCMU register
	 */
	REG_INIT(AB8540_VSMPS3REGU,		0x04, 0x05, 0x0f),
	/*
	 * 0x03, VpllRegu
	 * 0x0c, VanaRegu
	 */
	REG_INIT(AB8540_VPLLVANAREGU,		0x04, 0x06, 0x0f),
	/*
	 * 0x03, VextSupply1Regu
	 * 0x0c, VextSupply2Regu
	 * 0x30, VextSupply3Regu
	 * 0x40, ExtSupply2Bypass
	 * 0x80, ExtSupply3Bypass
	 */
	REG_INIT(AB8540_EXTSUPPLYREGU,		0x04, 0x08, 0xff),
	/*
	 * 0x03, Vaux1Regu
	 * 0x0c, Vaux2Regu
	 */
	REG_INIT(AB8540_VAUX12REGU,		0x04, 0x09, 0x0f),
	/*
	 * 0x0c, VRF1Regu
	 * 0x03, Vaux3Regu
	 */
	REG_INIT(AB8540_VRF1VAUX3REGU,		0x04, 0x0a, 0x0f),
	/*
	 * 0x3f, Vsmps1Sel1
	 */
	REG_INIT(AB8540_VSMPS1SEL1,		0x04, 0x13, 0x3f),
	/*
	 * 0x3f, Vsmps1Sel2
	 */
	REG_INIT(AB8540_VSMPS1SEL2,		0x04, 0x14, 0x3f),
	/*
	 * 0x3f, Vsmps1Sel3
	 */
	REG_INIT(AB8540_VSMPS1SEL3,		0x04, 0x15, 0x3f),
	/*
	 * 0x3f, Vsmps2Sel1
	 */
	REG_INIT(AB8540_VSMPS2SEL1,		0x04, 0x17, 0x3f),
	/*
	 * 0x3f, Vsmps2Sel2
	 */
	REG_INIT(AB8540_VSMPS2SEL2,		0x04, 0x18, 0x3f),
	/*
	 * 0x3f, Vsmps2Sel3
	 */
	REG_INIT(AB8540_VSMPS2SEL3,		0x04, 0x19, 0x3f),
	/*
	 * 0x7f, Vsmps3Sel1
	 * NOTE! PRCMU register
	 */
	REG_INIT(AB8540_VSMPS3SEL1,             0x04, 0x1b, 0x7f),
	/*
	 * 0x7f, Vsmps3Sel2
	 * NOTE! PRCMU register
	 */
	REG_INIT(AB8540_VSMPS3SEL2,             0x04, 0x1c, 0x7f),
	/*
	 * 0x0f, Vaux1Sel
	 */
	REG_INIT(AB8540_VAUX1SEL,		0x04, 0x1f, 0x0f),
	/*
	 * 0x0f, Vaux2Sel
	 */
	REG_INIT(AB8540_VAUX2SEL,		0x04, 0x20, 0x0f),
	/*
	 * 0x07, Vaux3Sel
	 * 0x70, Vrf1Sel
	 */
	REG_INIT(AB8540_VRF1VAUX3SEL,		0x04, 0x21, 0x77),
	/*
	 * 0x01, VextSupply12LP
	 */
	REG_INIT(AB8540_REGUCTRL2SPARE,		0x04, 0x22, 0x01),
	/*
	 * 0x07, Vanasel
	 * 0x30, Vpllsel
	 */
	REG_INIT(AB8540_VANAVPLLSEL,		0x04, 0x29, 0x37),
	/*
	 * 0x03, Vaux4RequestCtrl
	 */
	REG_INIT(AB8540_VAUX4REQCTRL,		0x04, 0x2d, 0x03),
	/*
	 * 0x03, Vaux4Regu
	 */
	REG_INIT(AB8540_VAUX4REGU,		0x04, 0x2e, 0x03),
	/*
	 * 0x0f, Vaux4Sel
	 */
	REG_INIT(AB8540_VAUX4SEL,		0x04, 0x2f, 0x0f),
	/*
	 * 0x03, Vaux5RequestCtrl
	 */
	REG_INIT(AB8540_VAUX5REQCTRL,		0x04, 0x31, 0x03),
	/*
	 * 0x03, Vaux5Regu
	 */
	REG_INIT(AB8540_VAUX5REGU,		0x04, 0x32, 0x03),
	/*
	 * 0x3f, Vaux5Sel
	 */
	REG_INIT(AB8540_VAUX5SEL,		0x04, 0x33, 0x3f),
	/*
	 * 0x03, Vaux6RequestCtrl
	 */
	REG_INIT(AB8540_VAUX6REQCTRL,		0x04, 0x34, 0x03),
	/*
	 * 0x03, Vaux6Regu
	 */
	REG_INIT(AB8540_VAUX6REGU,		0x04, 0x35, 0x03),
	/*
	 * 0x3f, Vaux6Sel
	 */
	REG_INIT(AB8540_VAUX6SEL,		0x04, 0x36, 0x3f),
	/*
	 * 0x03, VCLKBRequestCtrl
	 */
	REG_INIT(AB8540_VCLKBREQCTRL,		0x04, 0x37, 0x03),
	/*
	 * 0x03, VCLKBRegu
	 */
	REG_INIT(AB8540_VCLKBREGU,		0x04, 0x38, 0x03),
	/*
	 * 0x07, VCLKBSel
	 */
	REG_INIT(AB8540_VCLKBSEL,		0x04, 0x39, 0x07),
	/*
	 * 0x03, Vrf1RequestCtrl
	 */
	REG_INIT(AB8540_VRF1REQCTRL,		0x04, 0x3a, 0x03),
	/*
	 * 0x01, VpllDisch
	 * 0x02, Vrf1Disch
	 * 0x04, Vaux1Disch
	 * 0x08, Vaux2Disch
	 * 0x10, Vaux3Disch
	 * 0x20, Vintcore12Disch
	 * 0x40, VTVoutDisch
	 * 0x80, VaudioDisch
	 */
	REG_INIT(AB8540_REGUCTRLDISCH,		0x04, 0x43, 0xff),
	/*
	 * 0x02, VanaDisch
	 * 0x04, VdmicPullDownEna
	 * 0x08, VpllPullDownEna
	 * 0x10, VdmicDisch
	 */
	REG_INIT(AB8540_REGUCTRLDISCH2,		0x04, 0x44, 0x1e),
	/*
	 * 0x01, Vaux4Disch
	 */
	REG_INIT(AB8540_REGUCTRLDISCH3,		0x04, 0x48, 0x01),
	/*
	 * 0x01, Vaux5Disch
	 * 0x02, Vaux6Disch
	 * 0x04, VCLKBDisch
	 */
	REG_INIT(AB8540_REGUCTRLDISCH4,		0x04, 0x49, 0x07),
};

static int ab8500_regulator_init_registers(struct platform_device *pdev,
					   struct ab8500_reg_init *reg_init,
					   int id, int mask, int value)
{
	int err;

	BUG_ON(value & ~mask);
	BUG_ON(mask & ~reg_init[id].mask);

	/* initialize register */
	err = abx500_mask_and_set_register_interruptible(
		&pdev->dev,
		reg_init[id].bank,
		reg_init[id].addr,
		mask, value);
	if (err < 0) {
		dev_err(&pdev->dev,
			"Failed to initialize 0x%02x, 0x%02x.\n",
			reg_init[id].bank,
			reg_init[id].addr);
		return err;
	}
	dev_vdbg(&pdev->dev,
		 "  init: 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
		 reg_init[id].bank,
		 reg_init[id].addr,
		 mask, value);

	return 0;
}

static int ab8500_regulator_register(struct platform_device *pdev,
				     struct regulator_init_data *init_data,
				     struct ab8500_regulator_info *regulator_info,
				     int id, struct device_node *np)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct ab8500_regulator_info *info = NULL;
	struct regulator_config config = { };
	int err;

	/* assign per-regulator data */
	info = &regulator_info[id];
	info->dev = &pdev->dev;

	config.dev = &pdev->dev;
	config.init_data = init_data;
	config.driver_data = info;
	config.of_node = np;

	/* fix for hardware before ab8500v2.0 */
	if (is_ab8500_1p1_or_earlier(ab8500)) {
		if (info->desc.id == AB8500_LDO_AUX3) {
			info->desc.n_voltages =
				ARRAY_SIZE(ldo_vauxn_voltages);
			info->desc.volt_table = ldo_vauxn_voltages;
			info->voltage_mask = 0xf;
		}
	}

	/* register regulator with framework */
	info->regulator = regulator_register(&info->desc, &config);
	if (IS_ERR(info->regulator)) {
		err = PTR_ERR(info->regulator);
		dev_err(&pdev->dev, "failed to register regulator %s\n",
			info->desc.name);
		/* when we fail, un-register all earlier regulators */
		while (--id >= 0) {
			info = &regulator_info[id];
			regulator_unregister(info->regulator);
		}
		return err;
	}

	return 0;
}

static struct of_regulator_match ab8500_regulator_match[] = {
	{ .name	= "ab8500_ldo_aux1",    .driver_data = (void *) AB8500_LDO_AUX1, },
	{ .name	= "ab8500_ldo_aux2",    .driver_data = (void *) AB8500_LDO_AUX2, },
	{ .name	= "ab8500_ldo_aux3",    .driver_data = (void *) AB8500_LDO_AUX3, },
	{ .name	= "ab8500_ldo_intcore", .driver_data = (void *) AB8500_LDO_INTCORE, },
	{ .name	= "ab8500_ldo_tvout",   .driver_data = (void *) AB8500_LDO_TVOUT, },
	{ .name = "ab8500_ldo_audio",   .driver_data = (void *) AB8500_LDO_AUDIO, },
	{ .name	= "ab8500_ldo_anamic1", .driver_data = (void *) AB8500_LDO_ANAMIC1, },
	{ .name	= "ab8500_ldo_amamic2", .driver_data = (void *) AB8500_LDO_ANAMIC2, },
	{ .name	= "ab8500_ldo_dmic",    .driver_data = (void *) AB8500_LDO_DMIC, },
	{ .name	= "ab8500_ldo_ana",     .driver_data = (void *) AB8500_LDO_ANA, },
};

static struct of_regulator_match ab8505_regulator_match[] = {
	{ .name	= "ab8500_ldo_aux1",    .driver_data = (void *) AB8505_LDO_AUX1, },
	{ .name	= "ab8500_ldo_aux2",    .driver_data = (void *) AB8505_LDO_AUX2, },
	{ .name	= "ab8500_ldo_aux3",    .driver_data = (void *) AB8505_LDO_AUX3, },
	{ .name	= "ab8500_ldo_aux4",    .driver_data = (void *) AB8505_LDO_AUX4, },
	{ .name	= "ab8500_ldo_aux5",    .driver_data = (void *) AB8505_LDO_AUX5, },
	{ .name	= "ab8500_ldo_aux6",    .driver_data = (void *) AB8505_LDO_AUX6, },
	{ .name	= "ab8500_ldo_intcore", .driver_data = (void *) AB8505_LDO_INTCORE, },
	{ .name	= "ab8500_ldo_adc",	.driver_data = (void *) AB8505_LDO_ADC, },
	{ .name = "ab8500_ldo_audio",   .driver_data = (void *) AB8505_LDO_AUDIO, },
	{ .name	= "ab8500_ldo_anamic1", .driver_data = (void *) AB8505_LDO_ANAMIC1, },
	{ .name	= "ab8500_ldo_amamic2", .driver_data = (void *) AB8505_LDO_ANAMIC2, },
	{ .name	= "ab8500_ldo_aux8",    .driver_data = (void *) AB8505_LDO_AUX8, },
	{ .name	= "ab8500_ldo_ana",     .driver_data = (void *) AB8505_LDO_ANA, },
};

static struct of_regulator_match ab8540_regulator_match[] = {
	{ .name	= "ab8500_ldo_aux1",    .driver_data = (void *) AB8540_LDO_AUX1, },
	{ .name	= "ab8500_ldo_aux2",    .driver_data = (void *) AB8540_LDO_AUX2, },
	{ .name	= "ab8500_ldo_aux3",    .driver_data = (void *) AB8540_LDO_AUX3, },
	{ .name	= "ab8500_ldo_aux4",    .driver_data = (void *) AB8540_LDO_AUX4, },
	{ .name	= "ab8500_ldo_intcore", .driver_data = (void *) AB8540_LDO_INTCORE, },
	{ .name	= "ab8500_ldo_tvout",   .driver_data = (void *) AB8540_LDO_TVOUT, },
	{ .name = "ab8500_ldo_audio",   .driver_data = (void *) AB8540_LDO_AUDIO, },
	{ .name	= "ab8500_ldo_anamic1", .driver_data = (void *) AB8540_LDO_ANAMIC1, },
	{ .name	= "ab8500_ldo_amamic2", .driver_data = (void *) AB8540_LDO_ANAMIC2, },
	{ .name	= "ab8500_ldo_dmic",    .driver_data = (void *) AB8540_LDO_DMIC, },
	{ .name	= "ab8500_ldo_ana",     .driver_data = (void *) AB8540_LDO_ANA, },
	{ .name = "ab8500_ldo_sdio",    .driver_data = (void *) AB8540_LDO_SDIO, },
};

static struct of_regulator_match ab9540_regulator_match[] = {
	{ .name	= "ab8500_ldo_aux1",    .driver_data = (void *) AB9540_LDO_AUX1, },
	{ .name	= "ab8500_ldo_aux2",    .driver_data = (void *) AB9540_LDO_AUX2, },
	{ .name	= "ab8500_ldo_aux3",    .driver_data = (void *) AB9540_LDO_AUX3, },
	{ .name	= "ab8500_ldo_aux4",    .driver_data = (void *) AB9540_LDO_AUX4, },
	{ .name	= "ab8500_ldo_intcore", .driver_data = (void *) AB9540_LDO_INTCORE, },
	{ .name	= "ab8500_ldo_tvout",   .driver_data = (void *) AB9540_LDO_TVOUT, },
	{ .name = "ab8500_ldo_audio",   .driver_data = (void *) AB9540_LDO_AUDIO, },
	{ .name	= "ab8500_ldo_anamic1", .driver_data = (void *) AB9540_LDO_ANAMIC1, },
	{ .name	= "ab8500_ldo_amamic2", .driver_data = (void *) AB9540_LDO_ANAMIC2, },
	{ .name	= "ab8500_ldo_dmic",    .driver_data = (void *) AB9540_LDO_DMIC, },
	{ .name	= "ab8500_ldo_ana",     .driver_data = (void *) AB9540_LDO_ANA, },
};

static int
ab8500_regulator_of_probe(struct platform_device *pdev,
			  struct ab8500_regulator_info *regulator_info,
			  int regulator_info_size,
			  struct of_regulator_match *match,
			  struct device_node *np)
{
	int err, i;

	for (i = 0; i < regulator_info_size; i++) {
		err = ab8500_regulator_register(
			pdev, match[i].init_data, regulator_info,
			i, match[i].of_node);
		if (err)
			return err;
	}

	return 0;
}

static int ab8500_regulator_probe(struct platform_device *pdev)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.of_node;
	struct of_regulator_match *match;
	struct ab8500_platform_data *ppdata;
	struct ab8500_regulator_platform_data *pdata;
	int i, err;
	struct ab8500_regulator_info *regulator_info;
	int regulator_info_size;
	struct ab8500_reg_init *reg_init;
	int reg_init_size;

	if (is_ab9540(ab8500)) {
		regulator_info = ab9540_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab9540_regulator_info);
		reg_init = ab9540_reg_init;
		reg_init_size = AB9540_NUM_REGULATOR_REGISTERS;
		match = ab9540_regulator_match;
		match_size = ARRAY_SIZE(ab9540_regulator_match)
	} else if (is_ab8505(ab8500)) {
		regulator_info = ab8505_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab8505_regulator_info);
		reg_init = ab8505_reg_init;
		reg_init_size = AB8505_NUM_REGULATOR_REGISTERS;
	} else if (is_ab8540(ab8500)) {
		regulator_info = ab8540_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab8540_regulator_info);
		reg_init = ab8540_reg_init;
		reg_init_size = AB8540_NUM_REGULATOR_REGISTERS;
	} else {
		regulator_info = ab8500_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab8500_regulator_info);
		reg_init = ab8500_reg_init;
		reg_init_size = AB8500_NUM_REGULATOR_REGISTERS;
		match = ab8500_regulator_match;
		match_size = ARRAY_SIZE(ab8500_regulator_match)
	}

	if (np) {
		err = of_regulator_match(&pdev->dev, np, match, match_size);
		if (err < 0) {
			dev_err(&pdev->dev,
				"Error parsing regulator init data: %d\n", err);
			return err;
		}

		err = ab8500_regulator_of_probe(pdev, regulator_info,
						regulator_info_size, match, np);
		return err;
	}

	if (!ab8500) {
		dev_err(&pdev->dev, "null mfd parent\n");
		return -EINVAL;
	}

	ppdata = dev_get_platdata(ab8500->dev);
	if (!ppdata) {
		dev_err(&pdev->dev, "null parent pdata\n");
		return -EINVAL;
	}

	pdata = ppdata->regulator;
	if (!pdata) {
		dev_err(&pdev->dev, "null pdata\n");
		return -EINVAL;
	}

	/* make sure the platform data has the correct size */
	if (pdata->num_regulator != regulator_info_size) {
		dev_err(&pdev->dev, "Configuration error: size mismatch.\n");
		return -EINVAL;
	}

	/* initialize debug (initial state is recorded with this call) */
	err = ab8500_regulator_debug_init(pdev);
	if (err)
		return err;

	/* initialize registers */
	for (i = 0; i < pdata->num_reg_init; i++) {
		int id, mask, value;

		id = pdata->reg_init[i].id;
		mask = pdata->reg_init[i].mask;
		value = pdata->reg_init[i].value;

		/* check for configuration errors */
		BUG_ON(id >= AB8500_NUM_REGULATOR_REGISTERS);

		err = ab8500_regulator_init_registers(pdev, reg_init, id, mask, value);
		if (err < 0)
			return err;
	}

	/* register external regulators (before Vaux1, 2 and 3) */
	err = ab8500_ext_regulator_init(pdev);
	if (err)
		return err;

	/* register all regulators */
	for (i = 0; i < regulator_info_size; i++) {
		err = ab8500_regulator_register(pdev, &pdata->regulator[i],
						regulator_info, i, NULL);
		if (err < 0)
			return err;
	}

	return 0;
}

static int ab8500_regulator_remove(struct platform_device *pdev)
{
	int i, err;
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct ab8500_regulator_info *regulator_info;
	int regulator_info_size;


	if (is_ab9540(ab8500)) {
		regulator_info = ab9540_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab9540_regulator_info);
	} else if (is_ab8505(ab8500)) {
		regulator_info = ab8505_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab8505_regulator_info);
	} else {
		regulator_info = ab8500_regulator_info;
		regulator_info_size = ARRAY_SIZE(ab8500_regulator_info);
	}

	for (i = 0; i < regulator_info_size; i++) {
		struct ab8500_regulator_info *info = NULL;
		info = &regulator_info[i];

		dev_vdbg(rdev_get_dev(info->regulator),
			"%s-remove\n", info->desc.name);

		regulator_unregister(info->regulator);
	}

	/* remove external regulators (after Vaux1, 2 and 3) */
	err = ab8500_ext_regulator_exit(pdev);
	if (err)
		return err;

	/* remove regulator debug */
	err = ab8500_regulator_debug_exit(pdev);
	if (err)
		return err;

	return 0;
}

static struct platform_driver ab8500_regulator_driver = {
	.probe = ab8500_regulator_probe,
	.remove = ab8500_regulator_remove,
	.driver         = {
		.name   = "ab8500-regulator",
		.owner  = THIS_MODULE,
	},
};

static int __init ab8500_regulator_init(void)
{
	int ret;

	ret = platform_driver_register(&ab8500_regulator_driver);
	if (ret != 0)
		pr_err("Failed to register ab8500 regulator: %d\n", ret);

	return ret;
}
subsys_initcall(ab8500_regulator_init);

static void __exit ab8500_regulator_exit(void)
{
	platform_driver_unregister(&ab8500_regulator_driver);
}
module_exit(ab8500_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sundar Iyer <sundar.iyer@stericsson.com>");
MODULE_AUTHOR("Bengt Jonsson <bengt.g.jonsson@stericsson.com>");
MODULE_AUTHOR("Daniel Willerud <daniel.willerud@stericsson.com>");
MODULE_DESCRIPTION("Regulator Driver for ST-Ericsson AB8500 Mixed-Sig PMIC");
MODULE_ALIAS("platform:ab8500-regulator");
