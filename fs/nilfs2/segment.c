/*
 * segment.c - NILFS segment constructor.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 *
 */

#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/bio.h>
#include <linux/completion.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/crc32.h>
#include <linux/pagevec.h>
#include "nilfs.h"
#include "btnode.h"
#include "page.h"
#include "segment.h"
#include "sufile.h"
#include "cpfile.h"
#include "ifile.h"
#include "seglist.h"
#include "segbuf.h"


/*
 * Segment constructor
 */
#define SC_N_INODEVEC	16   /* Size of locally allocated inode vector */

#define SC_MAX_SEGDELTA 64   /* Upper limit of the number of segments
				appended in collection retry loop */

/* Construction mode */
enum {
	SC_LSEG_SR = 1,	/* Make a logical segment having a super root */
	SC_LSEG_DSYNC,	/* Flush data blocks of a given file and make
			   a logical segment without a super root */
	SC_FLUSH_FILE,	/* Flush data files, leads to segment writes without
			   creating a checkpoint */
	SC_FLUSH_DAT,	/* Flush DAT file. This also creates segments without
			   a checkpoint */
};

/* Stage numbers of dirty block collection */
enum {
	NILFS_ST_INIT = 0,
	NILFS_ST_GC,		/* Collecting dirty blocks for GC */
	NILFS_ST_FILE,
	NILFS_ST_SKETCH,
	NILFS_ST_IFILE,
	NILFS_ST_CPFILE,
	NILFS_ST_SUFILE,
	NILFS_ST_DAT,
	NILFS_ST_SR,		/* Super root */
	NILFS_ST_DSYNC,		/* Data sync blocks */
	NILFS_ST_DONE,
};

/* State flags of collection */
#define NILFS_CF_NODE		0x0001	/* Collecting node blocks */
#define NILFS_CF_IFILE_STARTED	0x0002	/* IFILE stage has started */
#define NILFS_CF_HISTORY_MASK	(NILFS_CF_IFILE_STARTED)

/* Operations depending on the construction mode and file type */
struct nilfs_sc_operations {
	int (*collect_data)(struct nilfs_sc_info *, struct buffer_head *,
			    struct inode *);
	int (*collect_node)(struct nilfs_sc_info *, struct buffer_head *,
			    struct inode *);
	int (*collect_bmap)(struct nilfs_sc_info *, struct buffer_head *,
			    struct inode *);
	void (*write_data_binfo)(struct nilfs_sc_info *,
				 struct nilfs_segsum_pointer *,
				 union nilfs_binfo *);
	void (*write_node_binfo)(struct nilfs_sc_info *,
				 struct nilfs_segsum_pointer *,
				 union nilfs_binfo *);
};

/*
 * Other definitions
 */
static void nilfs_segctor_start_timer(struct nilfs_sc_info *);
static void nilfs_segctor_do_flush(struct nilfs_sc_info *, int);
static void nilfs_segctor_do_immediate_flush(struct nilfs_sc_info *);
static void nilfs_dispose_list(struct nilfs_sb_info *, struct list_head *,
			       int);

#define nilfs_cnt32_gt(a, b)   \
	(typecheck(__u32, a) && typecheck(__u32, b) && \
	 ((__s32)(b) - (__s32)(a) < 0))
#define nilfs_cnt32_ge(a, b)   \
	(typecheck(__u32, a) && typecheck(__u32, b) && \
	 ((__s32)(a) - (__s32)(b) >= 0))
#define nilfs_cnt32_lt(a, b)  nilfs_cnt32_gt(b, a)
#define nilfs_cnt32_le(a, b)  nilfs_cnt32_ge(b, a)

/*
 * Transaction
 */
static struct kmem_cache *nilfs_transaction_cachep;

/**
 * nilfs_init_transaction_cache - create a cache for nilfs_transaction_info
 *
 * nilfs_init_transaction_cache() creates a slab cache for the struct
 * nilfs_transaction_info.
 *
 * Return Value: On success, it returns 0. On error, one of the following
 * negative error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_init_transaction_cache(void)
{
	nilfs_transaction_cachep =
		kmem_cache_create("nilfs2_transaction_cache",
				  sizeof(struct nilfs_transaction_info),
				  0, SLAB_RECLAIM_ACCOUNT, NULL);
	return (nilfs_transaction_cachep == NULL) ? -ENOMEM : 0;
}

/**
 * nilfs_detroy_transaction_cache - destroy the cache for transaction info
 *
 * nilfs_destroy_transaction_cache() frees the slab cache for the struct
 * nilfs_transaction_info.
 */
void nilfs_destroy_transaction_cache(void)
{
	kmem_cache_destroy(nilfs_transaction_cachep);
}

static int nilfs_prepare_segment_lock(struct nilfs_transaction_info *ti)
{
	struct nilfs_transaction_info *cur_ti = current->journal_info;
	void *save = NULL;

	if (cur_ti) {
		if (cur_ti->ti_magic == NILFS_TI_MAGIC)
			return ++cur_ti->ti_count;
		else {
			/*
			 * If journal_info field is occupied by other FS,
			 * we save it and restore on nilfs_transaction_end().
			 * But this should never happen.
			 */
			printk(KERN_WARNING
			       "NILFS warning: journal info from a different "
			       "FS\n");
			save = current->journal_info;
		}
	}
	if (!ti) {
		ti = kmem_cache_alloc(nilfs_transaction_cachep, GFP_NOFS);
		if (!ti)
			return -ENOMEM;
		ti->ti_flags = NILFS_TI_DYNAMIC_ALLOC;
	} else {
		ti->ti_flags = 0;
	}
	ti->ti_count = 0;
	ti->ti_save = save;
	ti->ti_magic = NILFS_TI_MAGIC;
	current->journal_info = ti;
	return 0;
}

/**
 * nilfs_transaction_begin - start indivisible file operations.
 * @sb: super block
 * @ti: nilfs_transaction_info
 * @vacancy_check: flags for vacancy rate checks
 *
 * nilfs_transaction_begin() acquires a reader/writer semaphore, called
 * the segment semaphore, to make a segment construction and write tasks
 * exclusive.  The function is used with nilfs_transaction_end() in pairs.
 * The region enclosed by these two functions can be nested.  To avoid a
 * deadlock, the semaphore is only acquired or released in the outermost call.
 *
 * This function allocates a nilfs_transaction_info struct to keep context
 * information on it.  It is initialized and hooked onto the current task in
 * the outermost call.  If a pre-allocated struct is given to @ti, it is used
 * instead; othewise a new struct is assigned from a slab.
 *
 * When @vacancy_check flag is set, this function will check the amount of
 * free space, and will wait for the GC to reclaim disk space if low capacity.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-ERESTARTSYS - Interrupted
 *
 * %-ENOSPC - No space left on device
 */
int nilfs_transaction_begin(struct super_block *sb,
			    struct nilfs_transaction_info *ti,
			    int vacancy_check)
{
	struct nilfs_sb_info *sbi;
	struct the_nilfs *nilfs;
	int ret = nilfs_prepare_segment_lock(ti);

	if (unlikely(ret < 0))
		return ret;
	if (ret > 0)
		return 0;

	sbi = NILFS_SB(sb);
	nilfs = sbi->s_nilfs;
	down_read(&nilfs->ns_segctor_sem);
	if (vacancy_check && nilfs_near_disk_full(nilfs)) {
		up_read(&nilfs->ns_segctor_sem);
		ret = -ENOSPC;
		goto failed;
	}
	return 0;

 failed:
	ti = current->journal_info;
	current->journal_info = ti->ti_save;
	if (ti->ti_flags & NILFS_TI_DYNAMIC_ALLOC)
		kmem_cache_free(nilfs_transaction_cachep, ti);
	return ret;
}

/**
 * nilfs_transaction_end - end indivisible file operations.
 * @sb: super block
 * @commit: commit flag (0 for no change)
 *
 * nilfs_transaction_end() releases the read semaphore which is
 * acquired by nilfs_transaction_begin(). Its releasing is only done
 * in outermost call of this function. If the nilfs_transaction_info
 * was allocated dynamically, it is given back to a slab cache.
 */
int nilfs_transaction_end(struct super_block *sb, int commit)
{
	struct nilfs_transaction_info *ti = current->journal_info;
	struct nilfs_sb_info *sbi;
	struct nilfs_sc_info *sci;
	int err = 0;

	BUG_ON(ti == NULL || ti->ti_magic != NILFS_TI_MAGIC);

	if (commit)
		ti->ti_flags |= NILFS_TI_COMMIT;
	if (ti->ti_count > 0) {
		ti->ti_count--;
		return 0;
	}
	sbi = NILFS_SB(sb);
	sci = NILFS_SC(sbi);
	if (sci != NULL) {
		if (ti->ti_flags & NILFS_TI_COMMIT)
			nilfs_segctor_start_timer(sci);
		if (atomic_read(&sbi->s_nilfs->ns_ndirtyblks) >
		    sci->sc_watermark)
			nilfs_segctor_do_flush(sci, 0);
	}
	up_read(&sbi->s_nilfs->ns_segctor_sem);
	current->journal_info = ti->ti_save;

	if (ti->ti_flags & NILFS_TI_SYNC)
		err = nilfs_construct_segment(sb);
	if (ti->ti_flags & NILFS_TI_DYNAMIC_ALLOC)
		kmem_cache_free(nilfs_transaction_cachep, ti);
	return err;
}

void nilfs_relax_pressure_in_lock(struct super_block *sb)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct nilfs_sc_info *sci = NILFS_SC(sbi);
	struct the_nilfs *nilfs = sbi->s_nilfs;

	if (!sci || !sci->sc_flush_request)
		return;

	set_bit(NILFS_SC_PRIOR_FLUSH, &sci->sc_flags);
	up_read(&nilfs->ns_segctor_sem);

	down_write(&nilfs->ns_segctor_sem);
	if (sci->sc_flush_request &&
	    test_bit(NILFS_SC_PRIOR_FLUSH, &sci->sc_flags)) {
		struct nilfs_transaction_info *ti = current->journal_info;

		ti->ti_flags |= NILFS_TI_WRITER;
		nilfs_segctor_do_immediate_flush(sci);
		ti->ti_flags &= ~NILFS_TI_WRITER;
	}
	downgrade_write(&nilfs->ns_segctor_sem);
}

static void nilfs_transaction_lock(struct nilfs_sb_info *sbi,
				   struct nilfs_transaction_info *ti,
				   int gcflag)
{
	struct nilfs_transaction_info *cur_ti = current->journal_info;

	BUG_ON(cur_ti);
	BUG_ON(!ti);
	ti->ti_flags = NILFS_TI_WRITER;
	ti->ti_count = 0;
	ti->ti_save = cur_ti;
	ti->ti_magic = NILFS_TI_MAGIC;
	INIT_LIST_HEAD(&ti->ti_garbage);
	current->journal_info = ti;

	for (;;) {
		down_write(&sbi->s_nilfs->ns_segctor_sem);
		if (!test_bit(NILFS_SC_PRIOR_FLUSH, &NILFS_SC(sbi)->sc_flags))
			break;

		nilfs_segctor_do_immediate_flush(NILFS_SC(sbi));

		up_write(&sbi->s_nilfs->ns_segctor_sem);
		yield();
	}
	if (gcflag)
		ti->ti_flags |= NILFS_TI_GC;
}

static void nilfs_transaction_unlock(struct nilfs_sb_info *sbi)
{
	struct nilfs_transaction_info *ti = current->journal_info;

	BUG_ON(ti == NULL || ti->ti_magic != NILFS_TI_MAGIC);
	BUG_ON(ti->ti_count > 0);

	up_write(&sbi->s_nilfs->ns_segctor_sem);
	current->journal_info = ti->ti_save;
	if (!list_empty(&ti->ti_garbage))
		nilfs_dispose_list(sbi, &ti->ti_garbage, 0);
}

static void *nilfs_segctor_map_segsum_entry(struct nilfs_sc_info *sci,
					    struct nilfs_segsum_pointer *ssp,
					    unsigned bytes)
{
	struct nilfs_segment_buffer *segbuf = sci->sc_curseg;
	unsigned blocksize = sci->sc_super->s_blocksize;
	void *p;

	if (unlikely(ssp->offset + bytes > blocksize)) {
		ssp->offset = 0;
		BUG_ON(NILFS_SEGBUF_BH_IS_LAST(ssp->bh,
					       &segbuf->sb_segsum_buffers));
		ssp->bh = NILFS_SEGBUF_NEXT_BH(ssp->bh);
	}
	p = ssp->bh->b_data + ssp->offset;
	ssp->offset += bytes;
	return p;
}

/**
 * nilfs_segctor_reset_segment_buffer - reset the current segment buffer
 * @sci: nilfs_sc_info
 */
static int nilfs_segctor_reset_segment_buffer(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf = sci->sc_curseg;
	struct buffer_head *sumbh;
	unsigned sumbytes;
	unsigned flags = 0;
	int err;

	if (nilfs_doing_gc())
		flags = NILFS_SS_GC;
	err = nilfs_segbuf_reset(segbuf, flags, sci->sc_seg_ctime);
	if (unlikely(err))
		return err;

	sumbh = NILFS_SEGBUF_FIRST_BH(&segbuf->sb_segsum_buffers);
	sumbytes = segbuf->sb_sum.sumbytes;
	sci->sc_finfo_ptr.bh = sumbh;  sci->sc_finfo_ptr.offset = sumbytes;
	sci->sc_binfo_ptr.bh = sumbh;  sci->sc_binfo_ptr.offset = sumbytes;
	sci->sc_blk_cnt = sci->sc_datablk_cnt = 0;
	return 0;
}

static int nilfs_segctor_feed_segment(struct nilfs_sc_info *sci)
{
	sci->sc_nblk_this_inc += sci->sc_curseg->sb_sum.nblocks;
	if (NILFS_SEGBUF_IS_LAST(sci->sc_curseg, &sci->sc_segbufs))
		return -E2BIG; /* The current segment is filled up
				  (internal code) */
	sci->sc_curseg = NILFS_NEXT_SEGBUF(sci->sc_curseg);
	return nilfs_segctor_reset_segment_buffer(sci);
}

