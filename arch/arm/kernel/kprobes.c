/*
 * arch/arm/kernel/kprobes.c
 *
 * Kprobes on ARM
 *
 * Abhishek Sagar <sagar.abhishek@gmail.com>
 * Copyright (C) 2006, 2007 Motorola Inc.
 *
 * Nicolas Pitre <nico@marvell.com>
 * Copyright (C) 2007 Marvell Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <linux/stringify.h>
#include <asm/traps.h>
#include <asm/cacheflush.h>

#include "kprobes.h"

#define MIN_STACK_SIZE(addr) 				\
	min((unsigned long)MAX_STACK_SIZE,		\
	    (unsigned long)current_thread_info() + THREAD_START_SP - (addr))

#define flush_insns(addr, size)				\
	flush_icache_range((unsigned long)(addr),	\
			   (unsigned long)(addr) +	\
			   (size))

/* Used as a marker in ARM_pc to note when we're in a jprobe. */
#define JPROBE_MAGIC_ADDR		0xffffffff

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);


int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	kprobe_opcode_t insn;
	kprobe_opcode_t tmp_insn[MAX_INSN_SIZE];
	unsigned long addr = (unsigned long)p->addr;
	kprobe_decode_insn_t *decode_insn;
	int is;

	if (in_exception_text(addr))
		return -EINVAL;

#ifdef CONFIG_THUMB2_KERNEL
	addr &= ~1; /* Bit 0 would normally be set to indicate Thumb code */
	insn = ((u16 *)addr)[0];
	if (is_wide_instruction(insn)) {
		insn <<= 16;
		insn |= ((u16 *)addr)[1];
		decode_insn = thumb32_kprobe_decode_insn;
	} else
		decode_insn = thumb16_kprobe_decode_insn;
#else /* !CONFIG_THUMB2_KERNEL */
	if (addr & 0x3)
		return -EINVAL;
	insn = *p->addr;
	decode_insn = arm_kprobe_decode_insn;
#endif

	p->opcode = insn;
	p->ainsn.insn = tmp_insn;

	switch ((*decode_insn)(insn, &p->ainsn)) {
	case INSN_REJECTED:	/* not supported */
		return -EINVAL;

	case INSN_GOOD:		/* instruction uses slot */
		p->ainsn.insn = get_insn_slot();
		if (!p->ainsn.insn)
			return -ENOMEM;
		for (is = 0; is < MAX_INSN_SIZE; ++is)
			p->ainsn.insn[is] = tmp_insn[is];
		flush_insns(p->ainsn.insn,
				sizeof(p->ainsn.insn[0]) * MAX_INSN_SIZE);
		break;

	case INSN_GOOD_NO_SLOT:	/* instruction doesn't need insn slot */
		p->ainsn.insn = NULL;
		break;
	}

	return 0;
}

#ifdef CONFIG_THUMB2_KERNEL

/*
 * For a 32-bit Thumb breakpoint spanning two memory words we need to take
 * special precautions to insert the breakpoint atomically, especially on SMP
 * systems. This is achieved by calling this arming function using stop_machine.
 */
static int __kprobes set_t32_breakpoint(void *addr)
{
	((u16 *)addr)[0] = KPROBE_THUMB32_BREAKPOINT_INSTRUCTION >> 16;
	((u16 *)addr)[1] = KPROBE_THUMB32_BREAKPOINT_INSTRUCTION & 0xffff;
	flush_insns(addr, 2*sizeof(u16));
	return 0;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	uintptr_t addr = (uintptr_t)p->addr & ~1; /* Remove any Thumb flag */

	if (!is_wide_instruction(p->opcode)) {
		*(u16 *)addr = KPROBE_THUMB16_BREAKPOINT_INSTRUCTION;
		flush_insns(addr, sizeof(u16));
	} else if (addr & 2) {
		/* A 32-bit instruction spanning two words needs special care */
		stop_machine(set_t32_breakpoint, (void *)addr, &cpu_online_map);
	} else {
		/* Word aligned 32-bit instruction can be written atomically */
		u32 bkp = KPROBE_THUMB32_BREAKPOINT_INSTRUCTION;
#ifndef __ARMEB__ /* Swap halfwords for little-endian */
		bkp = (bkp >> 16) | (bkp << 16);
#endif
		*(u32 *)addr = bkp;
		flush_insns(addr, sizeof(u32));
	}
}

