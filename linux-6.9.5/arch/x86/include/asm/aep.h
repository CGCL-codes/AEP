#ifndef _ASM_X86_AEP_H
#define _ASM_X86_AEP_H

#include "linux/llist.h"
#include "linux/list_bl.h"
#include "linux/mutex.h"
#include "linux/slab.h"
#include "linux/rbtree.h"
#include "linux/spinlock_types.h"
#include "linux/types.h"
#include "linux/workqueue.h"
#include <linux/capability.h>
#include <linux/dcache.h>

#define MAX_MUTANT_TEMPLATE_NUM 32

struct template_metadata {
	int aep_index; // index for aep in mutant_templates[]

	// shared with user for aep, start address ffffe880000000e0
	unsigned long user_pages_start_addr;
	unsigned long user_pages_end_addr; // alloc page from user_pages_end_addr to user_pages_start_addr
};

struct aep_private_vmalloc {
	struct rb_root vmap_area_root;
	struct list_head vmap_area_list;
	struct rb_root free_vmap_area_root;
	struct list_head free_vmap_area_list;
	struct llist_head vmap_purge_list;
	atomic_long_t nr_vmalloc_pages;
	atomic_long_t vmap_lazy_nr;
	spinlock_t vmap_area_lock;
	struct mutex vmap_purge_lock;

	struct list_head pv_cache_list;
	spinlock_t pv_cache_lock;
	struct list_head used_pv_page_list;
	spinlock_t used_pv_page_list_lock;
};

//delay controy block
struct aep_delay_cb{
	pte_t *entry;
};


struct aep_task_data {
	struct task_struct *pivot_task; //init task in the ns
	// mm->pgd as a template, it will not be loaded into CR3.
	struct mm_struct *mm;
	struct aep_private_vmalloc *pv;
	struct aep_delay_cb cb;

	int index;

	atomic_t ref_count;

	spinlock_t pv_addr_start_lock;
	unsigned long pv_addr_start;

	spinlock_t pv_devmap_lock;
	unsigned long pv_devmap_start, pv_devmap_end, pv_devmap_next;

	unsigned long pv_brk_start, pv_brk_end, pv_brk;
	unsigned long pv_mmap_start, pv_mmap_end;
    unsigned long pv_exec_start, pv_exec_end;
};


#define PV_EXEC_SIZE (PAGE_SIZE * 5120) //20M
#define PV_EXEC_END (PRIVATE_VMALLOC_END - PV_EXEC_SIZE + 1)
#define PV_EXEC_START (PV_EXEC_END - PV_EXEC_SIZE) //20M
#define AEP_SHADOW_ENTREY_ADDR (PV_EXEC_START + 0x1000)
#define AEP_SHADOW_ENTREY_SIZE (PAGE_SIZE)
#define AEP_SHADOW_ENTREY_END (AEP_SHADOW_ENTREY_ADDR + AEP_SHADOW_ENTREY_SIZE)

#define ADDR_IN_PV_REGION(addr) ((addr) >= PRIVATE_VMALLOC_START && (addr) < PRIVATE_VMALLOC_END)
#define ADDR_IN_SHADOW_ENTRY(addr) ((addr) >= AEP_SHADOW_ENTREY_ADDR && (addr) < AEP_SHADOW_ENTREY_END)
#define RANGE_IN_PV_REGION(addr, size) ((addr) >= PRIVATE_VMALLOC_START && (addr + size) < PRIVATE_VMALLOC_END)


extern unsigned long mutant_debug_level;

#define MUTANT_EMERG 1
#define MUTANT_ERR (1 << 2)
#define MUTANT_WARNING (1 << 3)
#define MUTANT_INFO (1 << 4)
#define MUTANT_DEBUG (1 << 5)

#ifdef CONFIG_ATOMIC_EXECUTION_PROTECTION

#define mutant_printk(msg_level, fmt, args...)                                 \
	{                                                                      \
		if (msg_level & mutant_debug_level) {                          \
			printk(KERN_CONT "Mutant: ");                          \
			printk(fmt, ##args);                                   \
		}                                                              \
	}

extern struct aep_task_data *mutant_templates[MAX_MUTANT_TEMPLATE_NUM];

void pv_vpl_free(struct aep_private_vmalloc *);

inline void aep_child_small_init(struct task_struct *tsk,
				 struct aep_task_data *aep);
int aep_child_init(struct task_struct *tsk, struct aep_task_data *aep);
void aep_child_exit(struct task_struct *tsk);
inline void aep_sync_pv(pgd_t *runtime_pgd, pgd_t *root_pgd);

struct aep_task_data *aep_init(int index);
void aep_exit(struct aep_task_data *aep);

//for pv memory allocate and release
void *private_vmalloc(unsigned long);
struct page *private_vmalloc_page(void);
struct page *root_private_vmalloc_page(struct aep_task_data *);

struct page *directfs_root_private_vmalloc_page(struct aep_task_data *, struct inode *, unsigned long);
void private_vunmap(struct aep_task_data *aep, void *addr);
void *directfs_root_private_vmalloc(unsigned long , struct aep_task_data *, unsigned long, unsigned long);
void *directfs_private_vmalloc(unsigned long , unsigned long, unsigned long);
void *directfs_private_vmalloc_with_exec(unsigned long , unsigned long, unsigned long);
void *root_directfs_private_vmalloc_with_exec(struct aep_task_data *aep, unsigned long size, 
										unsigned long start_addr, unsigned long end_addr);
void private_vmalloc_init_free_space(struct aep_private_vmalloc *pv);
void *root_directfs_private_vmalloc(struct aep_task_data *aep, unsigned long size, 
										unsigned long start_addr, unsigned long end_addr);
void *aep_private_map_cxl_pfn(struct aep_task_data *aep, phys_addr_t phys,
					     unsigned long size, pgprot_t prot);

void aep_unmap_shadow_entry(struct aep_task_data *aep, pte_t *pte);
pte_t *aep_get_shadow_entry(struct aep_task_data *aep);

#endif //CONFIG_ATOMIC_EXECUTION_PROTECTION

#endif