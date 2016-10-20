/* drivers/input/misc/ots_pat9125/pixart_ots.h
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 */

#ifndef __PIXART_OTS_H_
#define __PIXART_OTS_H_

#define PAT9125_DEV_NAME	"pixart_pat9125"
#define MAX_BUF_SIZE		20
#define RESET_DELAY_US		1000
#define PINCTRL_STATE_ACTIVE	"pmx_rot_switch_active"
#define PINCTRL_STATE_SUSPEND	"pmx_rot_switch_suspend"
#define PINCTRL_STATE_RELEASE	"pmx_rot_switch_release"
#define VDD_VTG_MIN_UV		1800000
#define VDD_VTG_MAX_UV		1800000
#define VDD_ACTIVE_LOAD_UA	10000
#define VLD_VTG_MIN_UV		2800000
#define VLD_VTG_MAX_UV		3300000
#define VLD_ACTIVE_LOAD_UA	10000
#define DELAY_BETWEEN_REG_US	20000

/* Register addresses */
#define PIXART_PAT9125_PRODUCT_ID1_REG		0x00
#define PIXART_PAT9125_PRODUCT_ID2_REG		0x01
#define PIXART_PAT9125_MOTION_STATUS_REG	0x02
#define PIXART_PAT9125_DELTA_X_LO_REG		0x03
#define PIXART_PAT9125_DELTA_Y_LO_REG		0x04
#define PIXART_PAT9125_CONFIG_REG		0x06
#define PIXART_PAT9125_WRITE_PROTECT_REG	0x09
#define PIXART_PAT9125_SET_CPI_RES_X_REG	0x0D
#define PIXART_PAT9125_SET_CPI_RES_Y_REG	0x0E
#define PIXART_PAT9125_DELTA_XY_HI_REG		0x12
#define PIXART_PAT9125_ORIENTATION_REG		0x19
#define PIXART_PAT9125_VOLTAGE_SEGMENT_SEL_REG	0x4B
#define PIXART_PAT9125_SELECT_BANK_REG		0x7F
#define PIXART_PAT9125_MISC1_REG		0x5D
#define PIXART_PAT9125_MISC2_REG		0x5E
/*Register configuration data */
#define PIXART_PAT9125_SENSOR_ID		0x31
#define PIXART_PAT9125_RESET			0x97
#define PIXART_PAT9125_MOTION_DATA_LENGTH	0x04
#define PIXART_PAT9125_BANK0			0x00
#define PIXART_PAT9125_DISABLE_WRITE_PROTECT	0x5A
#define PIXART_PAT9125_ENABLE_WRITE_PROTECT	0x00
#define PIXART_PAT9125_CPI_RESOLUTION_X		0x65
#define PIXART_PAT9125_CPI_RESOLUTION_Y		0xFF
#define PIXART_PAT9125_LOW_VOLTAGE_SEGMENT	0x04
#define PIXART_PAT9125_VALID_MOTION_DATA	0x80

#define PIXART_SAMPLING_PERIOD_US_MIN 4000
#define PIXART_SAMPLING_PERIOD_US_MAX 8000

/* Export functions */
bool ots_sensor_init(struct i2c_client *);

#endif
