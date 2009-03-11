/*
 *  Driver for the Auvitek AU0828 USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "au0828.h"

#include <media/v4l2-common.h>

static int i2c_scan;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");

#define I2C_WAIT_DELAY 512
#define I2C_WAIT_RETRY 64

static inline int i2c_slave_did_write_ack(struct i2c_adapter *i2c_adap)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	return au0828_read(dev, REG_201) & 0x08 ? 0 : 1;
}

static inline int i2c_slave_did_read_ack(struct i2c_adapter *i2c_adap)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	return au0828_read(dev, REG_201) & 0x02 ? 0 : 1;
}

static int i2c_wait_read_ack(struct i2c_adapter *i2c_adap)
{
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		if (!i2c_slave_did_read_ack(i2c_adap))
			break;
		udelay(I2C_WAIT_DELAY);
	}

	if (I2C_WAIT_RETRY == count)
		return 0;

	return 1;
}

static inline int i2c_is_read_busy(struct i2c_adapter *i2c_adap)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	return au0828_read(dev, REG_201) & 0x01 ? 0 : 1;
}

static int i2c_wait_read_done(struct i2c_adapter *i2c_adap)
{
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		if (!i2c_is_read_busy(i2c_adap))
			break;
		udelay(I2C_WAIT_DELAY);
	}

	if (I2C_WAIT_RETRY == count)
		return 0;

	return 1;
}

static inline int i2c_is_write_done(struct i2c_adapter *i2c_adap)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	return au0828_read(dev, REG_201) & 0x04 ? 1 : 0;
}

static int i2c_wait_write_done(struct i2c_adapter *i2c_adap)
{
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		if (i2c_is_write_done(i2c_adap))
			break;
		udelay(I2C_WAIT_DELAY);
	}

	if (I2C_WAIT_RETRY == count)
		return 0;

	return 1;
}

static inline int i2c_is_busy(struct i2c_adapter *i2c_adap)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	return au0828_read(dev, REG_201) & 0x10 ? 1 : 0;
}

static int i2c_wait_done(struct i2c_adapter *i2c_adap)
{
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		if (!i2c_is_busy(i2c_adap))
			break;
		udelay(I2C_WAIT_DELAY);
	}

	if (I2C_WAIT_RETRY == count)
		return 0;

	return 1;
}

/* FIXME: Implement join handling correctly */
static int i2c_sendbytes(struct i2c_adapter *i2c_adap,
	const struct i2c_msg *msg, int joined_rlen)
{
	int i, strobe = 0;
	struct au0828_dev *dev = i2c_adap->algo_data;

	dprintk(4, "%s()\n", __func__);

	au0828_write(dev, REG_2FF, 0x01);

	/* FIXME: There is a problem with i2c communications with xc5000 that
	   requires us to slow down the i2c clock until we have a better
	   strategy (such as using the secondary i2c bus to do firmware
	   loading */
	if ((msg->addr << 1) == 0xc2) {
		au0828_write(dev, REG_202, 0x40);
	} else {
		au0828_write(dev, REG_202, 0x07);
	}

	/* Hardware needs 8 bit addresses */
	au0828_write(dev, REG_203, msg->addr << 1);

	dprintk(4, "SEND: %02x\n", msg->addr);

	/* Deal with i2c_scan */
	if (msg->len == 0) {
		/* The analog tuner detection code makes use of the SMBUS_QUICK
		   message (which involves a zero length i2c write).  To avoid
		   checking the status register when we didn't strobe out any
		   actual bytes to the bus, just do a read check.  This is
		   consistent with how I saw i2c device checking done in the
		   USB trace of the Windows driver */
		au0828_write(dev, REG_200, 0x20);
		if (!i2c_wait_done(i2c_adap))
			return -EIO;

		if (i2c_wait_read_ack(i2c_adap))
			return -EIO;

		return 0;
	}

	for (i = 0; i < msg->len;) {

		dprintk(4, " %02x\n", msg->buf[i]);

		au0828_write(dev, REG_205, msg->buf[i]);

		strobe++;
		i++;

		if ((strobe >= 4) || (i >= msg->len)) {

			/* Strobe the byte into the bus */
			if (i < msg->len)
				au0828_write(dev, REG_200, 0x41);
			else
				au0828_write(dev, REG_200, 0x01);

			/* Reset strobe trigger */
			strobe = 0;

			if (!i2c_wait_write_done(i2c_adap))
				return -EIO;

		}

	}
	if (!i2c_wait_done(i2c_adap))
		return -EIO;

	dprintk(4, "\n");

	return msg->len;
}

/* FIXME: Implement join handling correctly */
static int i2c_readbytes(struct i2c_adapter *i2c_adap,
	const struct i2c_msg *msg, int joined)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	int i;

	dprintk(4, "%s()\n", __func__);

	au0828_write(dev, REG_2FF, 0x01);

	/* FIXME: There is a problem with i2c communications with xc5000 that
	   requires us to slow down the i2c clock until we have a better
	   strategy (such as using the secondary i2c bus to do firmware
	   loading */
	if ((msg->addr << 1) == 0xc2) {
		au0828_write(dev, REG_202, 0x40);
	} else {
		au0828_write(dev, REG_202, 0x07);
	}

	/* Hardware needs 8 bit addresses */
	au0828_write(dev, REG_203, msg->addr << 1);

	dprintk(4, " RECV:\n");

	/* Deal with i2c_scan */
	if (msg->len == 0) {
		au0828_write(dev, REG_200, 0x20);
		if (i2c_wait_read_ack(i2c_adap))
			return -EIO;
		return 0;
	}

	for (i = 0; i < msg->len;) {

		i++;

		if (i < msg->len)
			au0828_write(dev, REG_200, 0x60);
		else
			au0828_write(dev, REG_200, 0x20);

		if (!i2c_wait_read_done(i2c_adap))
			return -EIO;

		msg->buf[i-1] = au0828_read(dev, REG_209) & 0xff;

		dprintk(4, " %02x\n", msg->buf[i-1]);
	}
	if (!i2c_wait_done(i2c_adap))
		return -EIO;

	dprintk(4, "\n");

	return msg->len;
}

