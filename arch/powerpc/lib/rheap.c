/*
 * arch/ppc/syslib/rheap.c
 *
 * A Remote Heap.  Remote means that we don't touch the memory that the
 * heap points to. Normal heap implementations use the memory they manage
 * to place their list. We cannot do that because the memory we manage may
 * have special properties, for example it is uncachable or of different
 * endianess.
 *
 * Author: Pantelis Antoniou <panto@intracom.gr>
 *
 * 2004 (c) INTRACOM S.A. Greece. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/rheap.h>

/*
 * Fixup a list_head, needed when copying lists.  If the pointers fall
 * between s and e, apply the delta.  This assumes that
 * sizeof(struct list_head *) == sizeof(unsigned long *).
 */
static inline void fixup(unsigned long s, unsigned long e, int d,
			 struct list_head *l)
{
	unsigned long *pp;

	pp = (unsigned long *)&l->next;
	if (*pp >= s && *pp < e)
		*pp += d;

	pp = (unsigned long *)&l->prev;
	if (*pp >= s && *pp < e)
		*pp += d;
}

/* Grow the allocated blocks */
static int grow(rh_info_t * info, int max_blocks)
{
	rh_block_t *block, *blk;
	int i, new_blocks;
	int delta;
	unsigned long blks, blke;

	if (max_blocks <= info->max_blocks)
		return -EINVAL;

	new_blocks = max_blocks - info->max_blocks;

	block = kmalloc(sizeof(rh_block_t) * max_blocks, GFP_KERNEL);
	if (block == NULL)
		return -ENOMEM;

	if (info->max_blocks > 0) {

		/* copy old block area */
		memcpy(block, info->block,
		       sizeof(rh_block_t) * info->max_blocks);

		delta = (char *)block - (char *)info->block;

		/* and fixup list pointers */
		blks = (unsigned long)info->block;
		blke = (unsigned long)(info->block + info->max_blocks);

		for (i = 0, blk = block; i < info->max_blocks; i++, blk++)
			fixup(blks, blke, delta, &blk->list);

		fixup(blks, blke, delta, &info->empty_list);
		fixup(blks, blke, delta, &info->free_list);
		fixup(blks, blke, delta, &info->taken_list);

		/* free the old allocated memory */
		if ((info->flags & RHIF_STATIC_BLOCK) == 0)
			kfree(info->block);
	}

	info->block = block;
	info->empty_slots += new_blocks;
	info->max_blocks = max_blocks;
	info->flags &= ~RHIF_STATIC_BLOCK;

	/* add all new blocks to the free list */
	for (i = 0, blk = block + info->max_blocks; i < new_blocks; i++, blk++)
		list_add(&blk->list, &info->empty_list);

	return 0;
}

/*
 * Assure at least the required amount of empty slots.  If this function
 * causes a grow in the block area then all pointers kept to the block
 * area are invalid!
 */
static int assure_empty(rh_info_t * info, int slots)
{
	int max_blocks;

	/* This function is not meant to be used to grow uncontrollably */
	if (slots >= 4)
		return -EINVAL;

	/* Enough space */
	if (info->empty_slots >= slots)
		return 0;

	/* Next 16 sized block */
	max_blocks = ((info->max_blocks + slots) + 15) & ~15;

	return grow(info, max_blocks);
}

static rh_block_t *get_slot(rh_info_t * info)
{
	rh_block_t *blk;

	/* If no more free slots, and failure to extend. */
	/* XXX: You should have called assure_empty before */
	if (info->empty_slots == 0) {
		printk(KERN_ERR "rh: out of slots; crash is imminent.\n");
		return NULL;
	}

	/* Get empty slot to use */
	blk = list_entry(info->empty_list.next, rh_block_t, list);
	list_del_init(&blk->list);
	info->empty_slots--;

	/* Initialize */
	blk->start = NULL;
	blk->size = 0;
	blk->owner = NULL;

	return blk;
}

static inline void release_slot(rh_info_t * info, rh_block_t * blk)
{
	list_add(&blk->list, &info->empty_list);
	info->empty_slots++;
}

