/*
 * Driver for the mt9m111 sensor
 *
 * Copyright (C) 2008 Erik Andrén
 * Copyright (C) 2007 Ilyes Gouta. Based on the m5603x Linux Driver Project.
 * Copyright (C) 2005 m5603x Linux Driver Project <m5602@x3ng.com.br>
 *
 * Portions of code to USB interface and ALi driver software,
 * Copyright (c) 2006 Willem Duinker
 * v4l2 interface modeled after the V4L2 driver
 * for SN9C10x PC Camera Controllers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 */

#include "m5602_mt9m111.h"

int mt9m111_probe(struct sd *sd)
{
	u8 data[2] = {0x00, 0x00};
	int i;

	if (force_sensor) {
		if (force_sensor == MT9M111_SENSOR) {
			info("Forcing a %s sensor", mt9m111.name);
			goto sensor_found;
		}
		/* If we want to force another sensor, don't try to probe this
		 * one */
		return -ENODEV;
	}

	info("Probing for a mt9m111 sensor");

	/* Do the preinit */
	for (i = 0; i < ARRAY_SIZE(preinit_mt9m111); i++) {
		if (preinit_mt9m111[i][0] == BRIDGE) {
			m5602_write_bridge(sd,
				preinit_mt9m111[i][1],
				preinit_mt9m111[i][2]);
		} else {
			data[0] = preinit_mt9m111[i][2];
			data[1] = preinit_mt9m111[i][3];
			mt9m111_write_sensor(sd,
				preinit_mt9m111[i][1], data, 2);
		}
	}

	if (mt9m111_read_sensor(sd, MT9M111_SC_CHIPVER, data, 2))
		return -ENODEV;

	if ((data[0] == 0x14) && (data[1] == 0x3a)) {
		info("Detected a mt9m111 sensor");
		goto sensor_found;
	}

	return -ENODEV;

sensor_found:
	sd->gspca_dev.cam.cam_mode = mt9m111.modes;
	sd->gspca_dev.cam.nmodes = mt9m111.nmodes;
	sd->desc->ctrls = mt9m111.ctrls;
	sd->desc->nctrls = mt9m111.nctrls;
	return 0;
}

int mt9m111_init(struct sd *sd)
{
	int i, err = 0;

	/* Init the sensor */
	for (i = 0; i < ARRAY_SIZE(init_mt9m111); i++) {
		u8 data[2];

		if (init_mt9m111[i][0] == BRIDGE) {
			err = m5602_write_bridge(sd,
				init_mt9m111[i][1],
				init_mt9m111[i][2]);
		} else {
			data[0] = init_mt9m111[i][2];
			data[1] = init_mt9m111[i][3];
			err = mt9m111_write_sensor(sd,
				init_mt9m111[i][1], data, 2);
		}
	}

	if (dump_sensor)
		mt9m111_dump_registers(sd);

	return (err < 0) ? err : 0;
}

int mt9m111_power_down(struct sd *sd)
{
	return 0;
}

int mt9m111_get_vflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	err = mt9m111_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
				  data, 2);
	*val = data[0] & MT9M111_RMB_MIRROR_ROWS;
	PDEBUG(DBG_V4L2_CID, "Read vertical flip %d", *val);

	return (err < 0) ? err : 0;
}

int mt9m111_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(DBG_V4L2_CID, "Set vertical flip to %d", val);

	/* Set the correct page map */
	err = mt9m111_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		goto out;

	err = mt9m111_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B, data, 2);
	if (err < 0)
		goto out;

	data[0] = (data[0] & 0xfe) | val;
	err = mt9m111_write_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
				   data, 2);
out:
	return (err < 0) ? err : 0;
}

int mt9m111_get_hflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	err = mt9m111_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
				  data, 2);
	*val = data[0] & MT9M111_RMB_MIRROR_COLS;
	PDEBUG(DBG_V4L2_CID, "Read horizontal flip %d", *val);

	return (err < 0) ? err : 0;
}

int mt9m111_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(DBG_V4L2_CID, "Set horizontal flip to %d", val);

	/* Set the correct page map */
	err = mt9m111_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		goto out;

	err = mt9m111_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B, data, 2);
	if (err < 0)
		goto out;

	data[0] = (data[0] & 0xfd) | ((val << 1) & 0x02);
	err = mt9m111_write_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
					data, 2);
out:
	return (err < 0) ? err : 0;
}

int mt9m111_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err, tmp;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	err = mt9m111_read_sensor(sd, MT9M111_SC_GLOBAL_GAIN, data, 2);
	tmp = ((data[1] << 8) | data[0]);

	*val = ((tmp & (1 << 10)) * 2) |
	      ((tmp & (1 <<  9)) * 2) |
	      ((tmp & (1 <<  8)) * 2) |
	       (tmp & 0x7f);

	PDEBUG(DBG_V4L2_CID, "Read gain %d", *val);

	return (err < 0) ? err : 0;
}

