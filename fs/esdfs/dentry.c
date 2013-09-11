/*
 * Copyright (c) 1998-2014 Erez Zadok
 * Copyright (c) 2009	   Shrikar Archak
 * Copyright (c) 2003-2014 Stony Brook University
 * Copyright (c) 2003-2014 The Research Foundation of SUNY
 * Copyright (C) 2013-2014 Motorola Mobility, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ctype.h>
#include "esdfs.h"

/*
 * returns: -ERRNO if error (returned to user)
 *          0: tell VFS to invalidate dentry
 *          1: dentry is valid
 */
static int esdfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct path lower_path;
	struct path lower_parent_path;
	struct dentry *parent_dentry = NULL;
	struct dentry *lower_dentry = NULL;
	struct dentry *lower_parent_dentry = NULL;
	int err = 1;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	/* short-circuit if it's root */
	spin_lock(&dentry->d_lock);
	if (IS_ROOT(dentry)) {
		spin_unlock(&dentry->d_lock);
		return 1;
	}
	spin_unlock(&dentry->d_lock);

	esdfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = dget_parent(lower_dentry);

	parent_dentry = dget_parent(dentry);
	esdfs_get_lower_path(parent_dentry, &lower_parent_path);

	if (lower_parent_path.dentry != lower_parent_dentry)
		goto drop;

	/* can't do strcmp if lower is hashed */
	spin_lock(&lower_dentry->d_lock);
	if (d_unhashed(lower_dentry)) {
		spin_unlock(&lower_dentry->d_lock);
		goto drop;
	}

	spin_lock(&dentry->d_lock);

	if (lower_dentry->d_name.len != dentry->d_name.len ||
	    strncasecmp(lower_dentry->d_name.name,
			dentry->d_name.name,
			dentry->d_name.len) != 0) {
		err = 0;
		__d_drop(dentry);	/* already holding spin lock */
	}

	spin_unlock(&dentry->d_lock);
	spin_unlock(&lower_dentry->d_lock);

	goto out;

drop:
	d_drop(dentry);
	err = 0;
out:
	esdfs_put_lower_path(parent_dentry, &lower_parent_path);
	dput(parent_dentry);
	dput(lower_parent_dentry);
	esdfs_put_lower_path(dentry, &lower_path);
	return err;
}

/* directly from fs/fat/namei_vfat.c */
static unsigned int __vfat_striptail_len(unsigned int len, const char *name)
{
	while (len && name[len - 1] == '.')
		len--;
	return len;
}

static unsigned int vfat_striptail_len(const struct qstr *qstr)
{
	return __vfat_striptail_len(qstr->len, qstr->name);
}


/* based on vfat_hashi() in fs/fat/namei_vfat.c (no code pages) */
static int esdfs_d_hash(const struct dentry *dentry, struct qstr *qstr)
{
	const unsigned char *name;
	unsigned int len;
	unsigned long hash;

	name = qstr->name;
	len = vfat_striptail_len(qstr);

	hash = init_name_hash();
	while (len--)
		hash = partial_name_hash(tolower(*name++), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

/* based on vfat_cmpi() in fs/fat/namei_vfat.c (no code pages) */
static int esdfs_d_compare(const struct dentry *parent,
			   const struct dentry *dentry, unsigned int len,
			   const char *str, const struct qstr *name)
{
	unsigned int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = vfat_striptail_len(name);
	blen = __vfat_striptail_len(len, str);
	if (alen == blen) {
		if (strncasecmp(name->name, str, alen) == 0)
			return 0;
	}
	return 1;
}

static void esdfs_d_release(struct dentry *dentry)
{
	/* release and reset the lower paths */
	esdfs_put_reset_lower_path(dentry);
	free_dentry_private_data(dentry);
	return;
}

const struct dentry_operations esdfs_dops = {
	.d_revalidate	= esdfs_d_revalidate,
	.d_hash		= esdfs_d_hash,
	.d_compare	= esdfs_d_compare,
	.d_release	= esdfs_d_release,
};