static int nilfs_segctor_add_super_root(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf = sci->sc_curseg;
	int err;

	if (segbuf->sb_sum.nblocks >= segbuf->sb_rest_blocks) {
		err = nilfs_segctor_feed_segment(sci);
		if (err)
			return err;
		segbuf = sci->sc_curseg;
	}
	err = nilfs_segbuf_extend_payload(segbuf, &sci->sc_super_root);
	if (likely(!err))
		segbuf->sb_sum.flags |= NILFS_SS_SR;
	return err;
}

/*
 * Functions for making segment summary and payloads
 */
static int nilfs_segctor_segsum_block_required(
	struct nilfs_sc_info *sci, const struct nilfs_segsum_pointer *ssp,
	unsigned binfo_size)
{
	unsigned blocksize = sci->sc_super->s_blocksize;
	/* Size of finfo and binfo is enough small against blocksize */

	return ssp->offset + binfo_size +
		(!sci->sc_blk_cnt ? sizeof(struct nilfs_finfo) : 0) >
		blocksize;
}

static void nilfs_segctor_begin_finfo(struct nilfs_sc_info *sci,
				      struct inode *inode)
{
	sci->sc_curseg->sb_sum.nfinfo++;
	sci->sc_binfo_ptr = sci->sc_finfo_ptr;
	nilfs_segctor_map_segsum_entry(
		sci, &sci->sc_binfo_ptr, sizeof(struct nilfs_finfo));
	/* skip finfo */
}

static void nilfs_segctor_end_finfo(struct nilfs_sc_info *sci,
				    struct inode *inode)
{
	struct nilfs_finfo *finfo;
	struct nilfs_inode_info *ii;
	struct nilfs_segment_buffer *segbuf;

	if (sci->sc_blk_cnt == 0)
		return;

	ii = NILFS_I(inode);
	finfo = nilfs_segctor_map_segsum_entry(sci, &sci->sc_finfo_ptr,
						 sizeof(*finfo));
	finfo->fi_ino = cpu_to_le64(inode->i_ino);
	finfo->fi_nblocks = cpu_to_le32(sci->sc_blk_cnt);
	finfo->fi_ndatablk = cpu_to_le32(sci->sc_datablk_cnt);
	finfo->fi_cno = cpu_to_le64(ii->i_cno);

	segbuf = sci->sc_curseg;
	segbuf->sb_sum.sumbytes = sci->sc_binfo_ptr.offset +
		sci->sc_super->s_blocksize * (segbuf->sb_sum.nsumblk - 1);
	sci->sc_finfo_ptr = sci->sc_binfo_ptr;
	sci->sc_blk_cnt = sci->sc_datablk_cnt = 0;
}

static int nilfs_segctor_add_file_block(struct nilfs_sc_info *sci,
					struct buffer_head *bh,
					struct inode *inode,
					unsigned binfo_size)
{
	struct nilfs_segment_buffer *segbuf;
	int required, err = 0;

 retry:
	segbuf = sci->sc_curseg;
	required = nilfs_segctor_segsum_block_required(
		sci, &sci->sc_binfo_ptr, binfo_size);
	if (segbuf->sb_sum.nblocks + required + 1 > segbuf->sb_rest_blocks) {
		nilfs_segctor_end_finfo(sci, inode);
		err = nilfs_segctor_feed_segment(sci);
		if (err)
			return err;
		goto retry;
	}
	if (unlikely(required)) {
		err = nilfs_segbuf_extend_segsum(segbuf);
		if (unlikely(err))
			goto failed;
	}
	if (sci->sc_blk_cnt == 0)
		nilfs_segctor_begin_finfo(sci, inode);

	nilfs_segctor_map_segsum_entry(sci, &sci->sc_binfo_ptr, binfo_size);
	/* Substitution to vblocknr is delayed until update_blocknr() */
	nilfs_segbuf_add_file_buffer(segbuf, bh);
	sci->sc_blk_cnt++;
 failed:
	return err;
}

static int nilfs_handle_bmap_error(int err, const char *fname,
				   struct inode *inode, struct super_block *sb)
{
	if (err == -EINVAL) {
		nilfs_error(sb, fname, "broken bmap (inode=%lu)\n",
			    inode->i_ino);
		err = -EIO;
	}
	return err;
}

/*
 * Callback functions that enumerate, mark, and collect dirty blocks
 */
static int nilfs_collect_file_data(struct nilfs_sc_info *sci,
				   struct buffer_head *bh, struct inode *inode)
{
	int err;

	/* BUG_ON(!buffer_dirty(bh)); */
	/* excluded by scan_dirty_data_buffers() */
	err = nilfs_bmap_propagate(NILFS_I(inode)->i_bmap, bh);
	if (unlikely(err < 0))
		return nilfs_handle_bmap_error(err, __func__, inode,
					       sci->sc_super);

	err = nilfs_segctor_add_file_block(sci, bh, inode,
					   sizeof(struct nilfs_binfo_v));
	if (!err)
		sci->sc_datablk_cnt++;
	return err;
}

static int nilfs_collect_file_node(struct nilfs_sc_info *sci,
				   struct buffer_head *bh,
				   struct inode *inode)
{
	int err;

	/* BUG_ON(!buffer_dirty(bh)); */
	/* excluded by scan_dirty_node_buffers() */
	err = nilfs_bmap_propagate(NILFS_I(inode)->i_bmap, bh);
	if (unlikely(err < 0))
		return nilfs_handle_bmap_error(err, __func__, inode,
					       sci->sc_super);
	return 0;
}

static int nilfs_collect_file_bmap(struct nilfs_sc_info *sci,
				   struct buffer_head *bh,
				   struct inode *inode)
{
	BUG_ON(!buffer_dirty(bh));
	return nilfs_segctor_add_file_block(sci, bh, inode, sizeof(__le64));
}

static void nilfs_write_file_data_binfo(struct nilfs_sc_info *sci,
					struct nilfs_segsum_pointer *ssp,
					union nilfs_binfo *binfo)
{
	struct nilfs_binfo_v *binfo_v = nilfs_segctor_map_segsum_entry(
		sci, ssp, sizeof(*binfo_v));
	*binfo_v = binfo->bi_v;
}

static void nilfs_write_file_node_binfo(struct nilfs_sc_info *sci,
					struct nilfs_segsum_pointer *ssp,
					union nilfs_binfo *binfo)
{
	__le64 *vblocknr = nilfs_segctor_map_segsum_entry(
		sci, ssp, sizeof(*vblocknr));
	*vblocknr = binfo->bi_v.bi_vblocknr;
}

struct nilfs_sc_operations nilfs_sc_file_ops = {
	.collect_data = nilfs_collect_file_data,
	.collect_node = nilfs_collect_file_node,
	.collect_bmap = nilfs_collect_file_bmap,
	.write_data_binfo = nilfs_write_file_data_binfo,
	.write_node_binfo = nilfs_write_file_node_binfo,
};

static int nilfs_collect_dat_data(struct nilfs_sc_info *sci,
				  struct buffer_head *bh, struct inode *inode)
{
	int err;

	err = nilfs_bmap_propagate(NILFS_I(inode)->i_bmap, bh);
	if (unlikely(err < 0))
		return nilfs_handle_bmap_error(err, __func__, inode,
					       sci->sc_super);

	err = nilfs_segctor_add_file_block(sci, bh, inode, sizeof(__le64));
	if (!err)
		sci->sc_datablk_cnt++;
	return err;
}

static int nilfs_collect_dat_bmap(struct nilfs_sc_info *sci,
				  struct buffer_head *bh, struct inode *inode)
{
	BUG_ON(!buffer_dirty(bh));
	return nilfs_segctor_add_file_block(sci, bh, inode,
					    sizeof(struct nilfs_binfo_dat));
}

static void nilfs_write_dat_data_binfo(struct nilfs_sc_info *sci,
				       struct nilfs_segsum_pointer *ssp,
				       union nilfs_binfo *binfo)
{
	__le64 *blkoff = nilfs_segctor_map_segsum_entry(sci, ssp,
							  sizeof(*blkoff));
	*blkoff = binfo->bi_dat.bi_blkoff;
}

static void nilfs_write_dat_node_binfo(struct nilfs_sc_info *sci,
				       struct nilfs_segsum_pointer *ssp,
				       union nilfs_binfo *binfo)
{
	struct nilfs_binfo_dat *binfo_dat =
		nilfs_segctor_map_segsum_entry(sci, ssp, sizeof(*binfo_dat));
	*binfo_dat = binfo->bi_dat;
}

struct nilfs_sc_operations nilfs_sc_dat_ops = {
	.collect_data = nilfs_collect_dat_data,
	.collect_node = nilfs_collect_file_node,
	.collect_bmap = nilfs_collect_dat_bmap,
	.write_data_binfo = nilfs_write_dat_data_binfo,
	.write_node_binfo = nilfs_write_dat_node_binfo,
};

struct nilfs_sc_operations nilfs_sc_dsync_ops = {
	.collect_data = nilfs_collect_file_data,
	.collect_node = NULL,
	.collect_bmap = NULL,
	.write_data_binfo = nilfs_write_file_data_binfo,
	.write_node_binfo = NULL,
};

static int nilfs_lookup_dirty_data_buffers(struct inode *inode,
					   struct list_head *listp,
					   struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf = sci->sc_curseg;
	struct address_space *mapping = inode->i_mapping;
	struct pagevec pvec;
	unsigned i, ndirties = 0, nlimit;
	pgoff_t index = 0;
	int err = 0;

	nlimit = sci->sc_segbuf_nblocks -
		(sci->sc_nblk_this_inc + segbuf->sb_sum.nblocks);
	pagevec_init(&pvec, 0);
 repeat:
	if (!pagevec_lookup_tag(&pvec, mapping, &index, PAGECACHE_TAG_DIRTY,
				PAGEVEC_SIZE))
		return 0;

	for (i = 0; i < pagevec_count(&pvec); i++) {
		struct buffer_head *bh, *head;
		struct page *page = pvec.pages[i];

		if (mapping->host) {
			lock_page(page);
			if (!page_has_buffers(page))
				create_empty_buffers(page,
						     1 << inode->i_blkbits, 0);
			unlock_page(page);
		}

		bh = head = page_buffers(page);
		do {
			if (buffer_dirty(bh)) {
				if (ndirties > nlimit) {
					err = -E2BIG;
					break;
				}
				get_bh(bh);
				list_add_tail(&bh->b_assoc_buffers, listp);
				ndirties++;
			}
			bh = bh->b_this_page;
		} while (bh != head);
	}
	pagevec_release(&pvec);
	cond_resched();

	if (!err)
		goto repeat;
	return err;
}

static void nilfs_lookup_dirty_node_buffers(struct inode *inode,
					    struct list_head *listp)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct address_space *mapping = &ii->i_btnode_cache;
	struct pagevec pvec;
	struct buffer_head *bh, *head;
	unsigned int i;
	pgoff_t index = 0;

	pagevec_init(&pvec, 0);

	while (pagevec_lookup_tag(&pvec, mapping, &index, PAGECACHE_TAG_DIRTY,
				  PAGEVEC_SIZE)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			bh = head = page_buffers(pvec.pages[i]);
			do {
				if (buffer_dirty(bh)) {
					get_bh(bh);
					list_add_tail(&bh->b_assoc_buffers,
						      listp);
				}
				bh = bh->b_this_page;
			} while (bh != head);
		}
		pagevec_release(&pvec);
		cond_resched();
	}
}

static void nilfs_dispose_list(struct nilfs_sb_info *sbi,
			       struct list_head *head, int force)
{
	struct nilfs_inode_info *ii, *n;
	struct nilfs_inode_info *ivec[SC_N_INODEVEC], **pii;
	unsigned nv = 0;

	while (!list_empty(head)) {
		spin_lock(&sbi->s_inode_lock);
		list_for_each_entry_safe(ii, n, head, i_dirty) {
			list_del_init(&ii->i_dirty);
			if (force) {
				if (unlikely(ii->i_bh)) {
					brelse(ii->i_bh);
					ii->i_bh = NULL;
				}
			} else if (test_bit(NILFS_I_DIRTY, &ii->i_state)) {
				set_bit(NILFS_I_QUEUED, &ii->i_state);
				list_add_tail(&ii->i_dirty,
					      &sbi->s_dirty_files);
				continue;
			}
			ivec[nv++] = ii;
			if (nv == SC_N_INODEVEC)
				break;
		}
		spin_unlock(&sbi->s_inode_lock);

		for (pii = ivec; nv > 0; pii++, nv--)
			iput(&(*pii)->vfs_inode);
	}
}

static int nilfs_test_metadata_dirty(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	int ret = 0;

	if (nilfs_mdt_fetch_dirty(sbi->s_ifile))
		ret++;
	if (nilfs_mdt_fetch_dirty(nilfs->ns_cpfile))
		ret++;
	if (nilfs_mdt_fetch_dirty(nilfs->ns_sufile))
		ret++;
	if (ret || nilfs_doing_gc())
		if (nilfs_mdt_fetch_dirty(nilfs_dat_inode(nilfs)))
			ret++;
	return ret;
}

static int nilfs_segctor_clean(struct nilfs_sc_info *sci)
{
	return list_empty(&sci->sc_dirty_files) &&
		!test_bit(NILFS_SC_DIRTY, &sci->sc_flags) &&
		list_empty(&sci->sc_cleaning_segments) &&
		(!nilfs_doing_gc() || list_empty(&sci->sc_gc_inodes));
}

