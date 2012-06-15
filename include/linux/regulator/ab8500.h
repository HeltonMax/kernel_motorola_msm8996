/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 */

#ifndef __LINUX_MFD_AB8500_REGULATOR_H
#define __LINUX_MFD_AB8500_REGULATOR_H

/* AB8500 regulators */
enum ab8500_regulator_id {
	AB8500_LDO_AUX1,
	AB8500_LDO_AUX2,
	AB8500_LDO_AUX3,
	AB8500_LDO_INTCORE,
	AB8500_LDO_TVOUT,
	AB8500_LDO_USB,
	AB8500_LDO_AUDIO,
	AB8500_LDO_ANAMIC1,
	AB8500_LDO_ANAMIC2,
	AB8500_LDO_DMIC,
	AB8500_LDO_ANA,
	AB8500_NUM_REGULATORS,
};

/* AB9450 regulators */
enum ab9540_regulator_id {
	AB9540_LDO_AUX1,
	AB9540_LDO_AUX2,
	AB9540_LDO_AUX3,
	AB9540_LDO_AUX4,
	AB9540_LDO_INTCORE,
	AB9540_LDO_TVOUT,
	AB9540_LDO_USB,
	AB9540_LDO_AUDIO,
	AB9540_LDO_ANAMIC1,
	AB9540_LDO_ANAMIC2,
	AB9540_LDO_DMIC,
	AB9540_LDO_ANA,
	AB9540_SYSCLKREQ_2,
	AB9540_SYSCLKREQ_4,
	AB9540_NUM_REGULATORS,
};

/* AB8500 and AB9540 register initialization */
struct ab8500_regulator_reg_init {
	int id;
	u8 value;
};

#define INIT_REGULATOR_REGISTER(_id, _value)	\
	{					\
		.id = _id,			\
		.value = _value,		\
	}

/* AB8500 registers */
enum ab8500_regulator_reg {
	AB8500_REGUREQUESTCTRL2,
	AB8500_REGUREQUESTCTRL3,
	AB8500_REGUREQUESTCTRL4,
	AB8500_REGUSYSCLKREQ1HPVALID1,
	AB8500_REGUSYSCLKREQ1HPVALID2,
	AB8500_REGUHWHPREQ1VALID1,
	AB8500_REGUHWHPREQ1VALID2,
	AB8500_REGUHWHPREQ2VALID1,
	AB8500_REGUHWHPREQ2VALID2,
	AB8500_REGUSWHPREQVALID1,
	AB8500_REGUSWHPREQVALID2,
	AB8500_REGUSYSCLKREQVALID1,
	AB8500_REGUSYSCLKREQVALID2,
	AB8500_REGUMISC1,
	AB8500_VAUDIOSUPPLY,
	AB8500_REGUCTRL1VAMIC,
	AB8500_VPLLVANAREGU,
	AB8500_VREFDDR,
	AB8500_EXTSUPPLYREGU,
	AB8500_VAUX12REGU,
	AB8500_VRF1VAUX3REGU,
	AB8500_VAUX1SEL,
	AB8500_VAUX2SEL,
	AB8500_VRF1VAUX3SEL,
	AB8500_REGUCTRL2SPARE,
	AB8500_REGUCTRLDISCH,
	AB8500_REGUCTRLDISCH2,
	AB8500_VSMPS1SEL1,
	AB8500_NUM_REGULATOR_REGISTERS,
};


/* AB9540 registers */
enum ab9540_regulator_reg {
	AB9540_REGUREQUESTCTRL1,
	AB9540_REGUREQUESTCTRL2,
	AB9540_REGUREQUESTCTRL3,
	AB9540_REGUREQUESTCTRL4,
	AB9540_REGUSYSCLKREQ1HPVALID1,
	AB9540_REGUSYSCLKREQ1HPVALID2,
	AB9540_REGUHWHPREQ1VALID1,
	AB9540_REGUHWHPREQ1VALID2,
	AB9540_REGUHWHPREQ2VALID1,
	AB9540_REGUHWHPREQ2VALID2,
	AB9540_REGUSWHPREQVALID1,
	AB9540_REGUSWHPREQVALID2,
	AB9540_REGUSYSCLKREQVALID1,
	AB9540_REGUSYSCLKREQVALID2,
	AB9540_REGUVAUX4REQVALID,
	AB9540_REGUMISC1,
	AB9540_VAUDIOSUPPLY,
	AB9540_REGUCTRL1VAMIC,
	AB9540_VSMPS1REGU,
	AB9540_VSMPS2REGU,
	AB9540_VSMPS3REGU, /* NOTE! PRCMU register */
	AB9540_VPLLVANAREGU,
	AB9540_EXTSUPPLYREGU,
	AB9540_VAUX12REGU,
	AB9540_VRF1VAUX3REGU,
	AB9540_VSMPS1SEL1,
	AB9540_VSMPS1SEL2,
	AB9540_VSMPS1SEL3,
	AB9540_VSMPS2SEL1,
	AB9540_VSMPS2SEL2,
	AB9540_VSMPS2SEL3,
	AB9540_VSMPS3SEL1, /* NOTE! PRCMU register */
	AB9540_VSMPS3SEL2, /* NOTE! PRCMU register */
	AB9540_VAUX1SEL,
	AB9540_VAUX2SEL,
	AB9540_VRF1VAUX3SEL,
	AB9540_REGUCTRL2SPARE,
	AB9540_VAUX4REQCTRL,
	AB9540_VAUX4REGU,
	AB9540_VAUX4SEL,
	AB9540_REGUCTRLDISCH,
	AB9540_REGUCTRLDISCH2,
	AB9540_REGUCTRLDISCH3,
	AB9540_NUM_REGULATOR_REGISTERS,
};

#endif