#else /* !CONFIG_THUMB2_KERNEL */

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	*p->addr = KPROBE_ARM_BREAKPOINT_INSTRUCTION;
	flush_insns(p->addr, sizeof(p->addr[0]));
}

#endif /* !CONFIG_THUMB2_KERNEL */

/*
 * The actual disarming is done here on each CPU and synchronized using
 * stop_machine. This synchronization is necessary on SMP to avoid removing
 * a probe between the moment the 'Undefined Instruction' exception is raised
 * and the moment the exception handler reads the faulting instruction from
 * memory. It is also needed to atomically set the two half-words of a 32-bit
 * Thumb breakpoint.
 */
int __kprobes __arch_disarm_kprobe(void *p)
{
	struct kprobe *kp = p;
#ifdef CONFIG_THUMB2_KERNEL
	u16 *addr = (u16 *)((uintptr_t)kp->addr & ~1);
	kprobe_opcode_t insn = kp->opcode;
	unsigned int len;

	if (is_wide_instruction(insn)) {
		((u16 *)addr)[0] = insn>>16;
		((u16 *)addr)[1] = insn;
		len = 2*sizeof(u16);
	} else {
		((u16 *)addr)[0] = insn;
		len = sizeof(u16);
	}
	flush_insns(addr, len);

#else /* !CONFIG_THUMB2_KERNEL */
	*kp->addr = kp->opcode;
	flush_insns(kp->addr, sizeof(kp->addr[0]));
#endif
	return 0;
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	stop_machine(__arch_disarm_kprobe, p, &cpu_online_map);
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn, 0);
		p->ainsn.insn = NULL;
	}
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = kcb->prev_kprobe.kp;
	kcb->kprobe_status = kcb->prev_kprobe.status;
}

static void __kprobes set_current_kprobe(struct kprobe *p)
{
	__get_cpu_var(current_kprobe) = p;
}

static void __kprobes singlestep(struct kprobe *p, struct pt_regs *regs,
				 struct kprobe_ctlblk *kcb)
{
	regs->ARM_pc += 4;
	if (p->ainsn.insn_check_cc(regs->ARM_cpsr))
		p->ainsn.insn_handler(p, regs);
}

/*
 * Called with IRQs disabled. IRQs must remain disabled from that point
 * all the way until processing this kprobe is complete.  The current
 * kprobes implementation cannot process more than one nested level of
 * kprobe, and that level is reserved for user kprobe handlers, so we can't
 * risk encountering a new kprobe in an interrupt handler.
 */
void __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p, *cur;
	struct kprobe_ctlblk *kcb;

	kcb = get_kprobe_ctlblk();
	cur = kprobe_running();

#ifdef CONFIG_THUMB2_KERNEL
	/*
	 * First look for a probe which was registered using an address with
	 * bit 0 set, this is the usual situation for pointers to Thumb code.
	 * If not found, fallback to looking for one with bit 0 clear.
	 */
	p = get_kprobe((kprobe_opcode_t *)(regs->ARM_pc | 1));
	if (!p)
		p = get_kprobe((kprobe_opcode_t *)regs->ARM_pc);

#else /* ! CONFIG_THUMB2_KERNEL */
	p = get_kprobe((kprobe_opcode_t *)regs->ARM_pc);
