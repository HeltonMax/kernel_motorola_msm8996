/*
 * Common definitions accross all variants of ICP and ICS interrupt
 * controllers.
 */

#ifndef _XICS_H
#define _XICS_H

#include <linux/interrupt.h>

#define XICS_IPI		2
#define XICS_IRQ_SPURIOUS	0

/* Want a priority other than 0.  Various HW issues require this. */
#define	DEFAULT_PRIORITY	5

/*
 * Mark IPIs as higher priority so we can take them inside interrupts that
 * arent marked IRQF_DISABLED
 */
#define IPI_PRIORITY		4

/* The least favored priority */
#define LOWEST_PRIORITY		0xFF

/* The number of priorities defined above */
#define MAX_NUM_PRIORITIES	3

/* Native ICP */
extern int icp_native_init(void);

/* PAPR ICP */
extern int icp_hv_init(void);

/* ICP ops */
struct icp_ops {
	unsigned int (*get_irq)(void);
	void (*eoi)(struct irq_data *d);
	void (*set_priority)(unsigned char prio);
	void (*teardown_cpu)(void);
	void (*flush_ipi)(void);
#ifdef CONFIG_SMP
	void (*message_pass)(int target, int msg);
	irq_handler_t ipi_action;
#endif
};

extern const struct icp_ops *icp_ops;

/* Native ICS */
extern int ics_native_init(void);

/* RTAS ICS */
extern int ics_rtas_init(void);

/* ICS instance, hooked up to chip_data of an irq */
struct ics {
	struct list_head link;
	int (*map)(struct ics *ics, unsigned int virq);
	void (*mask_unknown)(struct ics *ics, unsigned long vec);
	long (*get_server)(struct ics *ics, unsigned long vec);
	char data[];
};

/* Commons */
extern unsigned int xics_default_server;
extern unsigned int xics_default_distrib_server;
extern unsigned int xics_interrupt_server_size;
extern struct irq_host *xics_host;

struct xics_cppr {
	unsigned char stack[MAX_NUM_PRIORITIES];
	int index;
};

DECLARE_PER_CPU(struct xics_cppr, xics_cppr);

static inline void xics_push_cppr(unsigned int vec)
{
	struct xics_cppr *os_cppr = &__get_cpu_var(xics_cppr);

	if (WARN_ON(os_cppr->index >= MAX_NUM_PRIORITIES - 1))
		return;

	if (vec == XICS_IPI)
		os_cppr->stack[++os_cppr->index] = IPI_PRIORITY;
	else
		os_cppr->stack[++os_cppr->index] = DEFAULT_PRIORITY;
}

static inline unsigned char xics_pop_cppr(void)
{
	struct xics_cppr *os_cppr = &__get_cpu_var(xics_cppr);

	if (WARN_ON(os_cppr->index < 1))
		return LOWEST_PRIORITY;

	return os_cppr->stack[--os_cppr->index];
}

static inline void xics_set_base_cppr(unsigned char cppr)
{
	struct xics_cppr *os_cppr = &__get_cpu_var(xics_cppr);

	/* we only really want to set the priority when there's
	 * just one cppr value on the stack
	 */
	WARN_ON(os_cppr->index != 0);

	os_cppr->stack[0] = cppr;
}

static inline unsigned char xics_cppr_top(void)
{
	struct xics_cppr *os_cppr = &__get_cpu_var(xics_cppr);
	
	return os_cppr->stack[os_cppr->index];
}

DECLARE_PER_CPU_SHARED_ALIGNED(unsigned long, xics_ipi_message);

extern void xics_init(void);
extern void xics_setup_cpu(void);
extern void xics_update_irq_servers(void);
extern void xics_set_cpu_giq(unsigned int gserver, unsigned int join);
extern void xics_mask_unknown_vec(unsigned int vec);
extern irqreturn_t xics_ipi_dispatch(int cpu);
extern int xics_smp_probe(void);
extern void xics_register_ics(struct ics *ics);
extern void xics_teardown_cpu(void);
extern void xics_kexec_teardown_cpu(int secondary);
extern void xics_migrate_irqs_away(void);
#ifdef CONFIG_SMP
extern int xics_get_irq_server(unsigned int virq, const struct cpumask *cpumask,
			       unsigned int strict_check);
#else
#define xics_get_irq_server(virq, cpumask, strict_check) (xics_default_server)
#endif


#endif /* _XICS_H */
