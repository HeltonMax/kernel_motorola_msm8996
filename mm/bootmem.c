/*
 *  bootmem - A boot-time physical memory allocator and configurator
 *
 *  Copyright (C) 1999 Ingo Molnar
 *                1999 Kanoj Sarcar, SGI
 *                2008 Johannes Weiner
 *
 * Access to this subsystem has to be serialized externally (which is true
 * for the boot process anyway).
 */
#include <linux/init.h>
#include <linux/pfn.h>
#include <linux/bootmem.h>
#include <linux/module.h>

#include <asm/bug.h>
#include <asm/io.h>
#include <asm/processor.h>

#include "internal.h"

unsigned long max_low_pfn;
unsigned long min_low_pfn;
unsigned long max_pfn;

#ifdef CONFIG_CRASH_DUMP
/*
 * If we have booted due to a crash, max_pfn will be a very low value. We need
 * to know the amount of memory that the previous kernel used.
 */
unsigned long saved_max_pfn;
#endif

bootmem_data_t bootmem_node_data[MAX_NUMNODES] __initdata;

static struct list_head bdata_list __initdata = LIST_HEAD_INIT(bdata_list);

static int bootmem_debug;

static int __init bootmem_debug_setup(char *buf)
{
	bootmem_debug = 1;
	return 0;
}
early_param("bootmem_debug", bootmem_debug_setup);

#define bdebug(fmt, args...) ({				\
	if (unlikely(bootmem_debug))			\
		printk(KERN_INFO			\
			"bootmem::%s " fmt,		\
			__FUNCTION__, ## args);		\
})

static unsigned long __init bootmap_bytes(unsigned long pages)
{
	unsigned long bytes = (pages + 7) / 8;

	return ALIGN(bytes, sizeof(long));
}

/**
 * bootmem_bootmap_pages - calculate bitmap size in pages
 * @pages: number of pages the bitmap has to represent
 */
unsigned long __init bootmem_bootmap_pages(unsigned long pages)
{
	unsigned long bytes = bootmap_bytes(pages);

	return PAGE_ALIGN(bytes) >> PAGE_SHIFT;
}

/*
 * link bdata in order
 */
static void __init link_bootmem(bootmem_data_t *bdata)
{
	struct list_head *iter;

	list_for_each(iter, &bdata_list) {
		bootmem_data_t *ent;

		ent = list_entry(iter, bootmem_data_t, list);
		if (bdata->node_boot_start < ent->node_boot_start)
			break;
	}
	list_add_tail(&bdata->list, iter);
}

/*
 * Called once to set up the allocator itself.
 */
static unsigned long __init init_bootmem_core(bootmem_data_t *bdata,
	unsigned long mapstart, unsigned long start, unsigned long end)
{
	unsigned long mapsize;

	mminit_validate_memmodel_limits(&start, &end);
	bdata->node_bootmem_map = phys_to_virt(PFN_PHYS(mapstart));
	bdata->node_boot_start = PFN_PHYS(start);
	bdata->node_low_pfn = end;
	link_bootmem(bdata);

	/*
	 * Initially all pages are reserved - setup_arch() has to
	 * register free RAM areas explicitly.
	 */
	mapsize = bootmap_bytes(end - start);
	memset(bdata->node_bootmem_map, 0xff, mapsize);

	bdebug("nid=%td start=%lx map=%lx end=%lx mapsize=%lx\n",
		bdata - bootmem_node_data, start, mapstart, end, mapsize);

	return mapsize;
}

/**
 * init_bootmem_node - register a node as boot memory
 * @pgdat: node to register
 * @freepfn: pfn where the bitmap for this node is to be placed
 * @startpfn: first pfn on the node
 * @endpfn: first pfn after the node
 *
 * Returns the number of bytes needed to hold the bitmap for this node.
 */
unsigned long __init init_bootmem_node(pg_data_t *pgdat, unsigned long freepfn,
				unsigned long startpfn, unsigned long endpfn)
{
	return init_bootmem_core(pgdat->bdata, freepfn, startpfn, endpfn);
}

/**
 * init_bootmem - register boot memory
 * @start: pfn where the bitmap is to be placed
 * @pages: number of available physical pages
 *
 * Returns the number of bytes needed to hold the bitmap.
 */
