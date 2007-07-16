/* mdesc.c: Sun4V machine description handling.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/log2.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/hypervisor.h>
#include <asm/mdesc.h>
#include <asm/prom.h>
#include <asm/oplib.h>
#include <asm/smp.h>

/* Unlike the OBP device tree, the machine description is a full-on
 * DAG.  An arbitrary number of ARCs are possible from one
 * node to other nodes and thus we can't use the OBP device_node
 * data structure to represent these nodes inside of the kernel.
 *
 * Actually, it isn't even a DAG, because there are back pointers
 * which create cycles in the graph.
 *
 * mdesc_hdr and mdesc_elem describe the layout of the data structure
 * we get from the Hypervisor.
 */
struct mdesc_hdr {
	u32	version; /* Transport version */
	u32	node_sz; /* node block size */
	u32	name_sz; /* name block size */
	u32	data_sz; /* data block size */
} __attribute__((aligned(16)));

struct mdesc_elem {
	u8	tag;
#define MD_LIST_END	0x00
#define MD_NODE		0x4e
#define MD_NODE_END	0x45
#define MD_NOOP		0x20
#define MD_PROP_ARC	0x61
#define MD_PROP_VAL	0x76
#define MD_PROP_STR	0x73
#define MD_PROP_DATA	0x64
	u8	name_len;
	u16	resv;
	u32	name_offset;
	union {
		struct {
			u32	data_len;
			u32	data_offset;
		} data;
		u64	val;
	} d;
};

struct mdesc_mem_ops {
	struct mdesc_handle *(*alloc)(unsigned int mdesc_size);
	void (*free)(struct mdesc_handle *handle);
};

struct mdesc_handle {
	struct list_head	list;
	struct mdesc_mem_ops	*mops;
	void			*self_base;
	atomic_t		refcnt;
	unsigned int		handle_size;
	struct mdesc_hdr	mdesc;
};

static void mdesc_handle_init(struct mdesc_handle *hp,
			      unsigned int handle_size,
			      void *base)
{
	BUG_ON(((unsigned long)&hp->mdesc) & (16UL - 1));

	memset(hp, 0, handle_size);
	INIT_LIST_HEAD(&hp->list);
	hp->self_base = base;
	atomic_set(&hp->refcnt, 1);
	hp->handle_size = handle_size;
}

static struct mdesc_handle *mdesc_bootmem_alloc(unsigned int mdesc_size)
{
	struct mdesc_handle *hp;
	unsigned int handle_size, alloc_size;

	handle_size = (sizeof(struct mdesc_handle) -
		       sizeof(struct mdesc_hdr) +
		       mdesc_size);
	alloc_size = PAGE_ALIGN(handle_size);

	hp = __alloc_bootmem(alloc_size, PAGE_SIZE, 0UL);
	if (hp)
		mdesc_handle_init(hp, handle_size, hp);

	return hp;
}

static void mdesc_bootmem_free(struct mdesc_handle *hp)
{
	unsigned int alloc_size, handle_size = hp->handle_size;
	unsigned long start, end;

	BUG_ON(atomic_read(&hp->refcnt) != 0);
	BUG_ON(!list_empty(&hp->list));

	alloc_size = PAGE_ALIGN(handle_size);

	start = (unsigned long) hp;
	end = start + alloc_size;

	while (start < end) {
		struct page *p;

		p = virt_to_page(start);
		ClearPageReserved(p);
		__free_page(p);
		start += PAGE_SIZE;
	}
}

static struct mdesc_mem_ops bootmem_mdesc_memops = {
	.alloc = mdesc_bootmem_alloc,
	.free  = mdesc_bootmem_free,
};

static struct mdesc_handle *mdesc_kmalloc(unsigned int mdesc_size)
{
	unsigned int handle_size;
	void *base;

	handle_size = (sizeof(struct mdesc_handle) -
		       sizeof(struct mdesc_hdr) +
		       mdesc_size);

	base = kmalloc(handle_size + 15, GFP_KERNEL);
	if (base) {
		struct mdesc_handle *hp;
		unsigned long addr;

		addr = (unsigned long)base;
		addr = (addr + 15UL) & ~15UL;
		hp = (struct mdesc_handle *) addr;

		mdesc_handle_init(hp, handle_size, base);
		return hp;
	}

	return NULL;
}

static void mdesc_kfree(struct mdesc_handle *hp)
{
	BUG_ON(atomic_read(&hp->refcnt) != 0);
	BUG_ON(!list_empty(&hp->list));

	kfree(hp->self_base);
}