#endif

	if (p) {
		if (cur) {
			/* Kprobe is pending, so we're recursing. */
			switch (kcb->kprobe_status) {
			case KPROBE_HIT_ACTIVE:
			case KPROBE_HIT_SSDONE:
				/* A pre- or post-handler probe got us here. */
				kprobes_inc_nmissed_count(p);
				save_previous_kprobe(kcb);
				set_current_kprobe(p);
				kcb->kprobe_status = KPROBE_REENTER;
				singlestep(p, regs, kcb);
				restore_previous_kprobe(kcb);
				break;
			default:
				/* impossible cases */
				BUG();
			}
		} else {
			set_current_kprobe(p);
			kcb->kprobe_status = KPROBE_HIT_ACTIVE;

			/*
			 * If we have no pre-handler or it returned 0, we
			 * continue with normal processing.  If we have a
			 * pre-handler and it returned non-zero, it prepped
			 * for calling the break_handler below on re-entry,
			 * so get out doing nothing more here.
			 */
			if (!p->pre_handler || !p->pre_handler(p, regs)) {
				kcb->kprobe_status = KPROBE_HIT_SS;
				singlestep(p, regs, kcb);
				if (p->post_handler) {
					kcb->kprobe_status = KPROBE_HIT_SSDONE;
					p->post_handler(p, regs, 0);
				}
				reset_current_kprobe();
			}
		}
	} else if (cur) {
		/* We probably hit a jprobe.  Call its break handler. */
		if (cur->break_handler && cur->break_handler(cur, regs)) {
			kcb->kprobe_status = KPROBE_HIT_SS;
			singlestep(cur, regs, kcb);
			if (cur->post_handler) {
				kcb->kprobe_status = KPROBE_HIT_SSDONE;
				cur->post_handler(cur, regs, 0);
			}
		}
		reset_current_kprobe();
	} else {
		/*
		 * The probe was removed and a race is in progress.
		 * There is nothing we can do about it.  Let's restart
		 * the instruction.  By the time we can restart, the
		 * real instruction will be there.
		 */
	}
}

static int __kprobes kprobe_trap_handler(struct pt_regs *regs, unsigned int instr)
{
	unsigned long flags;
	local_irq_save(flags);
	kprobe_handler(regs);
	local_irq_restore(flags);
	return 0;
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, unsigned int fsr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the PC to point back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		regs->ARM_pc = (long)cur->addr;
		if (kcb->kprobe_status == KPROBE_REENTER) {
			restore_previous_kprobe(kcb);
		} else {
			reset_current_kprobe();
		}
		break;

	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/*
		 * We increment the nmissed count for accounting,
		 * we can also use npre/npostfault count for accounting
		 * these specific fault cases.
		 */
		kprobes_inc_nmissed_count(cur);

		/*
		 * We come here because instructions in the pre/post
		 * handler caused the page_fault, this could happen
		 * if handler tries to access user space by
		 * copy_from_user(), get_user() etc. Let the
		 * user-specified handler try to fix it.
		 */
		if (cur->fault_handler && cur->fault_handler(cur, regs, fsr))
			return 1;
		break;

	default:
		break;
	}

	return 0;
}

int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	/*
	 * notify_die() is currently never called on ARM,
	 * so this callback is currently empty.
	 */
	return NOTIFY_DONE;
}

/*
 * When a retprobed function returns, trampoline_handler() is called,
 * calling the kretprobe's handler. We construct a struct pt_regs to
 * give a view of registers r0-r11 to the user return-handler.  This is
 * not a complete pt_regs structure, but that should be plenty sufficient
 * for kretprobe handlers which should normally be interested in r0 only
 * anyway.
 */
void __naked __kprobes kretprobe_trampoline(void)
{
	__asm__ __volatile__ (
		"stmdb	sp!, {r0 - r11}		\n\t"
		"mov	r0, sp			\n\t"
		"bl	trampoline_handler	\n\t"
		"mov	lr, r0			\n\t"
		"ldmia	sp!, {r0 - r11}		\n\t"
#ifdef CONFIG_THUMB2_KERNEL
		"bx	lr			\n\t"
#else
		"mov	pc, lr			\n\t"
#endif
		: : : "memory");
}