unsigned long __init init_bootmem(unsigned long start, unsigned long pages)
{
	max_low_pfn = pages;
	min_low_pfn = start;
	return init_bootmem_core(NODE_DATA(0)->bdata, start, 0, pages);
}

static unsigned long __init free_all_bootmem_core(bootmem_data_t *bdata)
{
	int aligned;
	struct page *page;
	unsigned long start, end, pages, count = 0;

	if (!bdata->node_bootmem_map)
		return 0;

	start = PFN_DOWN(bdata->node_boot_start);
	end = bdata->node_low_pfn;

	/*
	 * If the start is aligned to the machines wordsize, we might
	 * be able to free pages in bulks of that order.
	 */
	aligned = !(start & (BITS_PER_LONG - 1));

	bdebug("nid=%td start=%lx end=%lx aligned=%d\n",
		bdata - bootmem_node_data, start, end, aligned);

	while (start < end) {
		unsigned long *map, idx, vec;

		map = bdata->node_bootmem_map;
		idx = start - PFN_DOWN(bdata->node_boot_start);
		vec = ~map[idx / BITS_PER_LONG];

		if (aligned && vec == ~0UL && start + BITS_PER_LONG < end) {
			int order = ilog2(BITS_PER_LONG);

			__free_pages_bootmem(pfn_to_page(start), order);
			count += BITS_PER_LONG;
		} else {
			unsigned long off = 0;

			while (vec && off < BITS_PER_LONG) {
				if (vec & 1) {
					page = pfn_to_page(start + off);
					__free_pages_bootmem(page, 0);
					count++;
				}
				vec >>= 1;
				off++;
			}
		}
		start += BITS_PER_LONG;
	}

	page = virt_to_page(bdata->node_bootmem_map);
	pages = bdata->node_low_pfn - PFN_DOWN(bdata->node_boot_start);
	pages = bootmem_bootmap_pages(pages);
	count += pages;
	while (pages--)
		__free_pages_bootmem(page++, 0);

	bdebug("nid=%td released=%lx\n", bdata - bootmem_node_data, count);

	return count;
}

/**
 * free_all_bootmem_node - release a node's free pages to the buddy allocator
 * @pgdat: node to be released
 *
 * Returns the number of pages actually released.
 */
unsigned long __init free_all_bootmem_node(pg_data_t *pgdat)
{
	register_page_bootmem_info_node(pgdat);
	return free_all_bootmem_core(pgdat->bdata);
}

/**
 * free_all_bootmem - release free pages to the buddy allocator
 *
 * Returns the number of pages actually released.
 */
unsigned long __init free_all_bootmem(void)
{
	return free_all_bootmem_core(NODE_DATA(0)->bdata);
}

static void __init __free(bootmem_data_t *bdata,
			unsigned long sidx, unsigned long eidx)
{
	unsigned long idx;

	bdebug("nid=%td start=%lx end=%lx\n", bdata - bootmem_node_data,
		sidx + PFN_DOWN(bdata->node_boot_start),
		eidx + PFN_DOWN(bdata->node_boot_start));

	for (idx = sidx; idx < eidx; idx++)
		if (!test_and_clear_bit(idx, bdata->node_bootmem_map))
			BUG();
}

static int __init __reserve(bootmem_data_t *bdata, unsigned long sidx,
			unsigned long eidx, int flags)
{
	unsigned long idx;
	int exclusive = flags & BOOTMEM_EXCLUSIVE;

	bdebug("nid=%td start=%lx end=%lx flags=%x\n",
		bdata - bootmem_node_data,
		sidx + PFN_DOWN(bdata->node_boot_start),
		eidx + PFN_DOWN(bdata->node_boot_start),
		flags);

	for (idx = sidx; idx < eidx; idx++)
		if (test_and_set_bit(idx, bdata->node_bootmem_map)) {
			if (exclusive) {
				__free(bdata, sidx, idx);
				return -EBUSY;
			}
			bdebug("silent double reserve of PFN %lx\n",
				idx + PFN_DOWN(bdata->node_boot_start));
		}
	return 0;
}