static struct mdesc_mem_ops kmalloc_mdesc_memops = {
	.alloc = mdesc_kmalloc,
	.free  = mdesc_kfree,
};

static struct mdesc_handle *mdesc_alloc(unsigned int mdesc_size,
					struct mdesc_mem_ops *mops)
{
	struct mdesc_handle *hp = mops->alloc(mdesc_size);

	if (hp)
		hp->mops = mops;

	return hp;
}

static void mdesc_free(struct mdesc_handle *hp)
{
	hp->mops->free(hp);
}

static struct mdesc_handle *cur_mdesc;
static LIST_HEAD(mdesc_zombie_list);
static DEFINE_SPINLOCK(mdesc_lock);

struct mdesc_handle *mdesc_grab(void)
{
	struct mdesc_handle *hp;
	unsigned long flags;

	spin_lock_irqsave(&mdesc_lock, flags);
	hp = cur_mdesc;
	if (hp)
		atomic_inc(&hp->refcnt);
	spin_unlock_irqrestore(&mdesc_lock, flags);

	return hp;
}
EXPORT_SYMBOL(mdesc_grab);

void mdesc_release(struct mdesc_handle *hp)
{
	unsigned long flags;

	spin_lock_irqsave(&mdesc_lock, flags);
	if (atomic_dec_and_test(&hp->refcnt)) {
		list_del_init(&hp->list);
		hp->mops->free(hp);
	}
	spin_unlock_irqrestore(&mdesc_lock, flags);
}
EXPORT_SYMBOL(mdesc_release);

void mdesc_update(void)
{
	unsigned long len, real_len, status;
	struct mdesc_handle *hp, *orig_hp;
	unsigned long flags;

	(void) sun4v_mach_desc(0UL, 0UL, &len);

	hp = mdesc_alloc(len, &kmalloc_mdesc_memops);
	if (!hp) {
		printk(KERN_ERR "MD: mdesc alloc fails\n");
		return;
	}

	status = sun4v_mach_desc(__pa(&hp->mdesc), len, &real_len);
	if (status != HV_EOK || real_len > len) {
		printk(KERN_ERR "MD: mdesc reread fails with %lu\n",
		       status);
		atomic_dec(&hp->refcnt);
		mdesc_free(hp);
		return;
	}

	spin_lock_irqsave(&mdesc_lock, flags);
	orig_hp = cur_mdesc;
	cur_mdesc = hp;

	if (atomic_dec_and_test(&orig_hp->refcnt))
		mdesc_free(orig_hp);
	else
		list_add(&orig_hp->list, &mdesc_zombie_list);
	spin_unlock_irqrestore(&mdesc_lock, flags);
}

static struct mdesc_elem *node_block(struct mdesc_hdr *mdesc)
{
	return (struct mdesc_elem *) (mdesc + 1);
}

static void *name_block(struct mdesc_hdr *mdesc)
{
	return ((void *) node_block(mdesc)) + mdesc->node_sz;
}

static void *data_block(struct mdesc_hdr *mdesc)
{
	return ((void *) name_block(mdesc)) + mdesc->name_sz;
}

u64 mdesc_node_by_name(struct mdesc_handle *hp,
		       u64 from_node, const char *name)
{
	struct mdesc_elem *ep = node_block(&hp->mdesc);
	const char *names = name_block(&hp->mdesc);
	u64 last_node = hp->mdesc.node_sz / 16;
	u64 ret;

	if (from_node == MDESC_NODE_NULL) {
		ret = from_node = 0;
	} else if (from_node >= last_node) {
		return MDESC_NODE_NULL;
	} else {
		ret = ep[from_node].d.val;
	}

	while (ret < last_node) {
		if (ep[ret].tag != MD_NODE)
			return MDESC_NODE_NULL;
		if (!strcmp(names + ep[ret].name_offset, name))
			break;
		ret = ep[ret].d.val;
	}
	if (ret >= last_node)
		ret = MDESC_NODE_NULL;
	return ret;
}
EXPORT_SYMBOL(mdesc_node_by_name);