static int nilfs_segctor_confirm(struct nilfs_sc_info *sci)
{
	struct nilfs_sb_info *sbi = sci->sc_sbi;
	int ret = 0;

	if (nilfs_test_metadata_dirty(sbi))
		set_bit(NILFS_SC_DIRTY, &sci->sc_flags);

	spin_lock(&sbi->s_inode_lock);
	if (list_empty(&sbi->s_dirty_files) && nilfs_segctor_clean(sci))
		ret++;

	spin_unlock(&sbi->s_inode_lock);
	return ret;
}

static void nilfs_segctor_clear_metadata_dirty(struct nilfs_sc_info *sci)
{
	struct nilfs_sb_info *sbi = sci->sc_sbi;
	struct the_nilfs *nilfs = sbi->s_nilfs;

	nilfs_mdt_clear_dirty(sbi->s_ifile);
	nilfs_mdt_clear_dirty(nilfs->ns_cpfile);
	nilfs_mdt_clear_dirty(nilfs->ns_sufile);
	nilfs_mdt_clear_dirty(nilfs_dat_inode(nilfs));
}

static int nilfs_segctor_create_checkpoint(struct nilfs_sc_info *sci)
{
	struct the_nilfs *nilfs = sci->sc_sbi->s_nilfs;
	struct buffer_head *bh_cp;
	struct nilfs_checkpoint *raw_cp;
	int err;

	/* XXX: this interface will be changed */
	err = nilfs_cpfile_get_checkpoint(nilfs->ns_cpfile, nilfs->ns_cno, 1,
					  &raw_cp, &bh_cp);
	if (likely(!err)) {
		/* The following code is duplicated with cpfile.  But, it is
		   needed to collect the checkpoint even if it was not newly
		   created */
		nilfs_mdt_mark_buffer_dirty(bh_cp);
		nilfs_mdt_mark_dirty(nilfs->ns_cpfile);
		nilfs_cpfile_put_checkpoint(
			nilfs->ns_cpfile, nilfs->ns_cno, bh_cp);
	} else {
		BUG_ON(err == -EINVAL || err == -ENOENT);
	}
	return err;
}

static int nilfs_segctor_fill_in_checkpoint(struct nilfs_sc_info *sci)
{
	struct nilfs_sb_info *sbi = sci->sc_sbi;
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct buffer_head *bh_cp;
	struct nilfs_checkpoint *raw_cp;
	int err;

	err = nilfs_cpfile_get_checkpoint(nilfs->ns_cpfile, nilfs->ns_cno, 0,
					  &raw_cp, &bh_cp);
	if (unlikely(err)) {
		BUG_ON(err == -EINVAL || err == -ENOENT);
		goto failed_ibh;
	}
	raw_cp->cp_snapshot_list.ssl_next = 0;
	raw_cp->cp_snapshot_list.ssl_prev = 0;
	raw_cp->cp_inodes_count =
		cpu_to_le64(atomic_read(&sbi->s_inodes_count));
	raw_cp->cp_blocks_count =
		cpu_to_le64(atomic_read(&sbi->s_blocks_count));
	raw_cp->cp_nblk_inc =
		cpu_to_le64(sci->sc_nblk_inc + sci->sc_nblk_this_inc);
	raw_cp->cp_create = cpu_to_le64(sci->sc_seg_ctime);
	raw_cp->cp_cno = cpu_to_le64(nilfs->ns_cno);
	if (sci->sc_sketch_inode && i_size_read(sci->sc_sketch_inode) > 0)
		nilfs_checkpoint_set_sketch(raw_cp);
	nilfs_write_inode_common(sbi->s_ifile, &raw_cp->cp_ifile_inode, 1);
	nilfs_cpfile_put_checkpoint(nilfs->ns_cpfile, nilfs->ns_cno, bh_cp);
	return 0;

 failed_ibh:
	return err;
}

static void nilfs_fill_in_file_bmap(struct inode *ifile,
				    struct nilfs_inode_info *ii)

{
	struct buffer_head *ibh;
	struct nilfs_inode *raw_inode;

	if (test_bit(NILFS_I_BMAP, &ii->i_state)) {
		ibh = ii->i_bh;
		BUG_ON(!ibh);
		raw_inode = nilfs_ifile_map_inode(ifile, ii->vfs_inode.i_ino,
						  ibh);
		nilfs_bmap_write(ii->i_bmap, raw_inode);
		nilfs_ifile_unmap_inode(ifile, ii->vfs_inode.i_ino, ibh);
	}
}

static void nilfs_segctor_fill_in_file_bmap(struct nilfs_sc_info *sci,
					    struct inode *ifile)
{
	struct nilfs_inode_info *ii;

	list_for_each_entry(ii, &sci->sc_dirty_files, i_dirty) {
		nilfs_fill_in_file_bmap(ifile, ii);
		set_bit(NILFS_I_COLLECTED, &ii->i_state);
	}
	if (sci->sc_sketch_inode) {
		ii = NILFS_I(sci->sc_sketch_inode);
		if (test_bit(NILFS_I_DIRTY, &ii->i_state))
			nilfs_fill_in_file_bmap(ifile, ii);
	}
}

/*
 * CRC calculation routines
 */
static void nilfs_fill_in_super_root_crc(struct buffer_head *bh_sr, u32 seed)
{
	struct nilfs_super_root *raw_sr =
		(struct nilfs_super_root *)bh_sr->b_data;
	u32 crc;

	BUG_ON(NILFS_SR_BYTES > bh_sr->b_size);
	crc = crc32_le(seed,
		       (unsigned char *)raw_sr + sizeof(raw_sr->sr_sum),
		       NILFS_SR_BYTES - sizeof(raw_sr->sr_sum));
	raw_sr->sr_sum = cpu_to_le32(crc);
}

static void nilfs_segctor_fill_in_checksums(struct nilfs_sc_info *sci,
					    u32 seed)
{
	struct nilfs_segment_buffer *segbuf;

	if (sci->sc_super_root)
		nilfs_fill_in_super_root_crc(sci->sc_super_root, seed);

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		nilfs_segbuf_fill_in_segsum_crc(segbuf, seed);
		nilfs_segbuf_fill_in_data_crc(segbuf, seed);
	}
}

static void nilfs_segctor_fill_in_super_root(struct nilfs_sc_info *sci,
					     struct the_nilfs *nilfs)
{
	struct buffer_head *bh_sr = sci->sc_super_root;
	struct nilfs_super_root *raw_sr =
		(struct nilfs_super_root *)bh_sr->b_data;
	unsigned isz = nilfs->ns_inode_size;

	raw_sr->sr_bytes = cpu_to_le16(NILFS_SR_BYTES);
	raw_sr->sr_nongc_ctime
		= cpu_to_le64(nilfs_doing_gc() ?
			      nilfs->ns_nongc_ctime : sci->sc_seg_ctime);
	raw_sr->sr_flags = 0;

	nilfs_mdt_write_inode_direct(
		nilfs_dat_inode(nilfs), bh_sr, NILFS_SR_DAT_OFFSET(isz));
	nilfs_mdt_write_inode_direct(
		nilfs->ns_cpfile, bh_sr, NILFS_SR_CPFILE_OFFSET(isz));
	nilfs_mdt_write_inode_direct(
		nilfs->ns_sufile, bh_sr, NILFS_SR_SUFILE_OFFSET(isz));
}

static void nilfs_redirty_inodes(struct list_head *head)
{
	struct nilfs_inode_info *ii;

	list_for_each_entry(ii, head, i_dirty) {
		if (test_bit(NILFS_I_COLLECTED, &ii->i_state))
			clear_bit(NILFS_I_COLLECTED, &ii->i_state);
	}
}

static void nilfs_drop_collected_inodes(struct list_head *head)
{
	struct nilfs_inode_info *ii;

	list_for_each_entry(ii, head, i_dirty) {
		if (!test_and_clear_bit(NILFS_I_COLLECTED, &ii->i_state))
			continue;

		clear_bit(NILFS_I_INODE_DIRTY, &ii->i_state);
		set_bit(NILFS_I_UPDATED, &ii->i_state);
	}
}

static void nilfs_segctor_cancel_free_segments(struct nilfs_sc_info *sci,
					       struct inode *sufile)

{
	struct list_head *head = &sci->sc_cleaning_segments;
	struct nilfs_segment_entry *ent;
	int err;

	list_for_each_entry(ent, head, list) {
		if (!(ent->flags & NILFS_SLH_FREED))
			break;
		err = nilfs_sufile_cancel_free(sufile, ent->segnum);
		BUG_ON(err);

		ent->flags &= ~NILFS_SLH_FREED;
	}
}

static int nilfs_segctor_prepare_free_segments(struct nilfs_sc_info *sci,
					       struct inode *sufile)
{
	struct list_head *head = &sci->sc_cleaning_segments;
	struct nilfs_segment_entry *ent;
	int err;

	list_for_each_entry(ent, head, list) {
		err = nilfs_sufile_free(sufile, ent->segnum);
		if (unlikely(err))
			return err;
		ent->flags |= NILFS_SLH_FREED;
	}
	return 0;
}

static void nilfs_segctor_commit_free_segments(struct nilfs_sc_info *sci)
{
	nilfs_dispose_segment_list(&sci->sc_cleaning_segments);
}

static int nilfs_segctor_apply_buffers(struct nilfs_sc_info *sci,
				       struct inode *inode,
				       struct list_head *listp,
				       int (*collect)(struct nilfs_sc_info *,
						      struct buffer_head *,
						      struct inode *))
{
	struct buffer_head *bh, *n;
	int err = 0;

	if (collect) {
		list_for_each_entry_safe(bh, n, listp, b_assoc_buffers) {
			list_del_init(&bh->b_assoc_buffers);
			err = collect(sci, bh, inode);
			brelse(bh);
			if (unlikely(err))
				goto dispose_buffers;
		}
		return 0;
	}

 dispose_buffers:
	while (!list_empty(listp)) {
		bh = list_entry(listp->next, struct buffer_head,
				b_assoc_buffers);
		list_del_init(&bh->b_assoc_buffers);
		brelse(bh);
	}
	return err;
}

static int nilfs_segctor_scan_file(struct nilfs_sc_info *sci,
				   struct inode *inode,
				   struct nilfs_sc_operations *sc_ops)
{
	LIST_HEAD(data_buffers);
	LIST_HEAD(node_buffers);
	int err, err2;

	if (!(sci->sc_stage.flags & NILFS_CF_NODE)) {
		err = nilfs_lookup_dirty_data_buffers(inode, &data_buffers,
						      sci);
		if (err) {
			err2 = nilfs_segctor_apply_buffers(
				sci, inode, &data_buffers,
				err == -E2BIG ? sc_ops->collect_data : NULL);
			if (err == -E2BIG)
				err = err2;
			goto break_or_fail;
		}
	}
	nilfs_lookup_dirty_node_buffers(inode, &node_buffers);

	if (!(sci->sc_stage.flags & NILFS_CF_NODE)) {
		err = nilfs_segctor_apply_buffers(
			sci, inode, &data_buffers, sc_ops->collect_data);
		if (unlikely(err)) {
			/* dispose node list */
			nilfs_segctor_apply_buffers(
				sci, inode, &node_buffers, NULL);
			goto break_or_fail;
		}
		sci->sc_stage.flags |= NILFS_CF_NODE;
	}
	/* Collect node */
	err = nilfs_segctor_apply_buffers(
		sci, inode, &node_buffers, sc_ops->collect_node);
	if (unlikely(err))
		goto break_or_fail;

	nilfs_bmap_lookup_dirty_buffers(NILFS_I(inode)->i_bmap, &node_buffers);
	err = nilfs_segctor_apply_buffers(
		sci, inode, &node_buffers, sc_ops->collect_bmap);
	if (unlikely(err))
		goto break_or_fail;

	nilfs_segctor_end_finfo(sci, inode);
	sci->sc_stage.flags &= ~NILFS_CF_NODE;

 break_or_fail:
	return err;
}

static int nilfs_segctor_scan_file_dsync(struct nilfs_sc_info *sci,
					 struct inode *inode)
{
	LIST_HEAD(data_buffers);
	int err, err2;

	err = nilfs_lookup_dirty_data_buffers(inode, &data_buffers, sci);
	err2 = nilfs_segctor_apply_buffers(sci, inode, &data_buffers,
					   (!err || err == -E2BIG) ?
					   nilfs_collect_file_data : NULL);
	if (err == -E2BIG)
		err = err2;
	if (!err)
		nilfs_segctor_end_finfo(sci, inode);
	return err;
}

