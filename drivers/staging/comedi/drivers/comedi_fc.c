/*
 * comedi_fc.c
 * This is a place for code driver writers wish to share between
 * two or more drivers.  fc is short for frank-common.
 *
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Copyright (C) 2002 Frank Mori Hess
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include "../comedidev.h"

#include "comedi_fc.h"

/* Writes an array of data points to comedi's buffer */
unsigned int cfc_write_array_to_buffer(struct comedi_subdevice *s,
				       const void *data, unsigned int num_bytes)
{
	struct comedi_async *async = s->async;
	unsigned int retval;

	if (num_bytes == 0)
		return 0;

	retval = comedi_buf_write_alloc(s, num_bytes);
	if (retval != num_bytes) {
		dev_warn(s->device->class_dev, "buffer overrun\n");
		async->events |= COMEDI_CB_OVERFLOW;
		return 0;
	}

	comedi_buf_memcpy_to(s, 0, data, num_bytes);
	comedi_buf_write_free(s, num_bytes);
	comedi_inc_scan_progress(s, num_bytes);
	async->events |= COMEDI_CB_BLOCK;

	return num_bytes;
}
EXPORT_SYMBOL_GPL(cfc_write_array_to_buffer);

unsigned int cfc_read_array_from_buffer(struct comedi_subdevice *s,
					void *data, unsigned int num_bytes)
{
	if (num_bytes == 0)
		return 0;

	num_bytes = comedi_buf_read_alloc(s, num_bytes);
	comedi_buf_memcpy_from(s, 0, data, num_bytes);
	comedi_buf_read_free(s, num_bytes);
	comedi_inc_scan_progress(s, num_bytes);
	s->async->events |= COMEDI_CB_BLOCK;

	return num_bytes;
}
EXPORT_SYMBOL_GPL(cfc_read_array_from_buffer);

static int __init comedi_fc_init_module(void)
{
	return 0;
}
module_init(comedi_fc_init_module);

static void __exit comedi_fc_cleanup_module(void)
{
}
module_exit(comedi_fc_cleanup_module);

MODULE_AUTHOR("Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_DESCRIPTION("Shared functions for Comedi low-level drivers");
MODULE_LICENSE("GPL");
