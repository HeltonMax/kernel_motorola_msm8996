/*
    comedi/drivers/adl_pci7230.c

    Hardware comedi driver fot PCI7230 Adlink card
    Copyright (C) 2010 David Fernandez <dfcastelao@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
/*
Driver: adl_pci7230
Description: Driver for the Adlink PCI-7230 32 ch. isolated digital io board
Devices: [ADLink] PCI-7230 (adl_pci7230)
Author: David Fernandez <dfcastelao@gmail.com>
Status: experimental
Updated: Mon, 14 Apr 2008 15:08:14 +0100

Configuration Options:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)
  If bus/slot is not specified, the first supported
  PCI device found will be used.
*/

#include "../comedidev.h"
#include <linux/kernel.h>
#include "comedi_pci.h"

#define PCI7230_DI      0x00
#define PCI7230_DO	    0x00

#define PCI_DEVICE_ID_PCI7230 0x7230

static DEFINE_PCI_DEVICE_TABLE(adl_pci7230_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_PCI7230) },
	{0}
};

MODULE_DEVICE_TABLE(pci, adl_pci7230_pci_table);

struct adl_pci7230_private {
	int data;
	struct pci_dev *pci_dev;
};

#define devpriv ((struct adl_pci7230_private *)dev->private)

static int adl_pci7230_attach(struct comedi_device *dev,
	struct comedi_devconfig *it);
static int adl_pci7230_detach(struct comedi_device *dev);
static struct comedi_driver driver_adl_pci7230 = {
	.driver_name = "adl_pci7230",
	.module = THIS_MODULE,
	.attach = adl_pci7230_attach,
	.detach = adl_pci7230_detach,
};

/* Digital IO */

static int adl_pci7230_di_insn_bits(struct comedi_device *dev,
	struct comedi_subdevice *s,
	struct comedi_insn *insn,
	unsigned int *data);

static int adl_pci7230_do_insn_bits(struct comedi_device *dev,
	struct comedi_subdevice *s,
	struct comedi_insn *insn,
	unsigned int *data);

static int adl_pci7230_attach(struct comedi_device *dev,
	struct comedi_devconfig *it)
{
	struct pci_dev *pcidev = NULL;
	struct comedi_subdevice *s;
	int bus, slot;

	printk(KERN_INFO "comedi%d: adl_pci7230\n", dev->minor);

	dev->board_name = "pci7230";
	bus = it->options[0];
	slot = it->options[1];

	if (alloc_private(dev, sizeof(struct adl_pci7230_private)) < 0)
		return -ENOMEM;

	if (alloc_subdevices(dev, 2) < 0)
		return -ENOMEM;

	for_each_pci_dev(pcidev) {
		if (pcidev->vendor == PCI_VENDOR_ID_ADLINK &&
			pcidev->device == PCI_DEVICE_ID_PCI7230) {
			if (bus || slot) {
				/* requested particular bus/slot */
				if (pcidev->bus->number != bus ||
					PCI_SLOT(pcidev->devfn) != slot) {
					continue;
				}
			}
			devpriv->pci_dev = pcidev;
			break;
		}
	}
	if (pcidev == NULL) {
		printk(KERN_ERR "comedi%d: no supported board found! (req. bus/slot : %d/%d)\n",
			dev->minor, bus, slot);
		return -EIO;
	}
	if (comedi_pci_enable(pcidev, "adl_pci7230") < 0) {
		printk(KERN_ERR "comedi%d: Failed to enable PCI device and request regions\n",
			dev->minor);
		return -EIO;
	}
	dev->iobase = pci_resource_start(pcidev, 2);
	printk(KERN_DEBUG "comedi: base addr %4lx\n", dev->iobase);

	s = dev->subdevices + 0;
	/* Isolated do */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 16;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = adl_pci7230_do_insn_bits;

	s = dev->subdevices + 1;
	/* Isolated di */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 16;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = adl_pci7230_di_insn_bits;

	printk(KERN_DEBUG "comedi: attached\n");

	return 1;
}

static int adl_pci7230_detach(struct comedi_device *dev)
{
	printk(KERN_DEBUG "comedi%d: pci7230: remove\n", dev->minor);

	if (devpriv && devpriv->pci_dev) {
		if (dev->iobase)
			comedi_pci_disable(devpriv->pci_dev);
		pci_dev_put(devpriv->pci_dev);
	}

	return 0;
}

static int adl_pci7230_do_insn_bits(struct comedi_device *dev,
	struct comedi_subdevice *s,
	struct comedi_insn *insn,
	unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);

		outl((s->state  << 16) & 0xffffffff, dev->iobase + PCI7230_DO);
	}

	return 2;
}

static int adl_pci7230_di_insn_bits(struct comedi_device *dev,
	struct comedi_subdevice *s,
	struct comedi_insn *insn,
	unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	data[1] = inl(dev->iobase + PCI7230_DI) & 0xffffffff;

	return 2;
}

static int __devinit driver_adl_pci7230_pci_probe(struct pci_dev *dev,
						  const struct pci_device_id
						  *ent)
{
	return comedi_pci_auto_config(dev, driver_adl_pci7230.driver_name);
}

static void __devexit driver_adl_pci7230_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver driver_adl_pci7230_pci_driver = {
	.id_table = adl_pci7230_pci_table,
	.probe = &driver_adl_pci7230_pci_probe,
	.remove = __devexit_p(&driver_adl_pci7230_pci_remove)
};

static int __init driver_adl_pci7230_init_module(void)
{
	int retval;

	retval = comedi_driver_register(&driver_adl_pci7230);
	if (retval < 0)
		return retval;

	driver_adl_pci7230_pci_driver.name =
	    (char *)driver_adl_pci7230.driver_name;
	return pci_register_driver(&driver_adl_pci7230_pci_driver);
}

static void __exit driver_adl_pci7230_cleanup_module(void)
{
	pci_unregister_driver(&driver_adl_pci7230_pci_driver);
	comedi_driver_unregister(&driver_adl_pci7230);
}

module_init(driver_adl_pci7230_init_module);
module_exit(driver_adl_pci7230_cleanup_module);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
