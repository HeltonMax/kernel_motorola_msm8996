/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * acl.h
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef OCFS2_ACL_H
#define OCFS2_ACL_H

#include <linux/posix_acl_xattr.h>

struct ocfs2_acl_entry {
	__le16 e_tag;
	__le16 e_perm;
	__le32 e_id;
};

#ifdef CONFIG_OCFS2_FS_POSIX_ACL

extern int ocfs2_check_acl(struct inode *, int);

#else /* CONFIG_OCFS2_FS_POSIX_ACL*/

#define ocfs2_check_acl NULL

#endif /* CONFIG_OCFS2_FS_POSIX_ACL*/

#endif /* OCFS2_ACL_H */