static void attach_free_block(rh_info_t * info, rh_block_t * blkn)
{
	rh_block_t *blk;
	rh_block_t *before;
	rh_block_t *after;
	rh_block_t *next;
	int size;
	unsigned long s, e, bs, be;
	struct list_head *l;

	/* We assume that they are aligned properly */
	size = blkn->size;
	s = (unsigned long)blkn->start;
	e = s + size;

	/* Find the blocks immediately before and after the given one
	 * (if any) */
	before = NULL;
	after = NULL;
	next = NULL;

	list_for_each(l, &info->free_list) {
		blk = list_entry(l, rh_block_t, list);

		bs = (unsigned long)blk->start;
		be = bs + blk->size;

		if (next == NULL && s >= bs)
			next = blk;

		if (be == s)
			before = blk;

		if (e == bs)
			after = blk;

		/* If both are not null, break now */
		if (before != NULL && after != NULL)
			break;
	}

	/* Now check if they are really adjacent */
	if (before != NULL && s != (unsigned long)before->start + before->size)
		before = NULL;

	if (after != NULL && e != (unsigned long)after->start)
		after = NULL;

	/* No coalescing; list insert and return */
	if (before == NULL && after == NULL) {

		if (next != NULL)
			list_add(&blkn->list, &next->list);
		else
			list_add(&blkn->list, &info->free_list);

		return;
	}

	/* We don't need it anymore */
	release_slot(info, blkn);

	/* Grow the before block */
	if (before != NULL && after == NULL) {
		before->size += size;
		return;
	}

	/* Grow the after block backwards */
	if (before == NULL && after != NULL) {
		after->start = (int8_t *)after->start - size;
		after->size += size;
		return;
	}

	/* Grow the before block, and release the after block */
	before->size += size + after->size;
	list_del(&after->list);
	release_slot(info, after);
}

static void attach_taken_block(rh_info_t * info, rh_block_t * blkn)
{
	rh_block_t *blk;
	struct list_head *l;

	/* Find the block immediately before the given one (if any) */
	list_for_each(l, &info->taken_list) {
		blk = list_entry(l, rh_block_t, list);
		if (blk->start > blkn->start) {
			list_add_tail(&blkn->list, &blk->list);
			return;
		}
	}

	list_add_tail(&blkn->list, &info->taken_list);
}

/*
 * Create a remote heap dynamically.  Note that no memory for the blocks
 * are allocated.  It will upon the first allocation
 */
rh_info_t *rh_create(unsigned int alignment)
{
	rh_info_t *info;

	/* Alignment must be a power of two */
	if ((alignment & (alignment - 1)) != 0)
		return ERR_PTR(-EINVAL);

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return ERR_PTR(-ENOMEM);

	info->alignment = alignment;

	/* Initially everything as empty */
	info->block = NULL;
	info->max_blocks = 0;
	info->empty_slots = 0;
	info->flags = 0;

	INIT_LIST_HEAD(&info->empty_list);
	INIT_LIST_HEAD(&info->free_list);
	INIT_LIST_HEAD(&info->taken_list);

	return info;
}

/*
 * Destroy a dynamically created remote heap.  Deallocate only if the areas
 * are not static
 */
void rh_destroy(rh_info_t * info)
{
	if ((info->flags & RHIF_STATIC_BLOCK) == 0 && info->block != NULL)
		kfree(info->block);

	if ((info->flags & RHIF_STATIC_INFO) == 0)
		kfree(info);
}

/*
 * Initialize in place a remote heap info block.  This is needed to support
 * operation very early in the startup of the kernel, when it is not yet safe
 * to call kmalloc.
 */
void rh_init(rh_info_t * info, unsigned int alignment, int max_blocks,
	     rh_block_t * block)
{
	int i;
	rh_block_t *blk;

	/* Alignment must be a power of two */
	if ((alignment & (alignment - 1)) != 0)
		return;

	info->alignment = alignment;

	/* Initially everything as empty */
	info->block = block;
	info->max_blocks = max_blocks;
	info->empty_slots = max_blocks;
	info->flags = RHIF_STATIC_INFO | RHIF_STATIC_BLOCK;

	INIT_LIST_HEAD(&info->empty_list);
	INIT_LIST_HEAD(&info->free_list);
	INIT_LIST_HEAD(&info->taken_list);

	/* Add all new blocks to the free list */
	for (i = 0, blk = block; i < max_blocks; i++, blk++)
		list_add(&blk->list, &info->empty_list);
}