static void __init free_bootmem_core(bootmem_data_t *bdata, unsigned long addr,
				     unsigned long size)
{
	unsigned long sidx, eidx;
	unsigned long i;

	BUG_ON(!size);

	/* out range */
	if (addr + size < bdata->node_boot_start ||
		PFN_DOWN(addr) > bdata->node_low_pfn)
		return;
	/*
	 * round down end of usable mem, partially free pages are
	 * considered reserved.
	 */

	if (addr >= bdata->node_boot_start &&
			PFN_DOWN(addr - bdata->node_boot_start) < bdata->hint_idx)
		bdata->hint_idx = PFN_DOWN(addr - bdata->node_boot_start);

	/*
	 * Round up to index to the range.
	 */
	if (PFN_UP(addr) > PFN_DOWN(bdata->node_boot_start))
		sidx = PFN_UP(addr) - PFN_DOWN(bdata->node_boot_start);
	else
		sidx = 0;

	eidx = PFN_DOWN(addr + size - bdata->node_boot_start);
	if (eidx > bdata->node_low_pfn - PFN_DOWN(bdata->node_boot_start))
		eidx = bdata->node_low_pfn - PFN_DOWN(bdata->node_boot_start);

	__free(bdata, sidx, eidx);
}

/**
 * free_bootmem_node - mark a page range as usable
 * @pgdat: node the range resides on
 * @physaddr: starting address of the range
 * @size: size of the range in bytes
 *
 * Partial pages will be considered reserved and left as they are.
 *
 * Only physical pages that actually reside on @pgdat are marked.
 */
void __init free_bootmem_node(pg_data_t *pgdat, unsigned long physaddr,
			      unsigned long size)
{
	free_bootmem_core(pgdat->bdata, physaddr, size);
}

/**
 * free_bootmem - mark a page range as usable
 * @addr: starting address of the range
 * @size: size of the range in bytes
 *
 * Partial pages will be considered reserved and left as they are.
 *
 * All physical pages within the range are marked, no matter what
 * node they reside on.
 */
void __init free_bootmem(unsigned long addr, unsigned long size)
{
	bootmem_data_t *bdata;
	list_for_each_entry(bdata, &bdata_list, list)
		free_bootmem_core(bdata, addr, size);
}

/*
 * Marks a particular physical memory range as unallocatable. Usable RAM
 * might be used for boot-time allocations - or it might get added
 * to the free page pool later on.
 */
static int __init can_reserve_bootmem_core(bootmem_data_t *bdata,
			unsigned long addr, unsigned long size, int flags)
{
	unsigned long sidx, eidx;
	unsigned long i;

	BUG_ON(!size);

	/* out of range, don't hold other */
	if (addr + size < bdata->node_boot_start ||
		PFN_DOWN(addr) > bdata->node_low_pfn)
		return 0;

	/*
	 * Round up to index to the range.
	 */
	if (addr > bdata->node_boot_start)
		sidx= PFN_DOWN(addr - bdata->node_boot_start);
	else
		sidx = 0;

	eidx = PFN_UP(addr + size - bdata->node_boot_start);
	if (eidx > bdata->node_low_pfn - PFN_DOWN(bdata->node_boot_start))
		eidx = bdata->node_low_pfn - PFN_DOWN(bdata->node_boot_start);

	for (i = sidx; i < eidx; i++) {
		if (test_bit(i, bdata->node_bootmem_map)) {
			if (flags & BOOTMEM_EXCLUSIVE)
				return -EBUSY;
		}
	}

	return 0;

}

static void __init reserve_bootmem_core(bootmem_data_t *bdata,
			unsigned long addr, unsigned long size, int flags)
{
	unsigned long sidx, eidx;
	unsigned long i;

	BUG_ON(!size);

	/* out of range */
	if (addr + size < bdata->node_boot_start ||
		PFN_DOWN(addr) > bdata->node_low_pfn)
		return;

	/*
	 * Round up to index to the range.
	 */
	if (addr > bdata->node_boot_start)
		sidx= PFN_DOWN(addr - bdata->node_boot_start);
	else
		sidx = 0;

	eidx = PFN_UP(addr + size - bdata->node_boot_start);
	if (eidx > bdata->node_low_pfn - PFN_DOWN(bdata->node_boot_start))
		eidx = bdata->node_low_pfn - PFN_DOWN(bdata->node_boot_start);

	return __reserve(bdata, sidx, eidx, flags);
}

/**
 * reserve_bootmem_node - mark a page range as reserved
 * @pgdat: node the range resides on
 * @physaddr: starting address of the range
 * @size: size of the range in bytes
 * @flags: reservation flags (see linux/bootmem.h)
 *
 * Partial pages will be reserved.
 *
 * Only physical pages that actually reside on @pgdat are marked.
 */