static int nilfs_segctor_collect_blocks(struct nilfs_sc_info *sci, int mode)
{
	struct nilfs_sb_info *sbi = sci->sc_sbi;
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct list_head *head;
	struct nilfs_inode_info *ii;
	int err = 0;

	switch (sci->sc_stage.scnt) {
	case NILFS_ST_INIT:
		/* Pre-processes */
		sci->sc_stage.flags = 0;

		if (!test_bit(NILFS_SC_UNCLOSED, &sci->sc_flags)) {
			sci->sc_nblk_inc = 0;
			sci->sc_curseg->sb_sum.flags = NILFS_SS_LOGBGN;
			if (mode == SC_LSEG_DSYNC) {
				sci->sc_stage.scnt = NILFS_ST_DSYNC;
				goto dsync_mode;
			}
		}

		sci->sc_stage.dirty_file_ptr = NULL;
		sci->sc_stage.gc_inode_ptr = NULL;
		if (mode == SC_FLUSH_DAT) {
			sci->sc_stage.scnt = NILFS_ST_DAT;
			goto dat_stage;
		}
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_GC:
		if (nilfs_doing_gc()) {
			head = &sci->sc_gc_inodes;
			ii = list_prepare_entry(sci->sc_stage.gc_inode_ptr,
						head, i_dirty);
			list_for_each_entry_continue(ii, head, i_dirty) {
				err = nilfs_segctor_scan_file(
					sci, &ii->vfs_inode,
					&nilfs_sc_file_ops);
				if (unlikely(err)) {
					sci->sc_stage.gc_inode_ptr = list_entry(
						ii->i_dirty.prev,
						struct nilfs_inode_info,
						i_dirty);
					goto break_or_fail;
				}
				set_bit(NILFS_I_COLLECTED, &ii->i_state);
			}
			sci->sc_stage.gc_inode_ptr = NULL;
		}
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_FILE:
		head = &sci->sc_dirty_files;
		ii = list_prepare_entry(sci->sc_stage.dirty_file_ptr, head,
					i_dirty);
		list_for_each_entry_continue(ii, head, i_dirty) {
			clear_bit(NILFS_I_DIRTY, &ii->i_state);

			err = nilfs_segctor_scan_file(sci, &ii->vfs_inode,
						      &nilfs_sc_file_ops);
			if (unlikely(err)) {
				sci->sc_stage.dirty_file_ptr =
					list_entry(ii->i_dirty.prev,
						   struct nilfs_inode_info,
						   i_dirty);
				goto break_or_fail;
			}
			/* sci->sc_stage.dirty_file_ptr = NILFS_I(inode); */
			/* XXX: required ? */
		}
		sci->sc_stage.dirty_file_ptr = NULL;
		if (mode == SC_FLUSH_FILE) {
			sci->sc_stage.scnt = NILFS_ST_DONE;
			return 0;
		}
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_SKETCH:
		if (mode == SC_LSEG_SR && sci->sc_sketch_inode) {
			ii = NILFS_I(sci->sc_sketch_inode);
			if (test_bit(NILFS_I_DIRTY, &ii->i_state)) {
				sci->sc_sketch_inode->i_ctime.tv_sec
					= sci->sc_seg_ctime;
				sci->sc_sketch_inode->i_mtime.tv_sec
					= sci->sc_seg_ctime;
				err = nilfs_mark_inode_dirty(
					sci->sc_sketch_inode);
				if (unlikely(err))
					goto break_or_fail;
			}
			err = nilfs_segctor_scan_file(sci,
						      sci->sc_sketch_inode,
						      &nilfs_sc_file_ops);
			if (unlikely(err))
				goto break_or_fail;
		}
		sci->sc_stage.scnt++;
		sci->sc_stage.flags |= NILFS_CF_IFILE_STARTED;
		/* Fall through */
	case NILFS_ST_IFILE:
		err = nilfs_segctor_scan_file(sci, sbi->s_ifile,
					      &nilfs_sc_file_ops);
		if (unlikely(err))
			break;
		sci->sc_stage.scnt++;
		/* Creating a checkpoint */
		err = nilfs_segctor_create_checkpoint(sci);
		if (unlikely(err))
			break;
		/* Fall through */
	case NILFS_ST_CPFILE:
		err = nilfs_segctor_scan_file(sci, nilfs->ns_cpfile,
					      &nilfs_sc_file_ops);
		if (unlikely(err))
			break;
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_SUFILE:
		err = nilfs_segctor_prepare_free_segments(sci,
							  nilfs->ns_sufile);
		if (unlikely(err))
			break;
		err = nilfs_segctor_scan_file(sci, nilfs->ns_sufile,
					      &nilfs_sc_file_ops);
		if (unlikely(err))
			break;
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_DAT:
 dat_stage:
		err = nilfs_segctor_scan_file(sci, nilfs_dat_inode(nilfs),
					      &nilfs_sc_dat_ops);
		if (unlikely(err))
			break;
		if (mode == SC_FLUSH_DAT) {
			sci->sc_stage.scnt = NILFS_ST_DONE;
			return 0;
		}
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_SR:
		if (mode == SC_LSEG_SR) {
			/* Appending a super root */
			err = nilfs_segctor_add_super_root(sci);
			if (unlikely(err))
				break;
		}
		/* End of a logical segment */
		sci->sc_curseg->sb_sum.flags |= NILFS_SS_LOGEND;
		sci->sc_stage.scnt = NILFS_ST_DONE;
		return 0;
	case NILFS_ST_DSYNC:
 dsync_mode:
		sci->sc_curseg->sb_sum.flags |= NILFS_SS_SYNDT;
		ii = sci->sc_stage.dirty_file_ptr;
		if (!test_bit(NILFS_I_BUSY, &ii->i_state))
			break;

		err = nilfs_segctor_scan_file_dsync(sci, &ii->vfs_inode);
		if (unlikely(err))
			break;
		sci->sc_stage.dirty_file_ptr = NULL;
		sci->sc_curseg->sb_sum.flags |= NILFS_SS_LOGEND;
		sci->sc_stage.scnt = NILFS_ST_DONE;
		return 0;
	case NILFS_ST_DONE:
		return 0;
	default:
		BUG();
	}

 break_or_fail:
	return err;
}

static int nilfs_segctor_terminate_segment(struct nilfs_sc_info *sci,
					   struct nilfs_segment_buffer *segbuf,
					   struct inode *sufile)
{
	struct nilfs_segment_entry *ent = segbuf->sb_segent;
	int err;

	err = nilfs_open_segment_entry(ent, sufile);
	if (unlikely(err))
		return err;
	nilfs_mdt_mark_buffer_dirty(ent->bh_su);
	nilfs_mdt_mark_dirty(sufile);
	nilfs_close_segment_entry(ent, sufile);

	list_add_tail(&ent->list, &sci->sc_active_segments);
	segbuf->sb_segent = NULL;
	return 0;
}

static int nilfs_touch_segusage(struct inode *sufile, __u64 segnum)
{
	struct buffer_head *bh_su;
	struct nilfs_segment_usage *raw_su;
	int err;

	err = nilfs_sufile_get_segment_usage(sufile, segnum, &raw_su, &bh_su);
	if (unlikely(err))
		return err;
	nilfs_mdt_mark_buffer_dirty(bh_su);
	nilfs_mdt_mark_dirty(sufile);
	nilfs_sufile_put_segment_usage(sufile, segnum, bh_su);
	return 0;
}

static int nilfs_segctor_begin_construction(struct nilfs_sc_info *sci,
					    struct the_nilfs *nilfs)
{
	struct nilfs_segment_buffer *segbuf, *n;
	struct inode *sufile = nilfs->ns_sufile;
	__u64 nextnum;
	int err;

	if (list_empty(&sci->sc_segbufs)) {
		segbuf = nilfs_segbuf_new(sci->sc_super);
		if (unlikely(!segbuf))
			return -ENOMEM;
		list_add(&segbuf->sb_list, &sci->sc_segbufs);
	} else
		segbuf = NILFS_FIRST_SEGBUF(&sci->sc_segbufs);

	err = nilfs_segbuf_map(segbuf, nilfs->ns_segnum,
			       nilfs->ns_pseg_offset, nilfs);
	if (unlikely(err))
		return err;

	if (segbuf->sb_rest_blocks < NILFS_PSEG_MIN_BLOCKS) {
		err = nilfs_segctor_terminate_segment(sci, segbuf, sufile);
		if (unlikely(err))
			return err;

		nilfs_shift_to_next_segment(nilfs);
		err = nilfs_segbuf_map(segbuf, nilfs->ns_segnum, 0, nilfs);
	}
	sci->sc_segbuf_nblocks = segbuf->sb_rest_blocks;

	err = nilfs_touch_segusage(sufile, segbuf->sb_segnum);
	if (unlikely(err))
		return err;

	if (nilfs->ns_segnum == nilfs->ns_nextnum) {
		/* Start from the head of a new full segment */
		err = nilfs_sufile_alloc(sufile, &nextnum);
		if (unlikely(err))
			return err;
	} else
		nextnum = nilfs->ns_nextnum;

	segbuf->sb_sum.seg_seq = nilfs->ns_seg_seq;
	nilfs_segbuf_set_next_segnum(segbuf, nextnum, nilfs);

	/* truncating segment buffers */
	list_for_each_entry_safe_continue(segbuf, n, &sci->sc_segbufs,
					  sb_list) {
		list_del_init(&segbuf->sb_list);
		nilfs_segbuf_free(segbuf);
	}
	return err;
}

static int nilfs_segctor_extend_segments(struct nilfs_sc_info *sci,
					 struct the_nilfs *nilfs, int nadd)
{
	struct nilfs_segment_buffer *segbuf, *prev, *n;
	struct inode *sufile = nilfs->ns_sufile;
	__u64 nextnextnum;
	LIST_HEAD(list);
	int err, ret, i;

	prev = NILFS_LAST_SEGBUF(&sci->sc_segbufs);
	/*
	 * Since the segment specified with nextnum might be allocated during
	 * the previous construction, the buffer including its segusage may
	 * not be dirty.  The following call ensures that the buffer is dirty
	 * and will pin the buffer on memory until the sufile is written.
	 */
	err = nilfs_touch_segusage(sufile, prev->sb_nextnum);
	if (unlikely(err))
		return err;

	for (i = 0; i < nadd; i++) {
		/* extend segment info */
		err = -ENOMEM;
		segbuf = nilfs_segbuf_new(sci->sc_super);
		if (unlikely(!segbuf))
			goto failed;

		/* map this buffer to region of segment on-disk */
		err = nilfs_segbuf_map(segbuf, prev->sb_nextnum, 0, nilfs);
		if (unlikely(err))
			goto failed_segbuf;

		sci->sc_segbuf_nblocks += segbuf->sb_rest_blocks;

		/* allocate the next next full segment */
		err = nilfs_sufile_alloc(sufile, &nextnextnum);
		if (unlikely(err))
			goto failed_segbuf;

		segbuf->sb_sum.seg_seq = prev->sb_sum.seg_seq + 1;
		nilfs_segbuf_set_next_segnum(segbuf, nextnextnum, nilfs);

		list_add_tail(&segbuf->sb_list, &list);
		prev = segbuf;
	}
	list_splice(&list, sci->sc_segbufs.prev);
	return 0;

 failed_segbuf:
	nilfs_segbuf_free(segbuf);
 failed:
	list_for_each_entry_safe(segbuf, n, &list, sb_list) {
		ret = nilfs_sufile_free(sufile, segbuf->sb_nextnum);
		BUG_ON(ret);
		list_del_init(&segbuf->sb_list);
		nilfs_segbuf_free(segbuf);
	}
	return err;
}

static void nilfs_segctor_free_incomplete_segments(struct nilfs_sc_info *sci,
						   struct the_nilfs *nilfs)
{
	struct nilfs_segment_buffer *segbuf;
	int ret, done = 0;

	segbuf = NILFS_FIRST_SEGBUF(&sci->sc_segbufs);
	if (nilfs->ns_nextnum != segbuf->sb_nextnum) {
		ret = nilfs_sufile_free(nilfs->ns_sufile, segbuf->sb_nextnum);
		BUG_ON(ret);
	}
	if (segbuf->sb_io_error) {
		/* Case 1: The first segment failed */
		if (segbuf->sb_pseg_start != segbuf->sb_fseg_start)
			/* Case 1a:  Partial segment appended into an existing
			   segment */
			nilfs_terminate_segment(nilfs, segbuf->sb_fseg_start,
						segbuf->sb_fseg_end);
		else /* Case 1b:  New full segment */
			set_nilfs_discontinued(nilfs);
		done++;
	}

	list_for_each_entry_continue(segbuf, &sci->sc_segbufs, sb_list) {
		ret = nilfs_sufile_free(nilfs->ns_sufile, segbuf->sb_nextnum);
		BUG_ON(ret);
		if (!done && segbuf->sb_io_error) {
			if (segbuf->sb_segnum != nilfs->ns_nextnum)
				/* Case 2: extended segment (!= next) failed */
				nilfs_sufile_set_error(nilfs->ns_sufile,
						       segbuf->sb_segnum);
			done++;
		}
	}
}

static void nilfs_segctor_clear_segment_buffers(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf;

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list)
		nilfs_segbuf_clear(segbuf);
	sci->sc_super_root = NULL;
}

static void nilfs_segctor_destroy_segment_buffers(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf;

	while (!list_empty(&sci->sc_segbufs)) {
		segbuf = NILFS_FIRST_SEGBUF(&sci->sc_segbufs);
		list_del_init(&segbuf->sb_list);
		nilfs_segbuf_free(segbuf);
	}
	/* sci->sc_curseg = NULL; */
}

static void nilfs_segctor_end_construction(struct nilfs_sc_info *sci,
					   struct the_nilfs *nilfs, int err)
{
	if (unlikely(err)) {
		nilfs_segctor_free_incomplete_segments(sci, nilfs);
		nilfs_segctor_cancel_free_segments(sci, nilfs->ns_sufile);
	}
	nilfs_segctor_clear_segment_buffers(sci);
}

static void nilfs_segctor_update_segusage(struct nilfs_sc_info *sci,
					  struct inode *sufile)
{
	struct nilfs_segment_buffer *segbuf;
	struct buffer_head *bh_su;
	struct nilfs_segment_usage *raw_su;
	unsigned long live_blocks;
	int ret;

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		ret = nilfs_sufile_get_segment_usage(sufile, segbuf->sb_segnum,
						     &raw_su, &bh_su);
		BUG_ON(ret); /* always succeed because bh_su is dirty */
		live_blocks = segbuf->sb_sum.nblocks +
			(segbuf->sb_pseg_start - segbuf->sb_fseg_start);
		raw_su->su_lastmod = cpu_to_le64(sci->sc_seg_ctime);
		raw_su->su_nblocks = cpu_to_le32(live_blocks);
		nilfs_sufile_put_segment_usage(sufile, segbuf->sb_segnum,
					       bh_su);
	}
}

static void nilfs_segctor_cancel_segusage(struct nilfs_sc_info *sci,
					  struct inode *sufile)
{
	struct nilfs_segment_buffer *segbuf;
	struct buffer_head *bh_su;
	struct nilfs_segment_usage *raw_su;
	int ret;

	segbuf = NILFS_FIRST_SEGBUF(&sci->sc_segbufs);
	ret = nilfs_sufile_get_segment_usage(sufile, segbuf->sb_segnum,
					     &raw_su, &bh_su);
	BUG_ON(ret); /* always succeed because bh_su is dirty */
	raw_su->su_nblocks = cpu_to_le32(segbuf->sb_pseg_start -
					 segbuf->sb_fseg_start);
	nilfs_sufile_put_segment_usage(sufile, segbuf->sb_segnum, bh_su);