/* Attach a free memory region, coalesces regions if adjuscent */
int rh_attach_region(rh_info_t * info, void *start, int size)
{
	rh_block_t *blk;
	unsigned long s, e, m;
	int r;

	/* The region must be aligned */
	s = (unsigned long)start;
	e = s + size;
	m = info->alignment - 1;

	/* Round start up */
	s = (s + m) & ~m;

	/* Round end down */
	e = e & ~m;

	/* Take final values */
	start = (void *)s;
	size = (int)(e - s);

	/* Grow the blocks, if needed */
	r = assure_empty(info, 1);
	if (r < 0)
		return r;

	blk = get_slot(info);
	blk->start = start;
	blk->size = size;
	blk->owner = NULL;

	attach_free_block(info, blk);

	return 0;
}

/* Detatch given address range, splits free block if needed. */
void *rh_detach_region(rh_info_t * info, void *start, int size)
{
	struct list_head *l;
	rh_block_t *blk, *newblk;
	unsigned long s, e, m, bs, be;

	/* Validate size */
	if (size <= 0)
		return ERR_PTR(-EINVAL);

	/* The region must be aligned */
	s = (unsigned long)start;
	e = s + size;
	m = info->alignment - 1;

	/* Round start up */
	s = (s + m) & ~m;

	/* Round end down */
	e = e & ~m;

	if (assure_empty(info, 1) < 0)
		return ERR_PTR(-ENOMEM);

	blk = NULL;
	list_for_each(l, &info->free_list) {
		blk = list_entry(l, rh_block_t, list);
		/* The range must lie entirely inside one free block */
		bs = (unsigned long)blk->start;
		be = (unsigned long)blk->start + blk->size;
		if (s >= bs && e <= be)
			break;
		blk = NULL;
	}

	if (blk == NULL)
		return ERR_PTR(-ENOMEM);

	/* Perfect fit */
	if (bs == s && be == e) {
		/* Delete from free list, release slot */
		list_del(&blk->list);
		release_slot(info, blk);
		return (void *)s;
	}

	/* blk still in free list, with updated start and/or size */
	if (bs == s || be == e) {
		if (bs == s)
			blk->start = (int8_t *)blk->start + size;
		blk->size -= size;

	} else {
		/* The front free fragment */
		blk->size = s - bs;

		/* the back free fragment */
		newblk = get_slot(info);
		newblk->start = (void *)e;
		newblk->size = be - e;

		list_add(&newblk->list, &blk->list);
	}

	return (void *)s;
}

void *rh_alloc(rh_info_t * info, int size, const char *owner)
{
	struct list_head *l;
	rh_block_t *blk;
	rh_block_t *newblk;
	void *start;

	/* Validate size */
	if (size <= 0)
		return ERR_PTR(-EINVAL);

	/* Align to configured alignment */
	size = (size + (info->alignment - 1)) & ~(info->alignment - 1);

	if (assure_empty(info, 1) < 0)
		return ERR_PTR(-ENOMEM);

	blk = NULL;
	list_for_each(l, &info->free_list) {
		blk = list_entry(l, rh_block_t, list);
		if (size <= blk->size)
			break;
		blk = NULL;
	}

	if (blk == NULL)
		return ERR_PTR(-ENOMEM);

	/* Just fits */
	if (blk->size == size) {
		/* Move from free list to taken list */
		list_del(&blk->list);
		blk->owner = owner;
		start = blk->start;

		attach_taken_block(info, blk);

		return start;
	}

	newblk = get_slot(info);
	newblk->start = blk->start;
	newblk->size = size;
	newblk->owner = owner;

	/* blk still in free list, with updated start, size */
	blk->start = (int8_t *)blk->start + size;
	blk->size -= size;

	start = newblk->start;

	attach_taken_block(info, newblk);

	return start;
}

/* allocate at precisely the given address */
void *rh_alloc_fixed(rh_info_t * info, void *start, int size, const char *owner)
{
	struct list_head *l;
	rh_block_t *blk, *newblk1, *newblk2;
	unsigned long s, e, m, bs, be;

	/* Validate size */
	if (size <= 0)
		return ERR_PTR(-EINVAL);

	/* The region must be aligned */
	s = (unsigned long)start;
	e = s + size;
	m = info->alignment - 1;

	/* Round start up */
	s = (s + m) & ~m;

	/* Round end down */
	e = e & ~m;

	if (assure_empty(info, 2) < 0)
		return ERR_PTR(-ENOMEM);

	blk = NULL;
	list_for_each(l, &info->free_list) {
		blk = list_entry(l, rh_block_t, list);
		/* The range must lie entirely inside one free block */
		bs = (unsigned long)blk->start;
		be = (unsigned long)blk->start + blk->size;
		if (s >= bs && e <= be)
			break;
	}

	if (blk == NULL)
		return ERR_PTR(-ENOMEM);

	/* Perfect fit */
	if (bs == s && be == e) {
		/* Move from free list to taken list */
		list_del(&blk->list);
		blk->owner = owner;

		start = blk->start;
		attach_taken_block(info, blk);

		return start;

	}

	/* blk still in free list, with updated start and/or size */
	if (bs == s || be == e) {
		if (bs == s)
			blk->start = (int8_t *)blk->start + size;
		blk->size -= size;

	} else {
		/* The front free fragment */
		blk->size = s - bs;

		/* The back free fragment */
		newblk2 = get_slot(info);
		newblk2->start = (void *)e;
		newblk2->size = be - e;

		list_add(&newblk2->list, &blk->list);
	}

	newblk1 = get_slot(info);
	newblk1->start = (void *)s;
	newblk1->size = e - s;
	newblk1->owner = owner;

	start = newblk1->start;
	attach_taken_block(info, newblk1);

	return start;
}