const void *mdesc_get_property(struct mdesc_handle *hp, u64 node,
			       const char *name, int *lenp)
{
	const char *names = name_block(&hp->mdesc);
	u64 last_node = hp->mdesc.node_sz / 16;
	void *data = data_block(&hp->mdesc);
	struct mdesc_elem *ep;

	if (node == MDESC_NODE_NULL || node >= last_node)
		return NULL;

	ep = node_block(&hp->mdesc) + node;
	ep++;
	for (; ep->tag != MD_NODE_END; ep++) {
		void *val = NULL;
		int len = 0;

		switch (ep->tag) {
		case MD_PROP_VAL:
			val = &ep->d.val;
			len = 8;
			break;

		case MD_PROP_STR:
		case MD_PROP_DATA:
			val = data + ep->d.data.data_offset;
			len = ep->d.data.data_len;
			break;

		default:
			break;
		}
		if (!val)
			continue;

		if (!strcmp(names + ep->name_offset, name)) {
			if (lenp)
				*lenp = len;
			return val;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(mdesc_get_property);

u64 mdesc_next_arc(struct mdesc_handle *hp, u64 from, const char *arc_type)
{
	struct mdesc_elem *ep, *base = node_block(&hp->mdesc);
	const char *names = name_block(&hp->mdesc);
	u64 last_node = hp->mdesc.node_sz / 16;

	if (from == MDESC_NODE_NULL || from >= last_node)
		return MDESC_NODE_NULL;

	ep = base + from;

	ep++;
	for (; ep->tag != MD_NODE_END; ep++) {
		if (ep->tag != MD_PROP_ARC)
			continue;

		if (strcmp(names + ep->name_offset, arc_type))
			continue;

		return ep - base;
	}

	return MDESC_NODE_NULL;
}
EXPORT_SYMBOL(mdesc_next_arc);

u64 mdesc_arc_target(struct mdesc_handle *hp, u64 arc)
{
	struct mdesc_elem *ep, *base = node_block(&hp->mdesc);

	ep = base + arc;

	return ep->d.val;
}
EXPORT_SYMBOL(mdesc_arc_target);

const char *mdesc_node_name(struct mdesc_handle *hp, u64 node)
{
	struct mdesc_elem *ep, *base = node_block(&hp->mdesc);
	const char *names = name_block(&hp->mdesc);
	u64 last_node = hp->mdesc.node_sz / 16;

	if (node == MDESC_NODE_NULL || node >= last_node)
		return NULL;

	ep = base + node;
	if (ep->tag != MD_NODE)
		return NULL;

	return names + ep->name_offset;
}
EXPORT_SYMBOL(mdesc_node_name);

static void __init report_platform_properties(void)
{
	struct mdesc_handle *hp = mdesc_grab();
	u64 pn = mdesc_node_by_name(hp, MDESC_NODE_NULL, "platform");
	const char *s;
	const u64 *v;

	if (pn == MDESC_NODE_NULL) {
		prom_printf("No platform node in machine-description.\n");
		prom_halt();
	}

	s = mdesc_get_property(hp, pn, "banner-name", NULL);
	printk("PLATFORM: banner-name [%s]\n", s);
	s = mdesc_get_property(hp, pn, "name", NULL);
	printk("PLATFORM: name [%s]\n", s);

	v = mdesc_get_property(hp, pn, "hostid", NULL);
	if (v)
		printk("PLATFORM: hostid [%08lx]\n", *v);
	v = mdesc_get_property(hp, pn, "serial#", NULL);
	if (v)
		printk("PLATFORM: serial# [%08lx]\n", *v);
	v = mdesc_get_property(hp, pn, "stick-frequency", NULL);
	printk("PLATFORM: stick-frequency [%08lx]\n", *v);
	v = mdesc_get_property(hp, pn, "mac-address", NULL);
	if (v)
		printk("PLATFORM: mac-address [%lx]\n", *v);
	v = mdesc_get_property(hp, pn, "watchdog-resolution", NULL);
	if (v)
		printk("PLATFORM: watchdog-resolution [%lu ms]\n", *v);
	v = mdesc_get_property(hp, pn, "watchdog-max-timeout", NULL);
	if (v)
		printk("PLATFORM: watchdog-max-timeout [%lu ms]\n", *v);
	v = mdesc_get_property(hp, pn, "max-cpus", NULL);
	if (v)
		printk("PLATFORM: max-cpus [%lu]\n", *v);

#ifdef CONFIG_SMP
	{
		int max_cpu, i;

		if (v) {
			max_cpu = *v;
			if (max_cpu > NR_CPUS)
				max_cpu = NR_CPUS;
		} else {
			max_cpu = NR_CPUS;
		}
		for (i = 0; i < max_cpu; i++)
			cpu_set(i, cpu_possible_map);
	}
#endif

	mdesc_release(hp);
}

static int inline find_in_proplist(const char *list, const char *match, int len)
{
	while (len > 0) {
		int l;

		if (!strcmp(list, match))
			return 1;
		l = strlen(list) + 1;
		list += l;
		len -= l;
	}
	return 0;
}

static void __devinit fill_in_one_cache(cpuinfo_sparc *c,
					struct mdesc_handle *hp,
					u64 mp)
{
	const u64 *level = mdesc_get_property(hp, mp, "level", NULL);
	const u64 *size = mdesc_get_property(hp, mp, "size", NULL);
	const u64 *line_size = mdesc_get_property(hp, mp, "line-size", NULL);
	const char *type;
	int type_len;

	type = mdesc_get_property(hp, mp, "type", &type_len);

	switch (*level) {
	case 1:
		if (find_in_proplist(type, "instn", type_len)) {
			c->icache_size = *size;
			c->icache_line_size = *line_size;
		} else if (find_in_proplist(type, "data", type_len)) {
			c->dcache_size = *size;
			c->dcache_line_size = *line_size;
		}
		break;

	case 2:
		c->ecache_size = *size;
		c->ecache_line_size = *line_size;
		break;

	default:
		break;
	}

	if (*level == 1) {
		u64 a;

		mdesc_for_each_arc(a, hp, mp, MDESC_ARC_TYPE_FWD) {
			u64 target = mdesc_arc_target(hp, a);
			const char *name = mdesc_node_name(hp, target);

			if (!strcmp(name, "cache"))
				fill_in_one_cache(c, hp, target);
		}
	}
}

static void __devinit mark_core_ids(struct mdesc_handle *hp, u64 mp,
				    int core_id)
{
	u64 a;

	mdesc_for_each_arc(a, hp, mp, MDESC_ARC_TYPE_BACK) {
		u64 t = mdesc_arc_target(hp, a);
		const char *name;
		const u64 *id;

		name = mdesc_node_name(hp, t);
		if (!strcmp(name, "cpu")) {
			id = mdesc_get_property(hp, t, "id", NULL);
			if (*id < NR_CPUS)
				cpu_data(*id).core_id = core_id;
		} else {
			u64 j;

			mdesc_for_each_arc(j, hp, t, MDESC_ARC_TYPE_BACK) {
				u64 n = mdesc_arc_target(hp, j);
				const char *n_name;

				n_name = mdesc_node_name(hp, n);
				if (strcmp(n_name, "cpu"))
					continue;

				id = mdesc_get_property(hp, n, "id", NULL);
				if (*id < NR_CPUS)
					cpu_data(*id).core_id = core_id;
			}
		}
	}
}

static void __devinit set_core_ids(struct mdesc_handle *hp)
{
	int idx;
	u64 mp;

	idx = 1;
	mdesc_for_each_node_by_name(hp, mp, "cache") {
		const u64 *level;
		const char *type;
		int len;

		level = mdesc_get_property(hp, mp, "level", NULL);
		if (*level != 1)
			continue;

		type = mdesc_get_property(hp, mp, "type", &len);
		if (!find_in_proplist(type, "instn", len))
			continue;

		mark_core_ids(hp, mp, idx);

		idx++;
	}
}

static void __devinit mark_proc_ids(struct mdesc_handle *hp, u64 mp,
				    int proc_id)
{
	u64 a;

	mdesc_for_each_arc(a, hp, mp, MDESC_ARC_TYPE_BACK) {
		u64 t = mdesc_arc_target(hp, a);
		const char *name;
		const u64 *id;

		name = mdesc_node_name(hp, t);
		if (strcmp(name, "cpu"))
			continue;

		id = mdesc_get_property(hp, t, "id", NULL);
		if (*id < NR_CPUS)
			cpu_data(*id).proc_id = proc_id;
	}
}

static void __devinit __set_proc_ids(struct mdesc_handle *hp,
				     const char *exec_unit_name)
{
	int idx;
	u64 mp;

	idx = 0;
	mdesc_for_each_node_by_name(hp, mp, exec_unit_name) {
		const char *type;
		int len;

		type = mdesc_get_property(hp, mp, "type", &len);
		if (!find_in_proplist(type, "int", len) &&
		    !find_in_proplist(type, "integer", len))
			continue;

		mark_proc_ids(hp, mp, idx);

		idx++;
	}
}

static void __devinit set_proc_ids(struct mdesc_handle *hp)
{
	__set_proc_ids(hp, "exec_unit");
	__set_proc_ids(hp, "exec-unit");
}

static void __devinit get_one_mondo_bits(const u64 *p, unsigned int *mask,
					 unsigned char def)
{
	u64 val;

	if (!p)
		goto use_default;
	val = *p;

	if (!val || val >= 64)
		goto use_default;

	*mask = ((1U << val) * 64U) - 1U;
	return;

use_default:
	*mask = ((1U << def) * 64U) - 1U;
}

static void __devinit get_mondo_data(struct mdesc_handle *hp, u64 mp,
				     struct trap_per_cpu *tb)
{
	const u64 *val;

	val = mdesc_get_property(hp, mp, "q-cpu-mondo-#bits", NULL);
	get_one_mondo_bits(val, &tb->cpu_mondo_qmask, 7);

	val = mdesc_get_property(hp, mp, "q-dev-mondo-#bits", NULL);
	get_one_mondo_bits(val, &tb->dev_mondo_qmask, 7);

	val = mdesc_get_property(hp, mp, "q-resumable-#bits", NULL);
	get_one_mondo_bits(val, &tb->resum_qmask, 6);

	val = mdesc_get_property(hp, mp, "q-nonresumable-#bits", NULL);
	get_one_mondo_bits(val, &tb->nonresum_qmask, 2);
}

void __devinit mdesc_fill_in_cpu_data(cpumask_t mask)
{
	struct mdesc_handle *hp = mdesc_grab();
	u64 mp;

	ncpus_probed = 0;
	mdesc_for_each_node_by_name(hp, mp, "cpu") {
		const u64 *id = mdesc_get_property(hp, mp, "id", NULL);
		const u64 *cfreq = mdesc_get_property(hp, mp, "clock-frequency", NULL);
		struct trap_per_cpu *tb;
		cpuinfo_sparc *c;
		int cpuid;
		u64 a;

		ncpus_probed++;

		cpuid = *id;

#ifdef CONFIG_SMP
		if (cpuid >= NR_CPUS)
			continue;
		if (!cpu_isset(cpuid, mask))
			continue;
#else
		/* On uniprocessor we only want the values for the
		 * real physical cpu the kernel booted onto, however
		 * cpu_data() only has one entry at index 0.
		 */
		if (cpuid != real_hard_smp_processor_id())
			continue;
		cpuid = 0;
#endif

		c = &cpu_data(cpuid);
		c->clock_tick = *cfreq;

		tb = &trap_block[cpuid];
		get_mondo_data(hp, mp, tb);

		mdesc_for_each_arc(a, hp, mp, MDESC_ARC_TYPE_FWD) {
			u64 j, t = mdesc_arc_target(hp, a);
			const char *t_name;

			t_name = mdesc_node_name(hp, t);
			if (!strcmp(t_name, "cache")) {
				fill_in_one_cache(c, hp, t);
				continue;
			}

			mdesc_for_each_arc(j, hp, t, MDESC_ARC_TYPE_FWD) {
				u64 n = mdesc_arc_target(hp, j);
				const char *n_name;

				n_name = mdesc_node_name(hp, n);
				if (!strcmp(n_name, "cache"))
					fill_in_one_cache(c, hp, n);
			}
		}

#ifdef CONFIG_SMP
		cpu_set(cpuid, cpu_present_map);
#endif

		c->core_id = 0;
		c->proc_id = -1;
	}

#ifdef CONFIG_SMP
	sparc64_multi_core = 1;
#endif

	set_core_ids(hp);
	set_proc_ids(hp);

	smp_fill_in_sib_core_maps();

	mdesc_release(hp);
}

void __init sun4v_mdesc_init(void)
{
	struct mdesc_handle *hp;
	unsigned long len, real_len, status;
	cpumask_t mask;

	(void) sun4v_mach_desc(0UL, 0UL, &len);

	printk("MDESC: Size is %lu bytes.\n", len);

	hp = mdesc_alloc(len, &bootmem_mdesc_memops);
	if (hp == NULL) {
		prom_printf("MDESC: alloc of %lu bytes failed.\n", len);
		prom_halt();
	}

	status = sun4v_mach_desc(__pa(&hp->mdesc), len, &real_len);
	if (status != HV_EOK || real_len > len) {
		prom_printf("sun4v_mach_desc fails, err(%lu), "
			    "len(%lu), real_len(%lu)\n",
			    status, len, real_len);
		mdesc_free(hp);
		prom_halt();
	}

	cur_mdesc = hp;

	report_platform_properties();

	cpus_setall(mask);
	mdesc_fill_in_cpu_data(mask);
}