/* Called from kretprobe_trampoline */
static __used __kprobes void *trampoline_handler(struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head, empty_rp;
	struct hlist_node *node, *tmp;
	unsigned long flags, orig_ret_address = 0;
	unsigned long trampoline_address = (unsigned long)&kretprobe_trampoline;

	INIT_HLIST_HEAD(&empty_rp);
	kretprobe_hash_lock(current, &head, &flags);

	/*
	 * It is possible to have multiple instances associated with a given
	 * task either because multiple functions in the call path have
	 * a return probe installed on them, and/or more than one return
	 * probe was registered for a target function.
	 *
	 * We can handle this because:
	 *     - instances are always inserted at the head of the list
	 *     - when multiple return probes are registered for the same
	 *       function, the first instance's ret_addr will point to the
	 *       real return address, and all the rest will point to
	 *       kretprobe_trampoline
	 */
	hlist_for_each_entry_safe(ri, node, tmp, head, hlist) {
		if (ri->task != current)
			/* another task is sharing our hash bucket */
			continue;

		if (ri->rp && ri->rp->handler) {
			__get_cpu_var(current_kprobe) = &ri->rp->kp;
			get_kprobe_ctlblk()->kprobe_status = KPROBE_HIT_ACTIVE;
			ri->rp->handler(ri, regs);
			__get_cpu_var(current_kprobe) = NULL;
		}

		orig_ret_address = (unsigned long)ri->ret_addr;
		recycle_rp_inst(ri, &empty_rp);

		if (orig_ret_address != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	kretprobe_assert(ri, orig_ret_address, trampoline_address);
	kretprobe_hash_unlock(current, &flags);

	hlist_for_each_entry_safe(ri, node, tmp, &empty_rp, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}

	return (void *)orig_ret_address;
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->ARM_lr;

	/* Replace the return addr with trampoline addr. */
	regs->ARM_lr = (unsigned long)&kretprobe_trampoline;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	long sp_addr = regs->ARM_sp;
	long cpsr;

	kcb->jprobe_saved_regs = *regs;
	memcpy(kcb->jprobes_stack, (void *)sp_addr, MIN_STACK_SIZE(sp_addr));
	regs->ARM_pc = (long)jp->entry;

	cpsr = regs->ARM_cpsr | PSR_I_BIT;
#ifdef CONFIG_THUMB2_KERNEL
	/* Set correct Thumb state in cpsr */
	if (regs->ARM_pc & 1)
		cpsr |= PSR_T_BIT;
	else
		cpsr &= ~PSR_T_BIT;
#endif
	regs->ARM_cpsr = cpsr;

	preempt_disable();
	return 1;
}

void __kprobes jprobe_return(void)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	__asm__ __volatile__ (
		/*
		 * Setup an empty pt_regs. Fill SP and PC fields as
		 * they're needed by longjmp_break_handler.
		 *
		 * We allocate some slack between the original SP and start of
		 * our fabricated regs. To be precise we want to have worst case
		 * covered which is STMFD with all 16 regs so we allocate 2 *
		 * sizeof(struct_pt_regs)).
		 *
		 * This is to prevent any simulated instruction from writing
		 * over the regs when they are accessing the stack.
		 */
#ifdef CONFIG_THUMB2_KERNEL
		"sub    r0, %0, %1		\n\t"
		"mov    sp, r0			\n\t"
#else
		"sub    sp, %0, %1		\n\t"
#endif
		"ldr    r0, ="__stringify(JPROBE_MAGIC_ADDR)"\n\t"
		"str    %0, [sp, %2]		\n\t"
		"str    r0, [sp, %3]		\n\t"
		"mov    r0, sp			\n\t"
		"bl     kprobe_handler		\n\t"

		/*
		 * Return to the context saved by setjmp_pre_handler
		 * and restored by longjmp_break_handler.
		 */
#ifdef CONFIG_THUMB2_KERNEL
		"ldr	lr, [sp, %2]		\n\t" /* lr = saved sp */
		"ldrd	r0, r1, [sp, %5]	\n\t" /* r0,r1 = saved lr,pc */
		"ldr	r2, [sp, %4]		\n\t" /* r2 = saved psr */
		"stmdb	lr!, {r0, r1, r2}	\n\t" /* push saved lr and */
						      /* rfe context */
		"ldmia	sp, {r0 - r12}		\n\t"
		"mov	sp, lr			\n\t"
		"ldr	lr, [sp], #4		\n\t"
		"rfeia	sp!			\n\t"
#else
		"ldr	r0, [sp, %4]		\n\t"
		"msr	cpsr_cxsf, r0		\n\t"
		"ldmia	sp, {r0 - pc}		\n\t"
#endif
		:
		: "r" (kcb->jprobe_saved_regs.ARM_sp),
		  "I" (sizeof(struct pt_regs) * 2),
		  "J" (offsetof(struct pt_regs, ARM_sp)),
		  "J" (offsetof(struct pt_regs, ARM_pc)),
		  "J" (offsetof(struct pt_regs, ARM_cpsr)),
		  "J" (offsetof(struct pt_regs, ARM_lr))
		: "memory", "cc");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	long stack_addr = kcb->jprobe_saved_regs.ARM_sp;
	long orig_sp = regs->ARM_sp;
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	if (regs->ARM_pc == JPROBE_MAGIC_ADDR) {
		if (orig_sp != stack_addr) {
			struct pt_regs *saved_regs =
				(struct pt_regs *)kcb->jprobe_saved_regs.ARM_sp;
			printk("current sp %lx does not match saved sp %lx\n",
			       orig_sp, stack_addr);
			printk("Saved registers for jprobe %p\n", jp);
			show_regs(saved_regs);
			printk("Current registers\n");
			show_regs(regs);
			BUG();
		}
		*regs = kcb->jprobe_saved_regs;
		memcpy((void *)stack_addr, kcb->jprobes_stack,
		       MIN_STACK_SIZE(stack_addr));
		preempt_enable_no_resched();
		return 1;
	}
	return 0;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}

#ifdef CONFIG_THUMB2_KERNEL

static struct undef_hook kprobes_thumb16_break_hook = {
	.instr_mask	= 0xffff,
	.instr_val	= KPROBE_THUMB16_BREAKPOINT_INSTRUCTION,
	.cpsr_mask	= MODE_MASK,
	.cpsr_val	= SVC_MODE,
	.fn		= kprobe_trap_handler,
};

static struct undef_hook kprobes_thumb32_break_hook = {
	.instr_mask	= 0xffffffff,
	.instr_val	= KPROBE_THUMB32_BREAKPOINT_INSTRUCTION,
	.cpsr_mask	= MODE_MASK,
	.cpsr_val	= SVC_MODE,
	.fn		= kprobe_trap_handler,
};

#else  /* !CONFIG_THUMB2_KERNEL */

static struct undef_hook kprobes_arm_break_hook = {
	.instr_mask	= 0xffffffff,
	.instr_val	= KPROBE_ARM_BREAKPOINT_INSTRUCTION,
	.cpsr_mask	= MODE_MASK,
	.cpsr_val	= SVC_MODE,
	.fn		= kprobe_trap_handler,
};

#endif /* !CONFIG_THUMB2_KERNEL */

int __init arch_init_kprobes()
{
	arm_kprobe_decode_init();
#ifdef CONFIG_THUMB2_KERNEL
	register_undef_hook(&kprobes_thumb16_break_hook);
	register_undef_hook(&kprobes_thumb32_break_hook);
#else
	register_undef_hook(&kprobes_arm_break_hook);
#endif
	return 0;
}