	list_for_each_entry_continue(segbuf, &sci->sc_segbufs, sb_list) {
		ret = nilfs_sufile_get_segment_usage(sufile, segbuf->sb_segnum,
						     &raw_su, &bh_su);
		BUG_ON(ret); /* always succeed */
		raw_su->su_nblocks = 0;
		nilfs_sufile_put_segment_usage(sufile, segbuf->sb_segnum,
					       bh_su);
	}
}

static void nilfs_segctor_truncate_segments(struct nilfs_sc_info *sci,
					    struct nilfs_segment_buffer *last,
					    struct inode *sufile)
{
	struct nilfs_segment_buffer *segbuf = last, *n;
	int ret;

	list_for_each_entry_safe_continue(segbuf, n, &sci->sc_segbufs,
					  sb_list) {
		list_del_init(&segbuf->sb_list);
		sci->sc_segbuf_nblocks -= segbuf->sb_rest_blocks;
		ret = nilfs_sufile_free(sufile, segbuf->sb_nextnum);
		BUG_ON(ret);
		nilfs_segbuf_free(segbuf);
	}
}


static int nilfs_segctor_collect(struct nilfs_sc_info *sci,
				 struct the_nilfs *nilfs, int mode)
{
	struct nilfs_cstage prev_stage = sci->sc_stage;
	int err, nadd = 1;

	/* Collection retry loop */
	for (;;) {
		sci->sc_super_root = NULL;
		sci->sc_nblk_this_inc = 0;
		sci->sc_curseg = NILFS_FIRST_SEGBUF(&sci->sc_segbufs);

		err = nilfs_segctor_reset_segment_buffer(sci);
		if (unlikely(err))
			goto failed;

		err = nilfs_segctor_collect_blocks(sci, mode);
		sci->sc_nblk_this_inc += sci->sc_curseg->sb_sum.nblocks;
		if (!err)
			break;

		if (unlikely(err != -E2BIG))
			goto failed;

		/* The current segment is filled up */
		if (mode != SC_LSEG_SR || sci->sc_stage.scnt < NILFS_ST_CPFILE)
			break;

		nilfs_segctor_cancel_free_segments(sci, nilfs->ns_sufile);
		nilfs_segctor_clear_segment_buffers(sci);

		err = nilfs_segctor_extend_segments(sci, nilfs, nadd);
		if (unlikely(err))
			return err;

		nadd = min_t(int, nadd << 1, SC_MAX_SEGDELTA);
		sci->sc_stage = prev_stage;
	}
	nilfs_segctor_truncate_segments(sci, sci->sc_curseg, nilfs->ns_sufile);
	return 0;

 failed:
	return err;
}

static void nilfs_list_replace_buffer(struct buffer_head *old_bh,
				      struct buffer_head *new_bh)
{
	BUG_ON(!list_empty(&new_bh->b_assoc_buffers));

	list_replace_init(&old_bh->b_assoc_buffers, &new_bh->b_assoc_buffers);
	/* The caller must release old_bh */
}

static int
nilfs_segctor_update_payload_blocknr(struct nilfs_sc_info *sci,
				     struct nilfs_segment_buffer *segbuf,
				     int mode)
{
	struct inode *inode = NULL;
	sector_t blocknr;
	unsigned long nfinfo = segbuf->sb_sum.nfinfo;
	unsigned long nblocks = 0, ndatablk = 0;
	struct nilfs_sc_operations *sc_op = NULL;
	struct nilfs_segsum_pointer ssp;
	struct nilfs_finfo *finfo = NULL;
	union nilfs_binfo binfo;
	struct buffer_head *bh, *bh_org;
	ino_t ino = 0;
	int err = 0;

	if (!nfinfo)
		goto out;

	blocknr = segbuf->sb_pseg_start + segbuf->sb_sum.nsumblk;
	ssp.bh = NILFS_SEGBUF_FIRST_BH(&segbuf->sb_segsum_buffers);
	ssp.offset = sizeof(struct nilfs_segment_summary);

	list_for_each_entry(bh, &segbuf->sb_payload_buffers, b_assoc_buffers) {
		if (bh == sci->sc_super_root)
			break;
		if (!finfo) {
			finfo =	nilfs_segctor_map_segsum_entry(
				sci, &ssp, sizeof(*finfo));
			ino = le64_to_cpu(finfo->fi_ino);
			nblocks = le32_to_cpu(finfo->fi_nblocks);
			ndatablk = le32_to_cpu(finfo->fi_ndatablk);

			if (buffer_nilfs_node(bh))
				inode = NILFS_BTNC_I(bh->b_page->mapping);
			else
				inode = NILFS_AS_I(bh->b_page->mapping);

			if (mode == SC_LSEG_DSYNC)
				sc_op = &nilfs_sc_dsync_ops;
			else if (ino == NILFS_DAT_INO)
				sc_op = &nilfs_sc_dat_ops;
			else /* file blocks */
				sc_op = &nilfs_sc_file_ops;
		}
		bh_org = bh;
		get_bh(bh_org);
		err = nilfs_bmap_assign(NILFS_I(inode)->i_bmap, &bh, blocknr,
					&binfo);
		if (bh != bh_org)
			nilfs_list_replace_buffer(bh_org, bh);
		brelse(bh_org);
		if (unlikely(err))
			goto failed_bmap;

		if (ndatablk > 0)
			sc_op->write_data_binfo(sci, &ssp, &binfo);
		else
			sc_op->write_node_binfo(sci, &ssp, &binfo);

		blocknr++;
		if (--nblocks == 0) {
			finfo = NULL;
			if (--nfinfo == 0)
				break;
		} else if (ndatablk > 0)
			ndatablk--;
	}
 out:
	return 0;

 failed_bmap:
	err = nilfs_handle_bmap_error(err, __func__, inode, sci->sc_super);
	return err;
}

static int nilfs_segctor_assign(struct nilfs_sc_info *sci, int mode)
{
	struct nilfs_segment_buffer *segbuf;
	int err;

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		err = nilfs_segctor_update_payload_blocknr(sci, segbuf, mode);
		if (unlikely(err))
			return err;
		nilfs_segbuf_fill_in_segsum(segbuf);
	}
	return 0;
}

static int
nilfs_copy_replace_page_buffers(struct page *page, struct list_head *out)
{
	struct page *clone_page;
	struct buffer_head *bh, *head, *bh2;
	void *kaddr;

	bh = head = page_buffers(page);

	clone_page = nilfs_alloc_private_page(bh->b_bdev, bh->b_size, 0);
	if (unlikely(!clone_page))
		return -ENOMEM;

	bh2 = page_buffers(clone_page);
	kaddr = kmap_atomic(page, KM_USER0);
	do {
		if (list_empty(&bh->b_assoc_buffers))
			continue;
		get_bh(bh2);
		page_cache_get(clone_page); /* for each bh */
		memcpy(bh2->b_data, kaddr + bh_offset(bh), bh2->b_size);
		bh2->b_blocknr = bh->b_blocknr;
		list_replace(&bh->b_assoc_buffers, &bh2->b_assoc_buffers);
		list_add_tail(&bh->b_assoc_buffers, out);
	} while (bh = bh->b_this_page, bh2 = bh2->b_this_page, bh != head);
	kunmap_atomic(kaddr, KM_USER0);

	if (!TestSetPageWriteback(clone_page))
		inc_zone_page_state(clone_page, NR_WRITEBACK);
	unlock_page(clone_page);

	return 0;
}

static int nilfs_test_page_to_be_frozen(struct page *page)
{
	struct address_space *mapping = page->mapping;

	if (!mapping || !mapping->host || S_ISDIR(mapping->host->i_mode))
		return 0;

	if (page_mapped(page)) {
		ClearPageChecked(page);
		return 1;
	}
	return PageChecked(page);
}

static int nilfs_begin_page_io(struct page *page, struct list_head *out)
{
	if (!page || PageWriteback(page))
		/* For split b-tree node pages, this function may be called
		   twice.  We ignore the 2nd or later calls by this check. */
		return 0;

	lock_page(page);
	clear_page_dirty_for_io(page);
	set_page_writeback(page);
	unlock_page(page);

	if (nilfs_test_page_to_be_frozen(page)) {
		int err = nilfs_copy_replace_page_buffers(page, out);
		if (unlikely(err))
			return err;
	}
	return 0;
}

static int nilfs_segctor_prepare_write(struct nilfs_sc_info *sci,
				       struct page **failed_page)
{
	struct nilfs_segment_buffer *segbuf;
	struct page *bd_page = NULL, *fs_page = NULL;
	struct list_head *list = &sci->sc_copied_buffers;
	int err;

	*failed_page = NULL;
	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		struct buffer_head *bh;

		list_for_each_entry(bh, &segbuf->sb_segsum_buffers,
				    b_assoc_buffers) {
			if (bh->b_page != bd_page) {
				if (bd_page) {
					lock_page(bd_page);
					clear_page_dirty_for_io(bd_page);
					set_page_writeback(bd_page);
					unlock_page(bd_page);
				}
				bd_page = bh->b_page;
			}
		}

		list_for_each_entry(bh, &segbuf->sb_payload_buffers,
				    b_assoc_buffers) {
			if (bh == sci->sc_super_root) {
				if (bh->b_page != bd_page) {
					lock_page(bd_page);
					clear_page_dirty_for_io(bd_page);
					set_page_writeback(bd_page);
					unlock_page(bd_page);
					bd_page = bh->b_page;
				}
				break;
			}
			if (bh->b_page != fs_page) {
				err = nilfs_begin_page_io(fs_page, list);
				if (unlikely(err)) {
					*failed_page = fs_page;
					goto out;
				}
				fs_page = bh->b_page;
			}
		}
	}
	if (bd_page) {
		lock_page(bd_page);
		clear_page_dirty_for_io(bd_page);
		set_page_writeback(bd_page);
		unlock_page(bd_page);
	}
	err = nilfs_begin_page_io(fs_page, list);
	if (unlikely(err))
		*failed_page = fs_page;
 out:
	return err;
}

static int nilfs_segctor_write(struct nilfs_sc_info *sci,
			       struct backing_dev_info *bdi)
{
	struct nilfs_segment_buffer *segbuf;
	struct nilfs_write_info wi;
	int err, res;

	wi.sb = sci->sc_super;
	wi.bh_sr = sci->sc_super_root;
	wi.bdi = bdi;

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		nilfs_segbuf_prepare_write(segbuf, &wi);
		err = nilfs_segbuf_write(segbuf, &wi);

		res = nilfs_segbuf_wait(segbuf, &wi);
		err = unlikely(err) ? : res;
		if (unlikely(err))
			return err;
	}
	return 0;
}

static int nilfs_page_has_uncleared_buffer(struct page *page)
{
	struct buffer_head *head, *bh;

	head = bh = page_buffers(page);
	do {
		if (buffer_dirty(bh) && !list_empty(&bh->b_assoc_buffers))
			return 1;
		bh = bh->b_this_page;
	} while (bh != head);
	return 0;
}

static void __nilfs_end_page_io(struct page *page, int err)
{
	/* BUG_ON(err > 0); */
	if (!err) {
		if (!nilfs_page_buffers_clean(page))
			__set_page_dirty_nobuffers(page);
		ClearPageError(page);
	} else {
		__set_page_dirty_nobuffers(page);
		SetPageError(page);
	}

	if (buffer_nilfs_allocated(page_buffers(page))) {
		if (TestClearPageWriteback(page))
			dec_zone_page_state(page, NR_WRITEBACK);
	} else
		end_page_writeback(page);
}

static void nilfs_end_page_io(struct page *page, int err)
{
	if (!page)
		return;

	if (buffer_nilfs_node(page_buffers(page)) &&
	    nilfs_page_has_uncleared_buffer(page))
		/* For b-tree node pages, this function may be called twice
		   or more because they might be split in a segment.
		   This check assures that cleanup has been done for all
		   buffers in a split btnode page. */
		return;

	__nilfs_end_page_io(page, err);
}

static void nilfs_clear_copied_buffers(struct list_head *list, int err)
{
	struct buffer_head *bh, *head;
	struct page *page;

	while (!list_empty(list)) {
		bh = list_entry(list->next, struct buffer_head,
				b_assoc_buffers);
		page = bh->b_page;
		page_cache_get(page);
		head = bh = page_buffers(page);
		do {
			if (!list_empty(&bh->b_assoc_buffers)) {
				list_del_init(&bh->b_assoc_buffers);
				if (!err) {
					set_buffer_uptodate(bh);
					clear_buffer_dirty(bh);
					clear_buffer_nilfs_volatile(bh);
				}
				brelse(bh); /* for b_assoc_buffers */
			}
		} while ((bh = bh->b_this_page) != head);

		__nilfs_end_page_io(page, err);
		page_cache_release(page);
	}
}

static void nilfs_segctor_abort_write(struct nilfs_sc_info *sci,
				      struct page *failed_page, int err)
{
	struct nilfs_segment_buffer *segbuf;
	struct page *bd_page = NULL, *fs_page = NULL;

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		struct buffer_head *bh;

		list_for_each_entry(bh, &segbuf->sb_segsum_buffers,
				    b_assoc_buffers) {
			if (bh->b_page != bd_page) {
				if (bd_page)
					end_page_writeback(bd_page);
				bd_page = bh->b_page;
			}
		}

		list_for_each_entry(bh, &segbuf->sb_payload_buffers,
				    b_assoc_buffers) {
			if (bh == sci->sc_super_root) {
				if (bh->b_page != bd_page) {
					end_page_writeback(bd_page);
					bd_page = bh->b_page;
				}
				break;
			}
			if (bh->b_page != fs_page) {
				nilfs_end_page_io(fs_page, err);
				if (unlikely(fs_page == failed_page))
					goto done;
				fs_page = bh->b_page;
			}
		}
	}
	if (bd_page)
		end_page_writeback(bd_page);

	nilfs_end_page_io(fs_page, err);
 done:
	nilfs_clear_copied_buffers(&sci->sc_copied_buffers, err);
}