int __init reserve_bootmem_node(pg_data_t *pgdat, unsigned long physaddr,
				 unsigned long size, int flags)
{
	int ret;

	ret = can_reserve_bootmem_core(pgdat->bdata, physaddr, size, flags);
	if (ret < 0)
		return -ENOMEM;
	reserve_bootmem_core(pgdat->bdata, physaddr, size, flags);
	return 0;
}

#ifndef CONFIG_HAVE_ARCH_BOOTMEM_NODE
/**
 * reserve_bootmem - mark a page range as usable
 * @addr: starting address of the range
 * @size: size of the range in bytes
 * @flags: reservation flags (see linux/bootmem.h)
 *
 * Partial pages will be reserved.
 *
 * All physical pages within the range are marked, no matter what
 * node they reside on.
 */
int __init reserve_bootmem(unsigned long addr, unsigned long size,
			    int flags)
{
	bootmem_data_t *bdata;
	int ret;

	list_for_each_entry(bdata, &bdata_list, list) {
		ret = can_reserve_bootmem_core(bdata, addr, size, flags);
		if (ret < 0)
			return ret;
	}
	list_for_each_entry(bdata, &bdata_list, list)
		reserve_bootmem_core(bdata, addr, size, flags);

	return 0;
}
#endif /* !CONFIG_HAVE_ARCH_BOOTMEM_NODE */

static void * __init alloc_bootmem_core(struct bootmem_data *bdata,
				unsigned long size, unsigned long align,
				unsigned long goal, unsigned long limit)
{
	unsigned long min, max, start, sidx, midx, step;

	BUG_ON(!size);
	BUG_ON(align & (align - 1));
	BUG_ON(limit && goal + size > limit);

	if (!bdata->node_bootmem_map)
		return NULL;

	bdebug("nid=%td size=%lx [%lu pages] align=%lx goal=%lx limit=%lx\n",
		bdata - bootmem_node_data, size, PAGE_ALIGN(size) >> PAGE_SHIFT,
		align, goal, limit);

	min = PFN_DOWN(bdata->node_boot_start);
	max = bdata->node_low_pfn;

	goal >>= PAGE_SHIFT;
	limit >>= PAGE_SHIFT;

	if (limit && max > limit)
		max = limit;
	if (max <= min)
		return NULL;

	step = max(align >> PAGE_SHIFT, 1UL);

	if (goal && min < goal && goal < max)
		start = ALIGN(goal, step);
	else
		start = ALIGN(min, step);

	sidx = start - PFN_DOWN(bdata->node_boot_start);
	midx = max - PFN_DOWN(bdata->node_boot_start);

	if (bdata->hint_idx > sidx) {
		/* Make sure we retry on failure */
		goal = 1;
		sidx = ALIGN(bdata->hint_idx, step);
	}

	while (1) {
		int merge;
		void *region;
		unsigned long eidx, i, start_off, end_off;
find_block:
		sidx = find_next_zero_bit(bdata->node_bootmem_map, midx, sidx);
		sidx = ALIGN(sidx, step);
		eidx = sidx + PFN_UP(size);

		if (sidx >= midx || eidx > midx)
			break;

		for (i = sidx; i < eidx; i++)
			if (test_bit(i, bdata->node_bootmem_map)) {
				sidx = ALIGN(i, step);
				if (sidx == i)
					sidx += step;
				goto find_block;
			}

		if (bdata->last_end_off &&
				PFN_DOWN(bdata->last_end_off) + 1 == sidx)
			start_off = ALIGN(bdata->last_end_off, align);
		else
			start_off = PFN_PHYS(sidx);

		merge = PFN_DOWN(start_off) < sidx;
		end_off = start_off + size;

		bdata->last_end_off = end_off;
		bdata->hint_idx = PFN_UP(end_off);

		/*
		 * Reserve the area now:
		 */
		if (__reserve(bdata, PFN_DOWN(start_off) + merge,
				PFN_UP(end_off), BOOTMEM_EXCLUSIVE))
			BUG();

		region = phys_to_virt(bdata->node_boot_start + start_off);
		memset(region, 0, size);
		return region;
	}

	if (goal) {
		goal = 0;
		sidx = 0;
		goto find_block;
	}

	return NULL;
}

