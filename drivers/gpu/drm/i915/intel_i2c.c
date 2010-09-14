/*
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 */
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/i2c-id.h>
#include <linux/i2c-algo-bit.h>
#include "drmP.h"
#include "drm.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"

void intel_i2c_quirk_set(struct drm_device *dev, bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val;

	/* When using bit bashing for I2C, this bit needs to be set to 1 */
	if (!IS_PINEVIEW(dev))
		return;

	val = I915_READ(DSPCLK_GATE_D);
	if (enable)
		val |= DPCUNIT_CLOCK_GATE_DISABLE;
	else
		val &= ~DPCUNIT_CLOCK_GATE_DISABLE;
	I915_WRITE(DSPCLK_GATE_D, val);
}

/*
 * Intel GPIO access functions
 */

#define I2C_RISEFALL_TIME 20

static inline struct drm_i915_private *
get_dev_priv(struct intel_i2c_chan *chan)
{
	return chan->encoder->base.dev->dev_private;
}

static int get_clock(void *data)
{
	struct intel_i2c_chan *chan = data;
	struct drm_i915_private *dev_priv = get_dev_priv(chan);
	return (I915_READ(chan->reg) & GPIO_CLOCK_VAL_IN) != 0;
}

static int get_data(void *data)
{
	struct intel_i2c_chan *chan = data;
	struct drm_i915_private *dev_priv = get_dev_priv(chan);
	return (I915_READ(chan->reg) & GPIO_DATA_VAL_IN) != 0;
}

static void set_clock(void *data, int state_high)
{
	struct intel_i2c_chan *chan = data;
	struct drm_i915_private *dev_priv = get_dev_priv(chan);
	struct drm_device *dev = dev_priv->dev;
	u32 reserved = 0, clock_bits;

	/* On most chips, these bits must be preserved in software. */
	if (!IS_I830(dev) && !IS_845G(dev))
		reserved = I915_READ(chan->reg) & (GPIO_DATA_PULLUP_DISABLE |
						   GPIO_CLOCK_PULLUP_DISABLE);

	if (state_high)
		clock_bits = GPIO_CLOCK_DIR_IN | GPIO_CLOCK_DIR_MASK;
	else
		clock_bits = GPIO_CLOCK_DIR_OUT | GPIO_CLOCK_DIR_MASK |
			GPIO_CLOCK_VAL_MASK;
	I915_WRITE(chan->reg, reserved | clock_bits);
	POSTING_READ(chan->reg);
}

static void set_data(void *data, int state_high)
{
	struct intel_i2c_chan *chan = data;
	struct drm_i915_private *dev_priv = get_dev_priv(chan);
	struct drm_device *dev = dev_priv->dev;
	u32 reserved = 0, data_bits;

	/* On most chips, these bits must be preserved in software. */
	if (!IS_I830(dev) && !IS_845G(dev))
		reserved = I915_READ(chan->reg) & (GPIO_DATA_PULLUP_DISABLE |
						   GPIO_CLOCK_PULLUP_DISABLE);

	if (state_high)
		data_bits = GPIO_DATA_DIR_IN | GPIO_DATA_DIR_MASK;
	else
		data_bits = GPIO_DATA_DIR_OUT | GPIO_DATA_DIR_MASK |
			GPIO_DATA_VAL_MASK;

	I915_WRITE(chan->reg, reserved | data_bits);
	POSTING_READ(chan->reg);
}

/* Clears the GMBUS setup.  Our driver doesn't make use of the GMBUS I2C
 * engine, but if the BIOS leaves it enabled, then that can break our use
 * of the bit-banging I2C interfaces.  This is notably the case with the
 * Mac Mini in EFI mode.
 */
void
intel_i2c_reset_gmbus(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_PCH_SPLIT(dev))
		I915_WRITE(PCH_GMBUS0, 0);
	else
		I915_WRITE(GMBUS0, 0);
}

/**
 * intel_i2c_create - instantiate an Intel i2c bus using the specified GPIO reg
 * @dev: DRM device
 * @output: driver specific output device
 * @reg: GPIO reg to use
 * @name: name for this bus
 * @slave_addr: slave address (if fixed)
 *
 * Creates and registers a new i2c bus with the Linux i2c layer, for use
 * in output probing and control (e.g. DDC or SDVO control functions).
 *
 * Possible values for @reg include:
 *   %GPIOA
 *   %GPIOB
 *   %GPIOC
 *   %GPIOD
 *   %GPIOE
 *   %GPIOF
 *   %GPIOG
 *   %GPIOH
 * see PRM for details on how these different busses are used.
 */
struct i2c_adapter *intel_i2c_create(struct intel_encoder *encoder,
				     const u32 reg,
				     const char *name)
{
	struct intel_i2c_chan *chan;
	struct drm_device *dev = encoder->base.dev;

	chan = kzalloc(sizeof(struct intel_i2c_chan), GFP_KERNEL);
	if (!chan)
		goto out_free;

	chan->encoder = encoder;
	chan->reg = reg;
	snprintf(chan->adapter.name, I2C_NAME_SIZE, "intel drm %s", name);
	chan->adapter.owner = THIS_MODULE;
	chan->adapter.algo_data	= &chan->algo;
	chan->adapter.dev.parent = &dev->pdev->dev;
	chan->algo.setsda = set_data;
	chan->algo.setscl = set_clock;
	chan->algo.getsda = get_data;
	chan->algo.getscl = get_clock;
	chan->algo.udelay = I2C_RISEFALL_TIME;
	chan->algo.timeout = usecs_to_jiffies(2200);
	chan->algo.data = chan;

	i2c_set_adapdata(&chan->adapter, chan);

	if (i2c_bit_add_bus(&chan->adapter))
		goto out_free;

	intel_i2c_reset_gmbus(dev);

	/* JJJ:  raise SCL and SDA? */
	intel_i2c_quirk_set(dev, true);
	set_data(chan, 1);
	udelay(I2C_RISEFALL_TIME);
	set_clock(chan, 1);
	udelay(I2C_RISEFALL_TIME);
	intel_i2c_quirk_set(dev, false);

	return &chan->adapter;

out_free:
	kfree(chan);
	return NULL;
}

/**
 * intel_i2c_destroy - unregister and free i2c bus resources
 * @output: channel to free
 *
 * Unregister the adapter from the i2c layer, then free the structure.
 */
void intel_i2c_destroy(struct i2c_adapter *adapter)
{
	struct intel_i2c_chan *chan;

	if (!adapter)
		return;

	chan = container_of(adapter,
			    struct intel_i2c_chan,
			    adapter);
	i2c_del_adapter(&chan->adapter);
	kfree(chan);
}