static void nilfs_set_next_segment(struct the_nilfs *nilfs,
				   struct nilfs_segment_buffer *segbuf)
{
	nilfs->ns_segnum = segbuf->sb_segnum;
	nilfs->ns_nextnum = segbuf->sb_nextnum;
	nilfs->ns_pseg_offset = segbuf->sb_pseg_start - segbuf->sb_fseg_start
		+ segbuf->sb_sum.nblocks;
	nilfs->ns_seg_seq = segbuf->sb_sum.seg_seq;
	nilfs->ns_ctime = segbuf->sb_sum.ctime;
}

static void nilfs_segctor_complete_write(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf;
	struct page *bd_page = NULL, *fs_page = NULL;
	struct nilfs_sb_info *sbi = sci->sc_sbi;
	struct the_nilfs *nilfs = sbi->s_nilfs;
	int update_sr = (sci->sc_super_root != NULL);

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		struct buffer_head *bh;

		list_for_each_entry(bh, &segbuf->sb_segsum_buffers,
				    b_assoc_buffers) {
			set_buffer_uptodate(bh);
			clear_buffer_dirty(bh);
			if (bh->b_page != bd_page) {
				if (bd_page)
					end_page_writeback(bd_page);
				bd_page = bh->b_page;
			}
		}
		/*
		 * We assume that the buffers which belong to the same page
		 * continue over the buffer list.
		 * Under this assumption, the last BHs of pages is
		 * identifiable by the discontinuity of bh->b_page
		 * (page != fs_page).
		 *
		 * For B-tree node blocks, however, this assumption is not
		 * guaranteed.  The cleanup code of B-tree node pages needs
		 * special care.
		 */
		list_for_each_entry(bh, &segbuf->sb_payload_buffers,
				    b_assoc_buffers) {
			set_buffer_uptodate(bh);
			clear_buffer_dirty(bh);
			clear_buffer_nilfs_volatile(bh);
			if (bh == sci->sc_super_root) {
				if (bh->b_page != bd_page) {
					end_page_writeback(bd_page);
					bd_page = bh->b_page;
				}
				break;
			}
			if (bh->b_page != fs_page) {
				nilfs_end_page_io(fs_page, 0);
				fs_page = bh->b_page;
			}
		}

		if (!NILFS_SEG_SIMPLEX(&segbuf->sb_sum)) {
			if (NILFS_SEG_LOGBGN(&segbuf->sb_sum)) {
				set_bit(NILFS_SC_UNCLOSED, &sci->sc_flags);
				sci->sc_lseg_stime = jiffies;
			}
			if (NILFS_SEG_LOGEND(&segbuf->sb_sum))
				clear_bit(NILFS_SC_UNCLOSED, &sci->sc_flags);
		}
	}
	/*
	 * Since pages may continue over multiple segment buffers,
	 * end of the last page must be checked outside of the loop.
	 */
	if (bd_page)
		end_page_writeback(bd_page);

	nilfs_end_page_io(fs_page, 0);

	nilfs_clear_copied_buffers(&sci->sc_copied_buffers, 0);

	nilfs_drop_collected_inodes(&sci->sc_dirty_files);

	if (nilfs_doing_gc()) {
		nilfs_drop_collected_inodes(&sci->sc_gc_inodes);
		if (update_sr)
			nilfs_commit_gcdat_inode(nilfs);
	} else {
		nilfs->ns_nongc_ctime = sci->sc_seg_ctime;
		set_nilfs_cond_nongc_write(nilfs);
		wake_up(&nilfs->ns_cleanerd_wq);
	}

	sci->sc_nblk_inc += sci->sc_nblk_this_inc;

	segbuf = NILFS_LAST_SEGBUF(&sci->sc_segbufs);
	nilfs_set_next_segment(nilfs, segbuf);

	if (update_sr) {
		nilfs_set_last_segment(nilfs, segbuf->sb_pseg_start,
				       segbuf->sb_sum.seg_seq, nilfs->ns_cno);

		clear_bit(NILFS_SC_DIRTY, &sci->sc_flags);
		set_bit(NILFS_SC_SUPER_ROOT, &sci->sc_flags);
	} else
		clear_bit(NILFS_SC_SUPER_ROOT, &sci->sc_flags);
}

static int nilfs_segctor_check_in_files(struct nilfs_sc_info *sci,
					struct nilfs_sb_info *sbi)
{
	struct nilfs_inode_info *ii, *n;
	__u64 cno = sbi->s_nilfs->ns_cno;

	spin_lock(&sbi->s_inode_lock);
 retry:
	list_for_each_entry_safe(ii, n, &sbi->s_dirty_files, i_dirty) {
		if (!ii->i_bh) {
			struct buffer_head *ibh;
			int err;

			spin_unlock(&sbi->s_inode_lock);
			err = nilfs_ifile_get_inode_block(
				sbi->s_ifile, ii->vfs_inode.i_ino, &ibh);
			if (unlikely(err)) {
				nilfs_warning(sbi->s_super, __func__,
					      "failed to get inode block.\n");
				return err;
			}
			nilfs_mdt_mark_buffer_dirty(ibh);
			nilfs_mdt_mark_dirty(sbi->s_ifile);
			spin_lock(&sbi->s_inode_lock);
			if (likely(!ii->i_bh))
				ii->i_bh = ibh;
			else
				brelse(ibh);
			goto retry;
		}
		ii->i_cno = cno;

		clear_bit(NILFS_I_QUEUED, &ii->i_state);
		set_bit(NILFS_I_BUSY, &ii->i_state);
		list_del(&ii->i_dirty);
		list_add_tail(&ii->i_dirty, &sci->sc_dirty_files);
	}
	spin_unlock(&sbi->s_inode_lock);

	NILFS_I(sbi->s_ifile)->i_cno = cno;

	return 0;
}

static void nilfs_segctor_check_out_files(struct nilfs_sc_info *sci,
					  struct nilfs_sb_info *sbi)
{
	struct nilfs_transaction_info *ti = current->journal_info;
	struct nilfs_inode_info *ii, *n;
	__u64 cno = sbi->s_nilfs->ns_cno;

	spin_lock(&sbi->s_inode_lock);
	list_for_each_entry_safe(ii, n, &sci->sc_dirty_files, i_dirty) {
		if (!test_and_clear_bit(NILFS_I_UPDATED, &ii->i_state) ||
		    test_bit(NILFS_I_DIRTY, &ii->i_state)) {
			/* The current checkpoint number (=nilfs->ns_cno) is
			   changed between check-in and check-out only if the
			   super root is written out.  So, we can update i_cno
			   for the inodes that remain in the dirty list. */
			ii->i_cno = cno;
			continue;
		}
		clear_bit(NILFS_I_BUSY, &ii->i_state);
		brelse(ii->i_bh);
		ii->i_bh = NULL;
		list_del(&ii->i_dirty);
		list_add_tail(&ii->i_dirty, &ti->ti_garbage);
	}
	spin_unlock(&sbi->s_inode_lock);
}

/*
 * Nasty routines to manipulate active flags on sufile.
 * These would be removed in a future release.
 */
static void nilfs_segctor_reactivate_segments(struct nilfs_sc_info *sci,
					      struct the_nilfs *nilfs)
{
	struct nilfs_segment_buffer *segbuf, *last;
	struct nilfs_segment_entry *ent, *n;
	struct inode *sufile = nilfs->ns_sufile;
	struct list_head *head;

	last = NILFS_LAST_SEGBUF(&sci->sc_segbufs);
	nilfs_for_each_segbuf_before(segbuf, last, &sci->sc_segbufs) {
		ent = segbuf->sb_segent;
		if (!ent)
			break; /* ignore unmapped segments (should check it?)*/
		nilfs_segment_usage_set_active(ent->raw_su);
		nilfs_close_segment_entry(ent, sufile);
	}

	head = &sci->sc_active_segments;
	list_for_each_entry_safe(ent, n, head, list) {
		nilfs_segment_usage_set_active(ent->raw_su);
		nilfs_close_segment_entry(ent, sufile);
	}

	down_write(&nilfs->ns_sem);
	head = &nilfs->ns_used_segments;
	list_for_each_entry(ent, head, list) {
		nilfs_segment_usage_set_volatile_active(ent->raw_su);
	}
	up_write(&nilfs->ns_sem);
}

static int nilfs_segctor_deactivate_segments(struct nilfs_sc_info *sci,
					     struct the_nilfs *nilfs)
{
	struct nilfs_segment_buffer *segbuf, *last;
	struct nilfs_segment_entry *ent;
	struct inode *sufile = nilfs->ns_sufile;
	struct list_head *head;
	int err;

	last = NILFS_LAST_SEGBUF(&sci->sc_segbufs);
	nilfs_for_each_segbuf_before(segbuf, last, &sci->sc_segbufs) {
		/*
		 * Deactivate ongoing full segments.  The last segment is kept
		 * active because it is a start point of recovery, and is not
		 * relocatable until the super block points to a newer
		 * checkpoint.
		 */
		ent = segbuf->sb_segent;
		if (!ent)
			break; /* ignore unmapped segments (should check it?)*/
		err = nilfs_open_segment_entry(ent, sufile);
		if (unlikely(err))
			goto failed;
		nilfs_segment_usage_clear_active(ent->raw_su);
		BUG_ON(!buffer_dirty(ent->bh_su));
	}

	head = &sci->sc_active_segments;
	list_for_each_entry(ent, head, list) {
		err = nilfs_open_segment_entry(ent, sufile);
		if (unlikely(err))
			goto failed;
		nilfs_segment_usage_clear_active(ent->raw_su);
		BUG_ON(!buffer_dirty(ent->bh_su));
	}

	down_write(&nilfs->ns_sem);
	head = &nilfs->ns_used_segments;
	list_for_each_entry(ent, head, list) {
		/* clear volatile active for segments of older generations */
		nilfs_segment_usage_clear_volatile_active(ent->raw_su);
	}
	up_write(&nilfs->ns_sem);
	return 0;

 failed:
	nilfs_segctor_reactivate_segments(sci, nilfs);
	return err;
}

static void nilfs_segctor_bead_completed_segments(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf, *last;
	struct nilfs_segment_entry *ent;

	/* move each segbuf->sb_segent to the list of used active segments */
	last = NILFS_LAST_SEGBUF(&sci->sc_segbufs);
	nilfs_for_each_segbuf_before(segbuf, last, &sci->sc_segbufs) {
		ent = segbuf->sb_segent;
		if (!ent)
			break; /* ignore unmapped segments (should check it?)*/
		list_add_tail(&ent->list, &sci->sc_active_segments);
		segbuf->sb_segent = NULL;
	}
}

static void
__nilfs_segctor_commit_deactivate_segments(struct nilfs_sc_info *sci,
					   struct the_nilfs *nilfs)

{
	struct nilfs_segment_entry *ent;

	list_splice_init(&sci->sc_active_segments,
			 nilfs->ns_used_segments.prev);

	list_for_each_entry(ent, &nilfs->ns_used_segments, list) {
		nilfs_segment_usage_set_volatile_active(ent->raw_su);
		/* These segments are kept open */
	}
}

/*
 * Main procedure of segment constructor
 */
static int nilfs_segctor_do_construct(struct nilfs_sc_info *sci, int mode)
{
	struct nilfs_sb_info *sbi = sci->sc_sbi;
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct page *failed_page;
	int err, has_sr = 0;

	sci->sc_stage.scnt = NILFS_ST_INIT;

	err = nilfs_segctor_check_in_files(sci, sbi);
	if (unlikely(err))
		goto out;

	if (nilfs_test_metadata_dirty(sbi))
		set_bit(NILFS_SC_DIRTY, &sci->sc_flags);

	if (nilfs_segctor_clean(sci))
		goto out;

	do {
		sci->sc_stage.flags &= ~NILFS_CF_HISTORY_MASK;

		err = nilfs_segctor_begin_construction(sci, nilfs);
		if (unlikely(err))
			goto out;

		/* Update time stamp */
		sci->sc_seg_ctime = get_seconds();

		err = nilfs_segctor_collect(sci, nilfs, mode);
		if (unlikely(err))
			goto failed;

		has_sr = (sci->sc_super_root != NULL);

		/* Avoid empty segment */
		if (sci->sc_stage.scnt == NILFS_ST_DONE &&
		    NILFS_SEG_EMPTY(&sci->sc_curseg->sb_sum)) {
			BUG_ON(mode == SC_LSEG_SR);
			nilfs_segctor_end_construction(sci, nilfs, 1);
			goto out;
		}

		err = nilfs_segctor_assign(sci, mode);
		if (unlikely(err))
			goto failed;

		if (has_sr) {
			err = nilfs_segctor_deactivate_segments(sci, nilfs);
			if (unlikely(err))
				goto failed;
		}
		if (sci->sc_stage.flags & NILFS_CF_IFILE_STARTED)
			nilfs_segctor_fill_in_file_bmap(sci, sbi->s_ifile);

		if (has_sr) {
			err = nilfs_segctor_fill_in_checkpoint(sci);
			if (unlikely(err))
				goto failed_to_make_up;

			nilfs_segctor_fill_in_super_root(sci, nilfs);
		}
		nilfs_segctor_update_segusage(sci, nilfs->ns_sufile);

		/* Write partial segments */
		err = nilfs_segctor_prepare_write(sci, &failed_page);
		if (unlikely(err))
			goto failed_to_write;

		nilfs_segctor_fill_in_checksums(sci, nilfs->ns_crc_seed);

		err = nilfs_segctor_write(sci, nilfs->ns_bdi);
		if (unlikely(err))
			goto failed_to_write;

		nilfs_segctor_complete_write(sci);

		/* Commit segments */
		nilfs_segctor_bead_completed_segments(sci);
		if (has_sr) {
			down_write(&nilfs->ns_sem);
			nilfs_update_last_segment(sbi, 1);
			__nilfs_segctor_commit_deactivate_segments(sci, nilfs);
			up_write(&nilfs->ns_sem);
			nilfs_segctor_commit_free_segments(sci);
			nilfs_segctor_clear_metadata_dirty(sci);
		}

		nilfs_segctor_end_construction(sci, nilfs, 0);

	} while (sci->sc_stage.scnt != NILFS_ST_DONE);

	/* Clearing sketch data */
	if (has_sr && sci->sc_sketch_inode) {
		if (i_size_read(sci->sc_sketch_inode) == 0)
			clear_bit(NILFS_I_DIRTY,
				  &NILFS_I(sci->sc_sketch_inode)->i_state);
		i_size_write(sci->sc_sketch_inode, 0);
	}
 out:
	nilfs_segctor_destroy_segment_buffers(sci);
	nilfs_segctor_check_out_files(sci, sbi);
	return err;

 failed_to_write:
	nilfs_segctor_abort_write(sci, failed_page, err);
	nilfs_segctor_cancel_segusage(sci, nilfs->ns_sufile);

 failed_to_make_up:
	if (sci->sc_stage.flags & NILFS_CF_IFILE_STARTED)
		nilfs_redirty_inodes(&sci->sc_dirty_files);
	if (has_sr)
		nilfs_segctor_reactivate_segments(sci, nilfs);

 failed:
	if (nilfs_doing_gc())
		nilfs_redirty_inodes(&sci->sc_gc_inodes);
	nilfs_segctor_end_construction(sci, nilfs, err);
	goto out;
}