/**
 * __alloc_bootmem_nopanic - allocate boot memory without panicking
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * Returns NULL on failure.
 */
void * __init __alloc_bootmem_nopanic(unsigned long size, unsigned long align,
				      unsigned long goal)
{
	bootmem_data_t *bdata;
	void *ptr;

	list_for_each_entry(bdata, &bdata_list, list) {
		ptr = alloc_bootmem_core(bdata, size, align, goal, 0);
		if (ptr)
			return ptr;
	}
	return NULL;
}

/**
 * __alloc_bootmem - allocate boot memory
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem(unsigned long size, unsigned long align,
			      unsigned long goal)
{
	void *mem = __alloc_bootmem_nopanic(size,align,goal);

	if (mem)
		return mem;
	/*
	 * Whoops, we cannot satisfy the allocation request.
	 */
	printk(KERN_ALERT "bootmem alloc of %lu bytes failed!\n", size);
	panic("Out of memory");
	return NULL;
}

/**
 * __alloc_bootmem_node - allocate boot memory from a specific node
 * @pgdat: node to allocate from
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may fall back to any node in the system if the specified node
 * can not hold the requested memory.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem_node(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	void *ptr;

	ptr = alloc_bootmem_core(pgdat->bdata, size, align, goal, 0);
	if (ptr)
		return ptr;

	return __alloc_bootmem(size, align, goal);
}

#ifdef CONFIG_SPARSEMEM
/**
 * alloc_bootmem_section - allocate boot memory from a specific section
 * @size: size of the request in bytes
 * @section_nr: sparse map section to allocate from
 *
 * Return NULL on failure.
 */
void * __init alloc_bootmem_section(unsigned long size,
				    unsigned long section_nr)
{
	void *ptr;
	unsigned long limit, goal, start_nr, end_nr, pfn;
	struct pglist_data *pgdat;

	pfn = section_nr_to_pfn(section_nr);
	goal = PFN_PHYS(pfn);
	limit = PFN_PHYS(section_nr_to_pfn(section_nr + 1)) - 1;
	pgdat = NODE_DATA(early_pfn_to_nid(pfn));
	ptr = alloc_bootmem_core(pgdat->bdata, size, SMP_CACHE_BYTES, goal,
				limit);

	if (!ptr)
		return NULL;

	start_nr = pfn_to_section_nr(PFN_DOWN(__pa(ptr)));
	end_nr = pfn_to_section_nr(PFN_DOWN(__pa(ptr) + size));
	if (start_nr != section_nr || end_nr != section_nr) {
		printk(KERN_WARNING "alloc_bootmem failed on section %ld.\n",
		       section_nr);
		free_bootmem_core(pgdat->bdata, __pa(ptr), size);
		ptr = NULL;
	}

	return ptr;
}
#endif

void * __init __alloc_bootmem_node_nopanic(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	void *ptr;

	ptr = alloc_bootmem_core(pgdat->bdata, size, align, goal, 0);
	if (ptr)
		return ptr;

	return __alloc_bootmem_nopanic(size, align, goal);
}

#ifndef ARCH_LOW_ADDRESS_LIMIT
#define ARCH_LOW_ADDRESS_LIMIT	0xffffffffUL
#endif

/**
 * __alloc_bootmem_low - allocate low boot memory
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem_low(unsigned long size, unsigned long align,
				  unsigned long goal)
{
	bootmem_data_t *bdata;
	void *ptr;

	list_for_each_entry(bdata, &bdata_list, list) {
		ptr = alloc_bootmem_core(bdata, size, align, goal,
					ARCH_LOW_ADDRESS_LIMIT);
		if (ptr)
			return ptr;
	}

	/*
	 * Whoops, we cannot satisfy the allocation request.
	 */
	printk(KERN_ALERT "low bootmem alloc of %lu bytes failed!\n", size);
	panic("Out of low memory");
	return NULL;
}

/**
 * __alloc_bootmem_low_node - allocate low boot memory from a specific node
 * @pgdat: node to allocate from
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may fall back to any node in the system if the specified node
 * can not hold the requested memory.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem_low_node(pg_data_t *pgdat, unsigned long size,
				       unsigned long align, unsigned long goal)
{
	return alloc_bootmem_core(pgdat->bdata, size, align, goal,
				ARCH_LOW_ADDRESS_LIMIT);
}
