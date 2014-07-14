/*
 * Copyright (c) 2014 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "xfs.h"
#include "xfs_sysfs.h"
#include "xfs_log_format.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"

struct xfs_sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(char *buf, void *data);
	ssize_t (*store)(const char *buf, size_t count, void *data);
};

static inline struct xfs_sysfs_attr *
to_attr(struct attribute *attr)
{
	return container_of(attr, struct xfs_sysfs_attr, attr);
}

#define XFS_SYSFS_ATTR_RW(name) \
	static struct xfs_sysfs_attr xfs_sysfs_attr_##name = __ATTR_RW(name)
#define XFS_SYSFS_ATTR_RO(name) \
	static struct xfs_sysfs_attr xfs_sysfs_attr_##name = __ATTR_RO(name)

#define ATTR_LIST(name) &xfs_sysfs_attr_##name.attr

/*
 * xfs_mount kobject. This currently has no attributes and thus no need for show
 * and store helpers. The mp kobject serves as the per-mount parent object that
 * is identified by the fsname under sysfs.
 */

struct kobj_type xfs_mp_ktype = {
	.release = xfs_sysfs_release,
};

/* xlog */

static struct attribute *xfs_log_attrs[] = {
	NULL,
};

static inline struct xlog *
to_xlog(struct kobject *kobject)
{
	struct xfs_kobj *kobj = to_kobj(kobject);
	return container_of(kobj, struct xlog, l_kobj);
}

STATIC ssize_t
xfs_log_show(
	struct kobject		*kobject,
	struct attribute	*attr,
	char			*buf)
{
	struct xlog *log = to_xlog(kobject);
	struct xfs_sysfs_attr *xfs_attr = to_attr(attr);

	return xfs_attr->show ? xfs_attr->show(buf, log) : 0;
}

STATIC ssize_t
xfs_log_store(
	struct kobject		*kobject,
	struct attribute	*attr,
	const char		*buf,
	size_t			count)
{
	struct xlog *log = to_xlog(kobject);
	struct xfs_sysfs_attr *xfs_attr = to_attr(attr);

	return xfs_attr->store ? xfs_attr->store(buf, count, log) : 0;
}

static struct sysfs_ops xfs_log_ops = {
	.show = xfs_log_show,
	.store = xfs_log_store,
};

struct kobj_type xfs_log_ktype = {
	.release = xfs_sysfs_release,
	.sysfs_ops = &xfs_log_ops,
	.default_attrs = xfs_log_attrs,
};
