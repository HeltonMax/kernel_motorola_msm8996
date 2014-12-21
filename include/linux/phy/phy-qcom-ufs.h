/*
 * Copyright (c) 2013-2015, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef PHY_QCOM_UFS_H_
#define PHY_QCOM_UFS_H_

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/msm-bus.h>

#include <linux/scsi/ufs/ufshcd.h>
#include <linux/scsi/ufs/unipro.h>
#include <linux/scsi/ufs/ufs-qcom.h>

#define UFS_QCOM_PHY_CAL_ENTRY(reg, val)	\
	{				\
		.reg_offset = reg,	\
		.cfg_value = val,	\
	}

#define UFS_QCOM_PHY_NAME_LEN	30

enum {
	MASK_SERDES_START       = 0x1,
	MASK_PCS_READY          = 0x1,
};

enum {
	OFFSET_SERDES_START     = 0x0,
};

struct ufs_qcom_phy_stored_attributes {
	u32 att;
	u32 value;
};

struct ufs_qcom_phy_calibration {
	u32 reg_offset;
	u32 cfg_value;
};

struct ufs_qcom_phy {
	struct list_head list;
	struct device *dev;
	void __iomem *mmio;
	struct clk *tx_iface_clk;
	struct clk *rx_iface_clk;
	bool is_iface_clk_enabled;
	struct clk *ref_clk_src;
	struct clk *ref_clk_parent;
	struct clk *ref_clk;
	bool is_ref_clk_enabled;
	struct ufs_qcom_phy_vreg vdda_pll;
	struct ufs_qcom_phy_vreg vdda_phy;
	struct ufs_qcom_phy_vreg vddp_ref_clk;
	unsigned int quirks;
	u8 host_ctrl_rev_major;
	u16 host_ctrl_rev_minor;
	u16 host_ctrl_rev_step;

	/*
	* If UFS link is put into Hibern8 and if UFS PHY analog hardware is
	* power collapsed (by clearing UFS_PHY_POWER_DOWN_CONTROL), Hibern8
	* exit might fail even after powering on UFS PHY analog hardware.
	* Enabling this quirk will help to solve above issue by doing
	* custom PHY settings just before PHY analog power collapse.
	*/
	#define UFS_QCOM_PHY_QUIRK_HIBERN8_EXIT_AFTER_PHY_PWR_COLLAPSE	BIT(0)

	/*
	 * On some UFS PHY HW revisions, UFS PHY power up calibration sequence
	 * cannot have SVS mode configuration otherwise calibration result
	 * cannot be used in HS-G3. So there are additional register writes must
	 * be done after the PHY is initialized but before the controller
	 * requests hibernate exit.
	 */
	#define UFS_QCOM_PHY_QUIRK_SVS_MODE	BIT(1)

	char name[UFS_QCOM_PHY_NAME_LEN];
	struct ufs_qcom_phy_calibration *cached_regs;
	int cached_regs_table_size;
	bool is_powered_on;
	struct ufs_qcom_phy_specific_ops *phy_spec_ops;
};

/**
 * struct ufs_qcom_phy_specific_ops - set of pointers to functions which have a
 * specific implementation per phy. Each UFS phy, should implement
 * those functions according to its spec and requirements
 * @calibrate_phy: pointer to a function that calibrate the phy
 * @start_serdes: pointer to a function that starts the serdes
 * @save_configuration: pointer to a function that saves phy
 * configuration
 * @is_physical_coding_sublayer_ready: pointer to a function that
 * checks pcs readiness
 * @set_tx_lane_enable: pointer to a function that enable tx lanes
 * @power_control: pointer to a function that controls analog rail of phy
 * and writes to QSERDES_RX_SIGDET_CNTRL attribute
 */
struct ufs_qcom_phy_specific_ops {
	int (*calibrate_phy) (struct ufs_qcom_phy *phy);
	void (*start_serdes) (struct ufs_qcom_phy *phy);
	void (*save_configuration)(struct ufs_qcom_phy *phy);
	void (*restore_configuration)(struct ufs_qcom_phy *phy);
	int (*is_physical_coding_sublayer_ready) (struct ufs_qcom_phy *phy);
	void (*set_tx_lane_enable) (struct ufs_qcom_phy *phy, u32 val);
	void (*power_control) (struct ufs_qcom_phy *phy, bool val);
};

int ufs_qcom_phy_cfg_vreg(struct phy *phy,
			struct ufs_qcom_phy_vreg *vreg, bool on);
int ufs_qcom_phy_enable_vreg(struct phy *phy,
			struct ufs_qcom_phy_vreg *vreg);
int ufs_qcom_phy_disable_vreg(struct phy *phy,
			struct ufs_qcom_phy_vreg *vreg);
int ufs_qcom_phy_enable_ref_clk(struct phy *phy);
void ufs_qcom_phy_disable_ref_clk(struct phy *phy);
void ufs_qcom_phy_enable_dev_ref_clk(struct phy *);
void ufs_qcom_phy_disable_dev_ref_clk(struct phy *);
int ufs_qcom_phy_enable_iface_clk(struct phy *phy);
void ufs_qcom_phy_disable_iface_clk(struct phy *phy);
void ufs_qcom_phy_restore_swi_regs(struct phy *phy);
int ufs_qcom_phy_link_startup_post_change(struct phy *phy,
			struct ufs_hba *hba);
int ufs_qcom_phy_base_init(struct platform_device *pdev,
			struct ufs_qcom_phy *ufs_qcom_phy_ops);
struct ufs_qcom_phy *get_ufs_qcom_phy(struct phy *generic_phy);
int ufs_qcom_phy_start_serdes(struct phy *generic_phy);
int ufs_qcom_phy_set_tx_lane_enable(struct phy *generic_phy, u32 tx_lanes);
int ufs_qcom_phy_calibrate_phy(struct phy *generic_phy);
int ufs_qcom_phy_is_pcs_ready(struct phy *generic_phy);
void ufs_qcom_phy_save_controller_version(struct phy *generic_phy,
			u8 major, u16 minor, u16 step);
int ufs_qcom_phy_power_on(struct phy *generic_phy);
int ufs_qcom_phy_power_off(struct phy *generic_phy);
int ufs_qcom_phy_exit(struct phy *generic_phy);
int ufs_qcom_phy_init_clks(struct phy *generic_phy,
			struct ufs_qcom_phy *phy_common);
int ufs_qcom_phy_init_vregulators(struct phy *generic_phy,
			struct ufs_qcom_phy *phy_common);
int ufs_qcom_phy_remove(struct phy *generic_phy,
		       struct ufs_qcom_phy *ufs_qcom_phy);
struct phy *ufs_qcom_phy_generic_probe(struct platform_device *pdev,
			struct ufs_qcom_phy *common_cfg,
			struct phy_ops *ufs_qcom_phy_gen_ops,
			struct ufs_qcom_phy_specific_ops *phy_spec_ops);
int ufs_qcom_phy_calibrate(struct ufs_qcom_phy *ufs_qcom_phy,
			struct ufs_qcom_phy_calibration *tbl_A, int tbl_size_A,
			struct ufs_qcom_phy_calibration *tbl_B, int tbl_size_B,
			int rate);
#endif