/**
 * nilfs_secgtor_start_timer - set timer of background write
 * @sci: nilfs_sc_info
 *
 * If the timer has already been set, it ignores the new request.
 * This function MUST be called within a section locking the segment
 * semaphore.
 */
static void nilfs_segctor_start_timer(struct nilfs_sc_info *sci)
{
	spin_lock(&sci->sc_state_lock);
	if (sci->sc_timer && !(sci->sc_state & NILFS_SEGCTOR_COMMIT)) {
		sci->sc_timer->expires = jiffies + sci->sc_interval;
		add_timer(sci->sc_timer);
		sci->sc_state |= NILFS_SEGCTOR_COMMIT;
	}
	spin_unlock(&sci->sc_state_lock);
}

static void nilfs_segctor_do_flush(struct nilfs_sc_info *sci, int bn)
{
	spin_lock(&sci->sc_state_lock);
	if (!(sci->sc_flush_request & (1 << bn))) {
		unsigned long prev_req = sci->sc_flush_request;

		sci->sc_flush_request |= (1 << bn);
		if (!prev_req)
			wake_up(&sci->sc_wait_daemon);
	}
	spin_unlock(&sci->sc_state_lock);
}

/**
 * nilfs_flush_segment - trigger a segment construction for resource control
 * @sb: super block
 * @ino: inode number of the file to be flushed out.
 */
void nilfs_flush_segment(struct super_block *sb, ino_t ino)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct nilfs_sc_info *sci = NILFS_SC(sbi);

	if (!sci || nilfs_doing_construction())
		return;
	nilfs_segctor_do_flush(sci, NILFS_MDT_INODE(sb, ino) ? ino : 0);
					/* assign bit 0 to data files */
}

int nilfs_segctor_add_segments_to_be_freed(struct nilfs_sc_info *sci,
					   __u64 *segnum, size_t nsegs)
{
	struct nilfs_segment_entry *ent;
	struct the_nilfs *nilfs = sci->sc_sbi->s_nilfs;
	struct inode *sufile = nilfs->ns_sufile;
	LIST_HEAD(list);
	__u64 *pnum;
	const char *flag_name;
	size_t i;
	int err, err2 = 0;

	for (pnum = segnum, i = 0; i < nsegs; pnum++, i++) {
		ent = nilfs_alloc_segment_entry(*pnum);
		if (unlikely(!ent)) {
			err = -ENOMEM;
			goto failed;
		}
		list_add_tail(&ent->list, &list);

		err = nilfs_open_segment_entry(ent, sufile);
		if (unlikely(err))
			goto failed;

		if (unlikely(le32_to_cpu(ent->raw_su->su_flags) !=
			     (1UL << NILFS_SEGMENT_USAGE_DIRTY))) {
			if (nilfs_segment_usage_clean(ent->raw_su))
				flag_name = "clean";
			else if (nilfs_segment_usage_active(ent->raw_su))
				flag_name = "active";
			else if (nilfs_segment_usage_volatile_active(
					 ent->raw_su))
				flag_name = "volatile active";
			else if (!nilfs_segment_usage_dirty(ent->raw_su))
				flag_name = "non-dirty";
			else
				flag_name = "erroneous";

			printk(KERN_ERR
			       "NILFS: %s segment is requested to be cleaned "
			       "(segnum=%llu)\n",
			       flag_name, (unsigned long long)ent->segnum);
			err2 = -EINVAL;
		}
		nilfs_close_segment_entry(ent, sufile);
	}
	if (unlikely(err2)) {
		err = err2;
		goto failed;
	}
	list_splice(&list, sci->sc_cleaning_segments.prev);
	return 0;

 failed:
	nilfs_dispose_segment_list(&list);
	return err;
}

void nilfs_segctor_clear_segments_to_be_freed(struct nilfs_sc_info *sci)
{
	nilfs_dispose_segment_list(&sci->sc_cleaning_segments);
}

struct nilfs_segctor_wait_request {
	wait_queue_t	wq;
	__u32		seq;
	int		err;
	atomic_t	done;
};

static int nilfs_segctor_sync(struct nilfs_sc_info *sci)
{
	struct nilfs_segctor_wait_request wait_req;
	int err = 0;

	spin_lock(&sci->sc_state_lock);
	init_wait(&wait_req.wq);
	wait_req.err = 0;
	atomic_set(&wait_req.done, 0);
	wait_req.seq = ++sci->sc_seq_request;
	spin_unlock(&sci->sc_state_lock);

	init_waitqueue_entry(&wait_req.wq, current);
	add_wait_queue(&sci->sc_wait_request, &wait_req.wq);
	set_current_state(TASK_INTERRUPTIBLE);
	wake_up(&sci->sc_wait_daemon);

	for (;;) {
		if (atomic_read(&wait_req.done)) {
			err = wait_req.err;
			break;
		}
		if (!signal_pending(current)) {
			schedule();
			continue;
		}
		err = -ERESTARTSYS;
		break;
	}
	finish_wait(&sci->sc_wait_request, &wait_req.wq);
	return err;
}

static void nilfs_segctor_wakeup(struct nilfs_sc_info *sci, int err)
{
	struct nilfs_segctor_wait_request *wrq, *n;
	unsigned long flags;

	spin_lock_irqsave(&sci->sc_wait_request.lock, flags);
	list_for_each_entry_safe(wrq, n, &sci->sc_wait_request.task_list,
				 wq.task_list) {
		if (!atomic_read(&wrq->done) &&
		    nilfs_cnt32_ge(sci->sc_seq_done, wrq->seq)) {
			wrq->err = err;
			atomic_set(&wrq->done, 1);
		}
		if (atomic_read(&wrq->done)) {
			wrq->wq.func(&wrq->wq,
				     TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE,
				     0, NULL);
		}
	}
	spin_unlock_irqrestore(&sci->sc_wait_request.lock, flags);
}

/**
 * nilfs_construct_segment - construct a logical segment
 * @sb: super block
 *
 * Return Value: On success, 0 is retured. On errors, one of the following
 * negative error code is returned.
 *
 * %-EROFS - Read only filesystem.
 *
 * %-EIO - I/O error
 *
 * %-ENOSPC - No space left on device (only in a panic state).
 *
 * %-ERESTARTSYS - Interrupted.
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_construct_segment(struct super_block *sb)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct nilfs_sc_info *sci = NILFS_SC(sbi);
	struct nilfs_transaction_info *ti;
	int err;

	if (!sci)
		return -EROFS;

	/* A call inside transactions causes a deadlock. */
	BUG_ON((ti = current->journal_info) && ti->ti_magic == NILFS_TI_MAGIC);

	err = nilfs_segctor_sync(sci);
	return err;
}

/**
 * nilfs_construct_dsync_segment - construct a data-only logical segment
 * @sb: super block
 * @inode: the inode whose data blocks should be written out
 *
 * Return Value: On success, 0 is retured. On errors, one of the following
 * negative error code is returned.
 *
 * %-EROFS - Read only filesystem.
 *
 * %-EIO - I/O error
 *
 * %-ENOSPC - No space left on device (only in a panic state).
 *
 * %-ERESTARTSYS - Interrupted.
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_construct_dsync_segment(struct super_block *sb,
				  struct inode *inode)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct nilfs_sc_info *sci = NILFS_SC(sbi);
	struct nilfs_inode_info *ii;
	struct nilfs_transaction_info ti;
	int err = 0;

	if (!sci)
		return -EROFS;

	nilfs_transaction_lock(sbi, &ti, 0);

	ii = NILFS_I(inode);
	if (test_bit(NILFS_I_INODE_DIRTY, &ii->i_state) ||
	    nilfs_test_opt(sbi, STRICT_ORDER) ||
	    test_bit(NILFS_SC_UNCLOSED, &sci->sc_flags) ||
	    nilfs_discontinued(sbi->s_nilfs)) {
		nilfs_transaction_unlock(sbi);
		err = nilfs_segctor_sync(sci);
		return err;
	}

	spin_lock(&sbi->s_inode_lock);
	if (!test_bit(NILFS_I_QUEUED, &ii->i_state) &&
	    !test_bit(NILFS_I_BUSY, &ii->i_state)) {
		spin_unlock(&sbi->s_inode_lock);
		nilfs_transaction_unlock(sbi);
		return 0;
	}
	spin_unlock(&sbi->s_inode_lock);
	sci->sc_stage.dirty_file_ptr = ii;

	err = nilfs_segctor_do_construct(sci, SC_LSEG_DSYNC);

	nilfs_transaction_unlock(sbi);
	return err;
}

struct nilfs_segctor_req {
	int mode;
	__u32 seq_accepted;
	int sc_err;  /* construction failure */
	int sb_err;  /* super block writeback failure */
};

#define FLUSH_FILE_BIT	(0x1) /* data file only */
#define FLUSH_DAT_BIT	(1 << NILFS_DAT_INO) /* DAT only */

static void nilfs_segctor_accept(struct nilfs_sc_info *sci,
				 struct nilfs_segctor_req *req)
{
	BUG_ON(!sci);

	req->sc_err = req->sb_err = 0;
	spin_lock(&sci->sc_state_lock);
	req->seq_accepted = sci->sc_seq_request;
	spin_unlock(&sci->sc_state_lock);

	if (sci->sc_timer)
		del_timer_sync(sci->sc_timer);
}

static void nilfs_segctor_notify(struct nilfs_sc_info *sci,
				 struct nilfs_segctor_req *req)
{
	/* Clear requests (even when the construction failed) */
	spin_lock(&sci->sc_state_lock);

	sci->sc_state &= ~NILFS_SEGCTOR_COMMIT;

	if (req->mode == SC_LSEG_SR) {
		sci->sc_seq_done = req->seq_accepted;
		nilfs_segctor_wakeup(sci, req->sc_err ? : req->sb_err);
		sci->sc_flush_request = 0;
	} else if (req->mode == SC_FLUSH_FILE)
		sci->sc_flush_request &= ~FLUSH_FILE_BIT;
	else if (req->mode == SC_FLUSH_DAT)
		sci->sc_flush_request &= ~FLUSH_DAT_BIT;

	spin_unlock(&sci->sc_state_lock);
}

static int nilfs_segctor_construct(struct nilfs_sc_info *sci,
				   struct nilfs_segctor_req *req)
{
	struct nilfs_sb_info *sbi = sci->sc_sbi;
	struct the_nilfs *nilfs = sbi->s_nilfs;
	int err = 0;

	if (nilfs_discontinued(nilfs))
		req->mode = SC_LSEG_SR;
	if (!nilfs_segctor_confirm(sci)) {
		err = nilfs_segctor_do_construct(sci, req->mode);
		req->sc_err = err;
	}
	if (likely(!err)) {
		if (req->mode != SC_FLUSH_DAT)
			atomic_set(&nilfs->ns_ndirtyblks, 0);
		if (test_bit(NILFS_SC_SUPER_ROOT, &sci->sc_flags) &&
		    nilfs_discontinued(nilfs)) {
			down_write(&nilfs->ns_sem);
			req->sb_err = nilfs_commit_super(sbi);
			up_write(&nilfs->ns_sem);
		}
	}
	return err;
}

static void nilfs_construction_timeout(unsigned long data)
{
	struct task_struct *p = (struct task_struct *)data;
	wake_up_process(p);
}

static void
nilfs_remove_written_gcinodes(struct the_nilfs *nilfs, struct list_head *head)
{
	struct nilfs_inode_info *ii, *n;

	list_for_each_entry_safe(ii, n, head, i_dirty) {
		if (!test_bit(NILFS_I_UPDATED, &ii->i_state))
			continue;
		hlist_del_init(&ii->vfs_inode.i_hash);
		list_del_init(&ii->i_dirty);
		nilfs_clear_gcinode(&ii->vfs_inode);
	}
}