int rh_free(rh_info_t * info, void *start)
{
	rh_block_t *blk, *blk2;
	struct list_head *l;
	int size;

	/* Linear search for block */
	blk = NULL;
	list_for_each(l, &info->taken_list) {
		blk2 = list_entry(l, rh_block_t, list);
		if (start < blk2->start)
			break;
		blk = blk2;
	}

	if (blk == NULL || start > (blk->start + blk->size))
		return -EINVAL;

	/* Remove from taken list */
	list_del(&blk->list);

	/* Get size of freed block */
	size = blk->size;
	attach_free_block(info, blk);

	return size;
}

int rh_get_stats(rh_info_t * info, int what, int max_stats, rh_stats_t * stats)
{
	rh_block_t *blk;
	struct list_head *l;
	struct list_head *h;
	int nr;

	switch (what) {

	case RHGS_FREE:
		h = &info->free_list;
		break;

	case RHGS_TAKEN:
		h = &info->taken_list;
		break;

	default:
		return -EINVAL;
	}

	/* Linear search for block */
	nr = 0;
	list_for_each(l, h) {
		blk = list_entry(l, rh_block_t, list);
		if (stats != NULL && nr < max_stats) {
			stats->start = blk->start;
			stats->size = blk->size;
			stats->owner = blk->owner;
			stats++;
		}
		nr++;
	}

	return nr;
}

int rh_set_owner(rh_info_t * info, void *start, const char *owner)
{
	rh_block_t *blk, *blk2;
	struct list_head *l;
	int size;

	/* Linear search for block */
	blk = NULL;
	list_for_each(l, &info->taken_list) {
		blk2 = list_entry(l, rh_block_t, list);
		if (start < blk2->start)
			break;
		blk = blk2;
	}

	if (blk == NULL || start > (blk->start + blk->size))
		return -EINVAL;

	blk->owner = owner;
	size = blk->size;

	return size;
}

void rh_dump(rh_info_t * info)
{
	static rh_stats_t st[32];	/* XXX maximum 32 blocks */
	int maxnr;
	int i, nr;

	maxnr = sizeof(st) / sizeof(st[0]);

	printk(KERN_INFO
	       "info @0x%p (%d slots empty / %d max)\n",
	       info, info->empty_slots, info->max_blocks);

	printk(KERN_INFO "  Free:\n");
	nr = rh_get_stats(info, RHGS_FREE, maxnr, st);
	if (nr > maxnr)
		nr = maxnr;
	for (i = 0; i < nr; i++)
		printk(KERN_INFO
		       "    0x%p-0x%p (%u)\n",
		       st[i].start, (int8_t *) st[i].start + st[i].size,
		       st[i].size);
	printk(KERN_INFO "\n");

	printk(KERN_INFO "  Taken:\n");
	nr = rh_get_stats(info, RHGS_TAKEN, maxnr, st);
	if (nr > maxnr)
		nr = maxnr;
	for (i = 0; i < nr; i++)
		printk(KERN_INFO
		       "    0x%p-0x%p (%u) %s\n",
		       st[i].start, (int8_t *) st[i].start + st[i].size,
		       st[i].size, st[i].owner != NULL ? st[i].owner : "");
	printk(KERN_INFO "\n");
}

void rh_dump_blk(rh_info_t * info, rh_block_t * blk)
{
	printk(KERN_INFO
	       "blk @0x%p: 0x%p-0x%p (%u)\n",
	       blk, blk->start, (int8_t *) blk->start + blk->size, blk->size);
}