int mt9m111_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err, tmp;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	/* Set the correct page map */
	err = mt9m111_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		goto out;

	if (val >= INITIAL_MAX_GAIN * 2 * 2 * 2)
		return -EINVAL;

	if ((val >= INITIAL_MAX_GAIN * 2 * 2) &&
	    (val < (INITIAL_MAX_GAIN - 1) * 2 * 2 * 2))
		tmp = (1 << 10) | (val << 9) |
				(val << 8) | (val / 8);
	else if ((val >= INITIAL_MAX_GAIN * 2) &&
		 (val <  INITIAL_MAX_GAIN * 2 * 2))
		tmp = (1 << 9) | (1 << 8) | (val / 4);
	else if ((val >= INITIAL_MAX_GAIN) &&
		 (val < INITIAL_MAX_GAIN * 2))
		tmp = (1 << 8) | (val / 2);
	else
		tmp = val;

	data[1] = (tmp & 0xff00) >> 8;
	data[0] = (tmp & 0xff);
	PDEBUG(DBG_V4L2_CID, "tmp=%d, data[1]=%d, data[0]=%d", tmp,
	       data[1], data[0]);

	err = mt9m111_write_sensor(sd, MT9M111_SC_GLOBAL_GAIN,
				   data, 2);
out:
	return (err < 0) ? err : 0;
}

int mt9m111_read_sensor(struct sd *sd, const u8 address,
			u8 *i2c_data, const u8 len) {
	int err, i;

	do {
		err = m5602_read_bridge(sd, M5602_XB_I2C_STATUS, i2c_data);
	} while ((*i2c_data & I2C_BUSY) && !err);
	if (err < 0)
		goto out;

	err = m5602_write_bridge(sd, M5602_XB_I2C_DEV_ADDR,
				 sd->sensor->i2c_slave_id);
	if (err < 0)
		goto out;

	err = m5602_write_bridge(sd, M5602_XB_I2C_REG_ADDR, address);
	if (err < 0)
		goto out;

	err = m5602_write_bridge(sd, M5602_XB_I2C_CTRL, 0x1a);
	if (err < 0)
		goto out;

	for (i = 0; i < len && !err; i++) {
		err = m5602_read_bridge(sd, M5602_XB_I2C_DATA, &(i2c_data[i]));

		PDEBUG(DBG_TRACE, "Reading sensor register "
		       "0x%x contains 0x%x ", address, *i2c_data);
	}
out:
	return (err < 0) ? err : 0;
}

int mt9m111_write_sensor(struct sd *sd, const u8 address,
				u8 *i2c_data, const u8 len)
{
	int err, i;
	u8 *p;
	struct usb_device *udev = sd->gspca_dev.dev;
	__u8 *buf = sd->gspca_dev.usb_buf;

	/* No sensor with a data width larger
	   than 16 bits has yet been seen, nor with 0 :p*/
	if (len > 2 || !len)
		return -EINVAL;

	memcpy(buf, sensor_urb_skeleton,
	       sizeof(sensor_urb_skeleton));

	buf[11] = sd->sensor->i2c_slave_id;
	buf[15] = address;

	p = buf + 16;

	/* Copy a four byte write sequence for each byte to be written to */
	for (i = 0; i < len; i++) {
		memcpy(p, sensor_urb_skeleton + 16, 4);
		p[3] = i2c_data[i];
		p += 4;
		PDEBUG(DBG_TRACE, "Writing sensor register 0x%x with 0x%x",
		       address, i2c_data[i]);
	}

	/* Copy the tailer */
	memcpy(p, sensor_urb_skeleton + 20, 4);

	/* Set the total length */
	p[3] = 0x10 + len;

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      0x04, 0x40, 0x19,
			      0x0000, buf,
			      20 + len * 4, M5602_URB_MSG_TIMEOUT);

	return (err < 0) ? err : 0;
}

void mt9m111_dump_registers(struct sd *sd)
{
	u8 address, value[2] = {0x00, 0x00};

	info("Dumping the mt9m111 register state");

	info("Dumping the mt9m111 sensor core registers");
	value[1] = MT9M111_SENSOR_CORE;
	mt9m111_write_sensor(sd, MT9M111_PAGE_MAP, value, 2);
	for (address = 0; address < 0xff; address++) {
		mt9m111_read_sensor(sd, address, value, 2);
		info("register 0x%x contains 0x%x%x",
		     address, value[0], value[1]);
	}

	info("Dumping the mt9m111 color pipeline registers");
	value[1] = MT9M111_COLORPIPE;
	mt9m111_write_sensor(sd, MT9M111_PAGE_MAP, value, 2);
	for (address = 0; address < 0xff; address++) {
		mt9m111_read_sensor(sd, address, value, 2);
		info("register 0x%x contains 0x%x%x",
		     address, value[0], value[1]);
	}

	info("Dumping the mt9m111 camera control registers");
	value[1] = MT9M111_CAMERA_CONTROL;
	mt9m111_write_sensor(sd, MT9M111_PAGE_MAP, value, 2);
	for (address = 0; address < 0xff; address++) {
		mt9m111_read_sensor(sd, address, value, 2);
		info("register 0x%x contains 0x%x%x",
		     address, value[0], value[1]);
	}

	info("mt9m111 register state dump complete");
}
