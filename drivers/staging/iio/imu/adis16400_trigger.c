#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/spi/spi.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../trigger.h"
#include "adis16400.h"

static IIO_TRIGGER_NAME_ATTR;

static struct attribute *adis16400_trigger_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group adis16400_trigger_attr_group = {
	.attrs = adis16400_trigger_attrs,
};

/**
 * adis16400_data_rdy_trigger_set_state() set datardy interrupt state
 **/
static int adis16400_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct adis16400_state *st = trig->private_data;
	struct iio_dev *indio_dev = st->indio_dev;

	dev_dbg(&indio_dev->dev, "%s (%d)\n", __func__, state);
	return adis16400_set_irq(st->indio_dev, state);
}

int adis16400_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct adis16400_state *st = indio_dev->dev_data;
	char *name;

	name = kasprintf(GFP_KERNEL,
			 "%s-dev%d",
			 spi_get_device_id(st->us)->name,
			 indio_dev->id);
	if (name == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	st->trig = iio_allocate_trigger_named(name);
	if (st->trig == NULL) {
		ret = -ENOMEM;
		goto error_free_name;
	}

	ret = request_irq(st->us->irq,
			  &iio_trigger_generic_data_rdy_poll,
			  IRQF_TRIGGER_RISING,
			  "adis16400",
			  st->trig);
	if (ret)
		goto error_free_trig;

	st->trig->dev.parent = &st->us->dev;
	st->trig->owner = THIS_MODULE;
	st->trig->private_data = st;
	st->trig->set_trigger_state = &adis16400_data_rdy_trigger_set_state;
	st->trig->control_attrs = &adis16400_trigger_attr_group;
	ret = iio_trigger_register(st->trig);

	/* select default trigger */
	indio_dev->trig = st->trig;
	if (ret)
		goto error_free_irq;

	return 0;

error_free_irq:
	free_irq(st->us->irq, st->trig);
error_free_trig:
	iio_free_trigger(st->trig);
error_free_name:
	kfree(name);
error_ret:
	return ret;
}

void adis16400_remove_trigger(struct iio_dev *indio_dev)
{
	struct adis16400_state *state = indio_dev->dev_data;

	iio_trigger_unregister(state->trig);
	kfree(state->trig->name);
	free_irq(state->us->irq, state->trig);
	iio_free_trigger(state->trig);
}