int nilfs_clean_segments(struct super_block *sb, void __user *argp)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct nilfs_sc_info *sci = NILFS_SC(sbi);
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_transaction_info ti;
	struct nilfs_segctor_req req = { .mode = SC_LSEG_SR };
	int err;

	if (unlikely(!sci))
		return -EROFS;

	nilfs_transaction_lock(sbi, &ti, 1);

	err = nilfs_init_gcdat_inode(nilfs);
	if (unlikely(err))
		goto out_unlock;
	err = nilfs_ioctl_prepare_clean_segments(nilfs, argp);
	if (unlikely(err))
		goto out_unlock;

	list_splice_init(&nilfs->ns_gc_inodes, sci->sc_gc_inodes.prev);

	for (;;) {
		nilfs_segctor_accept(sci, &req);
		err = nilfs_segctor_construct(sci, &req);
		nilfs_remove_written_gcinodes(nilfs, &sci->sc_gc_inodes);
		nilfs_segctor_notify(sci, &req);

		if (likely(!err))
			break;

		nilfs_warning(sb, __func__,
			      "segment construction failed. (err=%d)", err);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(sci->sc_interval);
	}

 out_unlock:
	nilfs_clear_gcdat_inode(nilfs);
	nilfs_transaction_unlock(sbi);
	return err;
}

static void nilfs_segctor_thread_construct(struct nilfs_sc_info *sci, int mode)
{
	struct nilfs_sb_info *sbi = sci->sc_sbi;
	struct nilfs_transaction_info ti;
	struct nilfs_segctor_req req = { .mode = mode };

	nilfs_transaction_lock(sbi, &ti, 0);

	nilfs_segctor_accept(sci, &req);
	nilfs_segctor_construct(sci, &req);
	nilfs_segctor_notify(sci, &req);

	/*
	 * Unclosed segment should be retried.  We do this using sc_timer.
	 * Timeout of sc_timer will invoke complete construction which leads
	 * to close the current logical segment.
	 */
	if (test_bit(NILFS_SC_UNCLOSED, &sci->sc_flags))
		nilfs_segctor_start_timer(sci);

	nilfs_transaction_unlock(sbi);
}

static void nilfs_segctor_do_immediate_flush(struct nilfs_sc_info *sci)
{
	int mode = 0;
	int err;

	spin_lock(&sci->sc_state_lock);
	mode = (sci->sc_flush_request & FLUSH_DAT_BIT) ?
		SC_FLUSH_DAT : SC_FLUSH_FILE;
	spin_unlock(&sci->sc_state_lock);

	if (mode) {
		err = nilfs_segctor_do_construct(sci, mode);

		spin_lock(&sci->sc_state_lock);
		sci->sc_flush_request &= (mode == SC_FLUSH_FILE) ?
			~FLUSH_FILE_BIT : ~FLUSH_DAT_BIT;
		spin_unlock(&sci->sc_state_lock);
	}
	clear_bit(NILFS_SC_PRIOR_FLUSH, &sci->sc_flags);
}

static int nilfs_segctor_flush_mode(struct nilfs_sc_info *sci)
{
	if (!test_bit(NILFS_SC_UNCLOSED, &sci->sc_flags) ||
	    time_before(jiffies, sci->sc_lseg_stime + sci->sc_mjcp_freq)) {
		if (!(sci->sc_flush_request & ~FLUSH_FILE_BIT))
			return SC_FLUSH_FILE;
		else if (!(sci->sc_flush_request & ~FLUSH_DAT_BIT))
			return SC_FLUSH_DAT;
	}
	return SC_LSEG_SR;
}

/**
 * nilfs_segctor_thread - main loop of the segment constructor thread.
 * @arg: pointer to a struct nilfs_sc_info.
 *
 * nilfs_segctor_thread() initializes a timer and serves as a daemon
 * to execute segment constructions.
 */
static int nilfs_segctor_thread(void *arg)
{
	struct nilfs_sc_info *sci = (struct nilfs_sc_info *)arg;
	struct timer_list timer;
	int timeout = 0;

	init_timer(&timer);
	timer.data = (unsigned long)current;
	timer.function = nilfs_construction_timeout;
	sci->sc_timer = &timer;

	/* start sync. */
	sci->sc_task = current;
	wake_up(&sci->sc_wait_task); /* for nilfs_segctor_start_thread() */
	printk(KERN_INFO
	       "segctord starting. Construction interval = %lu seconds, "
	       "CP frequency < %lu seconds\n",
	       sci->sc_interval / HZ, sci->sc_mjcp_freq / HZ);

	spin_lock(&sci->sc_state_lock);
 loop:
	for (;;) {
		int mode;

		if (sci->sc_state & NILFS_SEGCTOR_QUIT)
			goto end_thread;

		if (timeout || sci->sc_seq_request != sci->sc_seq_done)
			mode = SC_LSEG_SR;
		else if (!sci->sc_flush_request)
			break;
		else
			mode = nilfs_segctor_flush_mode(sci);

		spin_unlock(&sci->sc_state_lock);
		nilfs_segctor_thread_construct(sci, mode);
		spin_lock(&sci->sc_state_lock);
		timeout = 0;
	}


	if (freezing(current)) {
		spin_unlock(&sci->sc_state_lock);
		refrigerator();
		spin_lock(&sci->sc_state_lock);
	} else {
		DEFINE_WAIT(wait);
		int should_sleep = 1;

		prepare_to_wait(&sci->sc_wait_daemon, &wait,
				TASK_INTERRUPTIBLE);

		if (sci->sc_seq_request != sci->sc_seq_done)
			should_sleep = 0;
		else if (sci->sc_flush_request)
			should_sleep = 0;
		else if (sci->sc_state & NILFS_SEGCTOR_COMMIT)
			should_sleep = time_before(jiffies,
						   sci->sc_timer->expires);

		if (should_sleep) {
			spin_unlock(&sci->sc_state_lock);
			schedule();
			spin_lock(&sci->sc_state_lock);
		}
		finish_wait(&sci->sc_wait_daemon, &wait);
		timeout = ((sci->sc_state & NILFS_SEGCTOR_COMMIT) &&
			   time_after_eq(jiffies, sci->sc_timer->expires));
	}
	goto loop;

 end_thread:
	spin_unlock(&sci->sc_state_lock);
	del_timer_sync(sci->sc_timer);
	sci->sc_timer = NULL;

	/* end sync. */
	sci->sc_task = NULL;
	wake_up(&sci->sc_wait_task); /* for nilfs_segctor_kill_thread() */
	return 0;
}

static int nilfs_segctor_start_thread(struct nilfs_sc_info *sci)
{
	struct task_struct *t;

	t = kthread_run(nilfs_segctor_thread, sci, "segctord");
	if (IS_ERR(t)) {
		int err = PTR_ERR(t);

		printk(KERN_ERR "NILFS: error %d creating segctord thread\n",
		       err);
		return err;
	}
	wait_event(sci->sc_wait_task, sci->sc_task != NULL);
	return 0;
}

static void nilfs_segctor_kill_thread(struct nilfs_sc_info *sci)
{
	sci->sc_state |= NILFS_SEGCTOR_QUIT;

	while (sci->sc_task) {
		wake_up(&sci->sc_wait_daemon);
		spin_unlock(&sci->sc_state_lock);
		wait_event(sci->sc_wait_task, sci->sc_task == NULL);
		spin_lock(&sci->sc_state_lock);
	}
}

static int nilfs_segctor_init(struct nilfs_sc_info *sci,
			      struct nilfs_recovery_info *ri)
{
	int err;
	struct inode *inode = nilfs_iget(sci->sc_super, NILFS_SKETCH_INO);

	sci->sc_sketch_inode = IS_ERR(inode) ? NULL : inode;
	if (sci->sc_sketch_inode)
		i_size_write(sci->sc_sketch_inode, 0);

	sci->sc_seq_done = sci->sc_seq_request;
	if (ri)
		list_splice_init(&ri->ri_used_segments,
				 sci->sc_active_segments.prev);

	err = nilfs_segctor_start_thread(sci);
	if (err) {
		if (ri)
			list_splice_init(&sci->sc_active_segments,
					 ri->ri_used_segments.prev);
		if (sci->sc_sketch_inode) {
			iput(sci->sc_sketch_inode);
			sci->sc_sketch_inode = NULL;
		}
	}
	return err;
}

/*
 * Setup & clean-up functions
 */
static struct nilfs_sc_info *nilfs_segctor_new(struct nilfs_sb_info *sbi)
{
	struct nilfs_sc_info *sci;

	sci = kzalloc(sizeof(*sci), GFP_KERNEL);
	if (!sci)
		return NULL;

	sci->sc_sbi = sbi;
	sci->sc_super = sbi->s_super;

	init_waitqueue_head(&sci->sc_wait_request);
	init_waitqueue_head(&sci->sc_wait_daemon);
	init_waitqueue_head(&sci->sc_wait_task);
	spin_lock_init(&sci->sc_state_lock);
	INIT_LIST_HEAD(&sci->sc_dirty_files);
	INIT_LIST_HEAD(&sci->sc_segbufs);
	INIT_LIST_HEAD(&sci->sc_gc_inodes);
	INIT_LIST_HEAD(&sci->sc_active_segments);
	INIT_LIST_HEAD(&sci->sc_cleaning_segments);
	INIT_LIST_HEAD(&sci->sc_copied_buffers);

	sci->sc_interval = HZ * NILFS_SC_DEFAULT_TIMEOUT;
	sci->sc_mjcp_freq = HZ * NILFS_SC_DEFAULT_SR_FREQ;
	sci->sc_watermark = NILFS_SC_DEFAULT_WATERMARK;

	if (sbi->s_interval)
		sci->sc_interval = sbi->s_interval;
	if (sbi->s_watermark)
		sci->sc_watermark = sbi->s_watermark;
	return sci;
}

static void nilfs_segctor_write_out(struct nilfs_sc_info *sci)
{
	int ret, retrycount = NILFS_SC_CLEANUP_RETRY;

	/* The segctord thread was stopped and its timer was removed.
	   But some tasks remain. */
	do {
		struct nilfs_sb_info *sbi = sci->sc_sbi;
		struct nilfs_transaction_info ti;
		struct nilfs_segctor_req req = { .mode = SC_LSEG_SR };

		nilfs_transaction_lock(sbi, &ti, 0);
		nilfs_segctor_accept(sci, &req);
		ret = nilfs_segctor_construct(sci, &req);
		nilfs_segctor_notify(sci, &req);
		nilfs_transaction_unlock(sbi);

	} while (ret && retrycount-- > 0);
}

/**
 * nilfs_segctor_destroy - destroy the segment constructor.
 * @sci: nilfs_sc_info
 *
 * nilfs_segctor_destroy() kills the segctord thread and frees
 * the nilfs_sc_info struct.
 * Caller must hold the segment semaphore.
 */
static void nilfs_segctor_destroy(struct nilfs_sc_info *sci)
{
	struct nilfs_sb_info *sbi = sci->sc_sbi;
	int flag;

	up_write(&sbi->s_nilfs->ns_segctor_sem);

	spin_lock(&sci->sc_state_lock);
	nilfs_segctor_kill_thread(sci);
	flag = ((sci->sc_state & NILFS_SEGCTOR_COMMIT) || sci->sc_flush_request
		|| sci->sc_seq_request != sci->sc_seq_done);
	spin_unlock(&sci->sc_state_lock);

	if (flag || nilfs_segctor_confirm(sci))
		nilfs_segctor_write_out(sci);

	BUG_ON(!list_empty(&sci->sc_copied_buffers));

	if (!list_empty(&sci->sc_dirty_files)) {
		nilfs_warning(sbi->s_super, __func__,
			      "dirty file(s) after the final construction\n");
		nilfs_dispose_list(sbi, &sci->sc_dirty_files, 1);
	}
	if (!list_empty(&sci->sc_active_segments))
		nilfs_dispose_segment_list(&sci->sc_active_segments);

	if (!list_empty(&sci->sc_cleaning_segments))
		nilfs_dispose_segment_list(&sci->sc_cleaning_segments);

	BUG_ON(!list_empty(&sci->sc_segbufs));

	if (sci->sc_sketch_inode) {
		iput(sci->sc_sketch_inode);
		sci->sc_sketch_inode = NULL;
	}
	down_write(&sbi->s_nilfs->ns_segctor_sem);

	kfree(sci);
}

/**
 * nilfs_attach_segment_constructor - attach a segment constructor
 * @sbi: nilfs_sb_info
 * @ri: nilfs_recovery_info
 *
 * nilfs_attach_segment_constructor() allocates a struct nilfs_sc_info,
 * initilizes it, and starts the segment constructor.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_attach_segment_constructor(struct nilfs_sb_info *sbi,
				     struct nilfs_recovery_info *ri)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	int err;

	/* Each field of nilfs_segctor is cleared through the initialization
	   of super-block info */
	sbi->s_sc_info = nilfs_segctor_new(sbi);
	if (!sbi->s_sc_info)
		return -ENOMEM;

	nilfs_attach_writer(nilfs, sbi);
	err = nilfs_segctor_init(NILFS_SC(sbi), ri);
	if (err) {
		nilfs_detach_writer(nilfs, sbi);
		kfree(sbi->s_sc_info);
		sbi->s_sc_info = NULL;
	}
	return err;
}

/**
 * nilfs_detach_segment_constructor - destroy the segment constructor
 * @sbi: nilfs_sb_info
 *
 * nilfs_detach_segment_constructor() kills the segment constructor daemon,
 * frees the struct nilfs_sc_info, and destroy the dirty file list.
 */
void nilfs_detach_segment_constructor(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	LIST_HEAD(garbage_list);

	down_write(&nilfs->ns_segctor_sem);
	if (NILFS_SC(sbi)) {
		nilfs_segctor_destroy(NILFS_SC(sbi));
		sbi->s_sc_info = NULL;
	}

	/* Force to free the list of dirty files */
	spin_lock(&sbi->s_inode_lock);
	if (!list_empty(&sbi->s_dirty_files)) {
		list_splice_init(&sbi->s_dirty_files, &garbage_list);
		nilfs_warning(sbi->s_super, __func__,
			      "Non empty dirty list after the last "
			      "segment construction\n");
	}
	spin_unlock(&sbi->s_inode_lock);
	up_write(&nilfs->ns_segctor_sem);

	nilfs_dispose_list(sbi, &garbage_list, 1);
	nilfs_detach_writer(nilfs, sbi);
}