static int i2c_xfer(struct i2c_adapter *i2c_adap,
		    struct i2c_msg *msgs, int num)
{
	int i, retval = 0;

	dprintk(4, "%s(num = %d)\n", __func__, num);

	for (i = 0; i < num; i++) {
		dprintk(4, "%s(num = %d) addr = 0x%02x  len = 0x%x\n",
			__func__, num, msgs[i].addr, msgs[i].len);
		if (msgs[i].flags & I2C_M_RD) {
			/* read */
			retval = i2c_readbytes(i2c_adap, &msgs[i], 0);
		} else if (i + 1 < num && (msgs[i + 1].flags & I2C_M_RD) &&
			   msgs[i].addr == msgs[i + 1].addr) {
			/* write then read from same address */
			retval = i2c_sendbytes(i2c_adap, &msgs[i],
					       msgs[i + 1].len);
			if (retval < 0)
				goto err;
			i++;
			retval = i2c_readbytes(i2c_adap, &msgs[i], 1);
		} else {
			/* write */
			retval = i2c_sendbytes(i2c_adap, &msgs[i], 0);
		}
		if (retval < 0)
			goto err;
	}
	return num;

err:
	return retval;
}

static int attach_inform(struct i2c_client *client)
{
	dprintk(1, "%s i2c attach [addr=0x%x,client=%s]\n",
		client->driver->driver.name, client->addr, client->name);

	if (!client->driver->command)
		return 0;

	return 0;
}

static int detach_inform(struct i2c_client *client)
{
	dprintk(1, "i2c detach [client=%s]\n", client->name);

	return 0;
}

void au0828_call_i2c_clients(struct au0828_dev *dev,
			      unsigned int cmd, void *arg)
{
	if (dev->i2c_rc != 0)
		return;

	i2c_clients_command(&dev->i2c_adap, cmd, arg);
}

static u32 au0828_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_I2C;
}

static struct i2c_algorithm au0828_i2c_algo_template = {
	.master_xfer	= i2c_xfer,
	.functionality	= au0828_functionality,
};

/* ----------------------------------------------------------------------- */

static struct i2c_adapter au0828_i2c_adap_template = {
	.name              = DRIVER_NAME,
	.owner             = THIS_MODULE,
	.id                = I2C_HW_B_AU0828,
	.algo              = &au0828_i2c_algo_template,
	.class             = I2C_CLASS_TV_ANALOG,
	.client_register   = attach_inform,
	.client_unregister = detach_inform,
};

static struct i2c_client au0828_i2c_client_template = {
	.name	= "au0828 internal",
};

static char *i2c_devs[128] = {
	[0x8e >> 1] = "au8522",
	[0xa0 >> 1] = "eeprom",
	[0xc2 >> 1] = "tuner/xc5000",
};

static void do_i2c_scan(char *name, struct i2c_client *c)
{
	unsigned char buf;
	int i, rc;

	for (i = 0; i < 128; i++) {
		c->addr = i;
		rc = i2c_master_recv(c, &buf, 0);
		if (rc < 0)
			continue;
		printk(KERN_INFO "%s: i2c scan: found device @ 0x%x  [%s]\n",
		       name, i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}
}

/* init + register i2c algo-bit adapter */
int au0828_i2c_register(struct au0828_dev *dev)
{
	dprintk(1, "%s()\n", __func__);

	memcpy(&dev->i2c_adap, &au0828_i2c_adap_template,
	       sizeof(dev->i2c_adap));
	memcpy(&dev->i2c_algo, &au0828_i2c_algo_template,
	       sizeof(dev->i2c_algo));
	memcpy(&dev->i2c_client, &au0828_i2c_client_template,
	       sizeof(dev->i2c_client));

	dev->i2c_adap.dev.parent = &dev->usbdev->dev;

	strlcpy(dev->i2c_adap.name, DRIVER_NAME,
		sizeof(dev->i2c_adap.name));

	dev->i2c_algo.data = dev;
	dev->i2c_adap.algo_data = dev;
	i2c_set_adapdata(&dev->i2c_adap, dev);
	i2c_add_adapter(&dev->i2c_adap);

	dev->i2c_client.adapter = &dev->i2c_adap;

	if (0 == dev->i2c_rc) {
		printk(KERN_INFO "%s: i2c bus registered\n", DRIVER_NAME);
		if (i2c_scan)
			do_i2c_scan(DRIVER_NAME, &dev->i2c_client);
	} else
		printk(KERN_INFO "%s: i2c bus register FAILED\n", DRIVER_NAME);

	return dev->i2c_rc;
}

int au0828_i2c_unregister(struct au0828_dev *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
}

