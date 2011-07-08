/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_DIR2_SF_H__
#define	__XFS_DIR2_SF_H__

/*
 * Directory layout when stored internal to an inode.
 *
 * Small directories are packed as tightly as possible so as to fit into the
 * literal area of the inode.  They consist of a single xfs_dir2_sf_hdr header
 * followed by zero or more xfs_dir2_sf_entry structures.  Due the different
 * inode number storage size and the variable length name field in
 * the xfs_dir2_sf_entry all these structure are variable length, and the
 * accessors in this file should be used to iterate over them.
 */

struct uio;
struct xfs_dabuf;
struct xfs_da_args;
struct xfs_dir2_block;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * Inode number stored as 8 8-bit values.
 */
typedef	struct { __uint8_t i[8]; } xfs_dir2_ino8_t;

/*
 * Inode number stored as 4 8-bit values.
 * Works a lot of the time, when all the inode numbers in a directory
 * fit in 32 bits.
 */
typedef struct { __uint8_t i[4]; } xfs_dir2_ino4_t;

typedef union {
	xfs_dir2_ino8_t	i8;
	xfs_dir2_ino4_t	i4;
} xfs_dir2_inou_t;
#define	XFS_DIR2_MAX_SHORT_INUM	((xfs_ino_t)0xffffffffULL)

/*
 * Normalized offset (in a data block) of the entry, really xfs_dir2_data_off_t.
 * Only need 16 bits, this is the byte offset into the single block form.
 */
typedef struct { __uint8_t i[2]; } __arch_pack xfs_dir2_sf_off_t;

/*
 * The parent directory has a dedicated field, and the self-pointer must
 * be calculated on the fly.
 *
 * Entries are packed toward the top as tightly as possible, and thus may
 * be misaligned.  Care needs to be taken to access them through special
 * helpers or copy them into aligned variables first.
 */
typedef struct xfs_dir2_sf_hdr {
	__uint8_t		count;		/* count of entries */
	__uint8_t		i8count;	/* count of 8-byte inode #s */
	xfs_dir2_inou_t		parent;		/* parent dir inode number */
} __arch_pack xfs_dir2_sf_hdr_t;

typedef struct xfs_dir2_sf_entry {
	__uint8_t		namelen;	/* actual name length */
	xfs_dir2_sf_off_t	offset;		/* saved offset */
	__uint8_t		name[1];	/* name, variable size */
	xfs_dir2_inou_t		inumber;	/* inode number, var. offset */
} __arch_pack xfs_dir2_sf_entry_t; 

static inline int xfs_dir2_sf_hdr_size(int i8count)
{
	return ((uint)sizeof(xfs_dir2_sf_hdr_t) - \
		((i8count) == 0) * \
		((uint)sizeof(xfs_dir2_ino8_t) - (uint)sizeof(xfs_dir2_ino4_t)));
}

static inline xfs_dir2_data_aoff_t
xfs_dir2_sf_get_offset(xfs_dir2_sf_entry_t *sfep)
{
	return INT_GET_UNALIGNED_16_BE(&(sfep)->offset.i);
}

static inline void
xfs_dir2_sf_put_offset(xfs_dir2_sf_entry_t *sfep, xfs_dir2_data_aoff_t off)
{
	INT_SET_UNALIGNED_16_BE(&(sfep)->offset.i, off);
}

static inline int xfs_dir2_sf_entsize_byname(xfs_dir2_sf_hdr_t *sfp, int len)
{
	return ((uint)sizeof(xfs_dir2_sf_entry_t) - 1 + (len) - \
		((sfp)->i8count == 0) * \
		((uint)sizeof(xfs_dir2_ino8_t) - (uint)sizeof(xfs_dir2_ino4_t)));
}

static inline int
xfs_dir2_sf_entsize_byentry(xfs_dir2_sf_hdr_t *sfp, xfs_dir2_sf_entry_t *sfep)
{
	return ((uint)sizeof(xfs_dir2_sf_entry_t) - 1 + (sfep)->namelen - \
		((sfp)->i8count == 0) * \
		((uint)sizeof(xfs_dir2_ino8_t) - (uint)sizeof(xfs_dir2_ino4_t)));
}

static inline struct xfs_dir2_sf_entry *
xfs_dir2_sf_firstentry(struct xfs_dir2_sf_hdr *hdr)
{
	return (struct xfs_dir2_sf_entry *)
		((char *)hdr + xfs_dir2_sf_hdr_size(hdr->i8count));
}

static inline struct xfs_dir2_sf_entry *
xfs_dir2_sf_nextentry(struct xfs_dir2_sf_hdr *hdr,
		struct xfs_dir2_sf_entry *sfep)
{
	return (struct xfs_dir2_sf_entry *)
		((char *)sfep + xfs_dir2_sf_entsize_byentry(hdr, sfep));
}

/*
 * Functions.
 */
extern xfs_ino_t xfs_dir2_sf_get_parent_ino(struct xfs_dir2_sf_hdr *sfp);
extern xfs_ino_t xfs_dir2_sfe_get_ino(struct xfs_dir2_sf_hdr *sfp,
				      struct xfs_dir2_sf_entry *sfep);
extern int xfs_dir2_block_sfsize(struct xfs_inode *dp,
				 struct xfs_dir2_block *block,
				 xfs_dir2_sf_hdr_t *sfhp);
extern int xfs_dir2_block_to_sf(struct xfs_da_args *args, struct xfs_dabuf *bp,
				int size, xfs_dir2_sf_hdr_t *sfhp);
extern int xfs_dir2_sf_addname(struct xfs_da_args *args);
extern int xfs_dir2_sf_create(struct xfs_da_args *args, xfs_ino_t pino);
extern int xfs_dir2_sf_getdents(struct xfs_inode *dp, void *dirent,
				xfs_off_t *offset, filldir_t filldir);
extern int xfs_dir2_sf_lookup(struct xfs_da_args *args);
extern int xfs_dir2_sf_removename(struct xfs_da_args *args);
extern int xfs_dir2_sf_replace(struct xfs_da_args *args);

#endif	/* __XFS_DIR2_SF_H__ */
