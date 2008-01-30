#ifndef _I386_KEXEC_H
#define _I386_KEXEC_H

#define PA_CONTROL_PAGE  0
#define VA_CONTROL_PAGE  1
#define PA_PGD           2
#define VA_PGD           3
#define PA_PTE_0         4
#define VA_PTE_0         5
#define PA_PTE_1         6
#define VA_PTE_1         7
#ifdef CONFIG_X86_PAE
#define PA_PMD_0         8
#define VA_PMD_0         9
#define PA_PMD_1         10
#define VA_PMD_1         11
#define PAGES_NR         12
#else
#define PAGES_NR         8
#endif

#ifndef __ASSEMBLY__

#include <asm/ptrace.h>
#include <asm/string.h>

/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 */

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT TASK_SIZE

#define KEXEC_CONTROL_CODE_SIZE	4096

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_386

/* We can also handle crash dumps from 64 bit kernel. */
#define vmcore_elf_check_arch_cross(x) ((x)->e_machine == EM_X86_64)

/* CPU does not save ss and sp on stack if execution is already
 * running in kernel mode at the time of NMI occurrence. This code
 * fixes it.
 */
static inline void crash_fixup_ss_esp(struct pt_regs *newregs,
					struct pt_regs *oldregs)
{
	memcpy(newregs, oldregs, sizeof(*newregs));
	newregs->sp = (unsigned long)&(oldregs->sp);
	__asm__ __volatile__(
			"xorl %%eax, %%eax\n\t"
			"movw %%ss, %%ax\n\t"
			:"=a"(newregs->ss));
}

/*
 * This function is responsible for capturing register states if coming
 * via panic otherwise just fix up the ss and sp if coming via kernel
 * mode exception.
 */
static inline void crash_setup_regs(struct pt_regs *newregs,
                                       struct pt_regs *oldregs)
{
       if (oldregs)
               crash_fixup_ss_esp(newregs, oldregs);
       else {
               __asm__ __volatile__("movl %%ebx,%0" : "=m"(newregs->bx));
               __asm__ __volatile__("movl %%ecx,%0" : "=m"(newregs->cx));
               __asm__ __volatile__("movl %%edx,%0" : "=m"(newregs->dx));
               __asm__ __volatile__("movl %%esi,%0" : "=m"(newregs->si));
               __asm__ __volatile__("movl %%edi,%0" : "=m"(newregs->di));
               __asm__ __volatile__("movl %%ebp,%0" : "=m"(newregs->bp));
               __asm__ __volatile__("movl %%eax,%0" : "=m"(newregs->ax));
               __asm__ __volatile__("movl %%esp,%0" : "=m"(newregs->sp));
               __asm__ __volatile__("movl %%ss, %%eax;" :"=a"(newregs->ss));
               __asm__ __volatile__("movl %%cs, %%eax;" :"=a"(newregs->cs));
               __asm__ __volatile__("movl %%ds, %%eax;" :"=a"(newregs->ds));
               __asm__ __volatile__("movl %%es, %%eax;" :"=a"(newregs->es));
               __asm__ __volatile__("pushfl; popl %0" :"=m"(newregs->flags));

               newregs->ip = (unsigned long)current_text_addr();
       }
}
asmlinkage NORET_TYPE void
relocate_kernel(unsigned long indirection_page,
		unsigned long control_page,
		unsigned long start_address,
		unsigned int has_pae) ATTRIB_NORET;

#endif /* __ASSEMBLY__ */

#endif /* _I386_KEXEC_H */
