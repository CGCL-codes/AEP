#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/kallsyms.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/sizes.h>
#include <linux/aep.h>
#include <linux/random.h>
#include <linux/atomic.h>
#include <linux/highmem.h> // for kmap_atomic()
#include <linux/writeback.h>
#include <linux/capability.h>

#include <asm/special_insns.h>
#include <asm/cpufeature.h>
#include <asm/hypervisor.h>
#include <asm/cmdline.h>
#include <asm/pgtable.h> //swapper_pg_dir存在
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>
#include <asm/sections.h>
#include <asm/traps.h>
#include <asm/insn.h> //insn_init, insn_get_length

struct mm_struct *root_mm = &init_mm;
struct aep_task_data *mutant_templates[MAX_MUTANT_TEMPLATE_NUM];
unsigned long mutant_debug_level = MUTANT_ERR | MUTANT_INFO | MUTANT_DEBUG;

inline void aep_sync_pv(pgd_t *runtime_pgd, pgd_t *root_pgd)
{
	memcpy(runtime_pgd + pgd_index(PRIVATE_VMALLOC_START),
	       root_pgd + pgd_index(PRIVATE_VMALLOC_START), 1 * sizeof(pgd_t));
           //pv 512GB size, need 1 pgd 
	return;
}

static inline void aep_modify_pgtable(struct task_struct *tsk,
				      struct aep_task_data *aep)
{
	pgd_t *root_pgd;
	pgd_t *runtime_pgd;

	/*同步内核页表*/
	root_pgd = aep->mm->pgd;
	runtime_pgd = tsk->mm->pgd;

	aep_sync_pv(runtime_pgd, root_pgd);
	return;
}

static inline void init_private_vmalloc(struct aep_task_data *aep)
{
	struct aep_private_vmalloc *pv =
		kzalloc(sizeof(struct aep_private_vmalloc), GFP_KERNEL);
	aep->pv = pv;

	pv->vmap_area_root = RB_ROOT;
	INIT_LIST_HEAD(&pv->vmap_area_list);

	pv->free_vmap_area_root = RB_ROOT;
	INIT_LIST_HEAD(&pv->free_vmap_area_list);
	private_vmalloc_init_free_space(pv);

	spin_lock_init(&pv->vmap_area_lock);

	return;
}



static struct aep_task_data *aep_alloc(int index)
{
	struct aep_task_data *aep;
	struct mm_struct *mm_new;
	int err = -ENOMEM;
	int i;

	mutant_printk(MUTANT_INFO, "enter aep_alloc\n");

	aep = kzalloc(sizeof(*aep), GFP_KERNEL);
	if (!aep)
		return NULL;
	mutant_printk(MUTANT_INFO, "aep address: %lx\n",
		      (unsigned long)aep);

	mm_new = kzalloc(sizeof(*mm_new), GFP_KERNEL);
	if (!mm_new)
		return NULL;
	mutant_printk(MUTANT_INFO, "mm_new address: %lx\n",
		      (unsigned long)mm_new);

	aep->mm = mm_new;

	aep->mm->pgd = (pgd_t *)get_zeroed_page(GFP_KERNEL);
	if (!aep->mm->pgd)
		goto free_ptes;
	mutant_printk(MUTANT_INFO, "aep->pgd address: 0x%lx\n",
		      (unsigned long)aep->mm->pgd);


	aep->index = index;

	// private vmalloc
	init_private_vmalloc(aep);

    // pv_brk and pv_mmap pages accout 1/2 of the pv region
	spin_lock_init(&aep->pv_devmap_lock);
	aep->pv_devmap_start = PRIVATE_VMALLOC_START + PRIVATE_VMALLOC_SIZE/4;
	aep->pv_devmap_end = PRIVATE_VMALLOC_START + PRIVATE_VMALLOC_SIZE/2;
	aep->pv_devmap_next = aep->pv_devmap_start;
	aep->pv_brk_start = PRIVATE_VMALLOC_START + PRIVATE_VMALLOC_SIZE/2;
	aep->pv_brk_end = aep->pv_brk_start + PRIVATE_VMALLOC_SIZE/8; // pv brk region is 1/8 of the pv region
	aep->pv_brk = aep->pv_brk_start;
	aep->pv_addr_start = PRIVATE_VMALLOC_START + PAGE_SIZE * 2; // need one gap for two pages
	aep->pv_mmap_start = aep->pv_brk_end + PAGE_SIZE; // add guard page
	aep->pv_mmap_end = aep->pv_mmap_start + PRIVATE_VMALLOC_SIZE/8; // pv mmap region is 1/8 of the pv region
    aep->pv_exec_start = PV_EXEC_START;
    aep->pv_exec_end = PV_EXEC_END;

    printk("pv_addr_start: 0x%lx, pv_mmap_start: 0x%lx, pv_mmap_end: 0x%lx\n", aep->pv_addr_start, aep->pv_mmap_start, aep->pv_mmap_end);
    printk("pv_devmap_start: 0x%lx, pv_devmap_end: 0x%lx\n", aep->pv_devmap_start, aep->pv_devmap_end);
    printk("pv_exec_start: 0x%lx, pv_exec_end: 0x%lx\n", aep->pv_exec_start, aep->pv_exec_end);
	
	atomic_set(&aep->ref_count, 0);

	return aep;

	free_page((unsigned long)aep->mm->pgd);
free_ptes:
	kfree(aep);
	return NULL;
}

inline void aep_child_small_init(struct task_struct *tsk,
				 struct aep_task_data *aep)
{
	if (unlikely(!aep)) {
        printk("aep_child_small_init: aep is empty! cannot init child.\n");
		return;
	}

	tsk->aep = aep;
	tsk->should_kill = false;
	atomic_inc(&aep->ref_count);

	return;
}

//为子进程开启隔离，用参数aep作为同步内核页表的根
int aep_child_init(struct task_struct *tsk, struct aep_task_data *aep)
{
	if (unlikely(!aep)) {
        printk("aep_child_small_init: aep is empty! cannot init child.\n");
		return 0;
	}

	aep_modify_pgtable(tsk, aep);
	tsk->aep = aep;
	tsk->should_kill = false;
	atomic_inc(&aep->ref_count);

	return 0;
}
EXPORT_SYMBOL(aep_child_init);

void aep_child_exit(struct task_struct *tsk)
{
	struct aep_task_data *aep = tsk->aep;

	if (aep) {
		tsk->should_kill = true;
		tsk->aep = NULL;
		atomic_dec(&aep->ref_count);
	}

	return;
}
EXPORT_SYMBOL(aep_child_exit);

//生成root aep,作为这个进程之后所有子进程的页表的模板
struct aep_task_data *aep_init(int index)
{
	struct aep_task_data *root;

	root = aep_alloc(index);
	if (!root) {
        printk("aep_alloc index = %d failed!\n", index);
		return NULL;
	}

	unsigned long addr = (unsigned long)root_directfs_private_vmalloc_with_exec(root, PV_EXEC_SIZE, PV_EXEC_START, PV_EXEC_END);
	printk("alloc pv exec pages: %lx, PV_EXEC_START: %lx, PV_EXEC_END: %lx\n", addr, PV_EXEC_START, PV_EXEC_END);
	
	root->cb.entry = aep_get_shadow_entry(root);
	printk("aep shadow entry pte at: %lx\n", root->cb.entry);
	printk("aep: aep_init succeed!\n");

	return root;
}
EXPORT_SYMBOL(aep_init);

static int init_one_aep_template(void)
{
	int i;
	// 0 reserved for privilege template
	for (i = 1; i < MAX_MUTANT_TEMPLATE_NUM; i++) {
		if (mutant_templates[i])
			continue;
		mutant_templates[i] = aep_init(i);
		break;
	}

	return i > MAX_MUTANT_TEMPLATE_NUM ? -1 : i;
}


extern void pv_va_free(struct aep_task_data *);
extern void pv_fv_free(struct aep_private_vmalloc *);

static inline void free_aep_private_vmalloc(struct aep_task_data *aep)
{
	pv_va_free(aep);
	pv_vpl_free(aep->pv);
	pv_fv_free(aep->pv);
}

void aep_exit(struct aep_task_data *aep)
{
	int ref_count;
	if (!aep) {
		return;
	}
	
	ref_count = atomic_read(&aep->ref_count);
	if (ref_count) {
        printk("aep root left %d process!\n",ref_count);
	}

	free_aep_private_vmalloc(aep);
	kfree(aep->pv);
    aep->pv = NULL;

	free_page((unsigned long)aep->mm->pgd);
	kfree(aep->mm);
	aep->mm = NULL;
	kfree(aep);
	aep = NULL;
    printk("aep: aep root exit\n");
}
EXPORT_SYMBOL(aep_exit);

static int aep_exit_user_interface(struct aep_task_data *aep_root)
{
	if (!aep_root) {
        printk("test_root lost!\n");
		return 0;
	} else {
		aep_exit(aep_root);
		mutant_printk(MUTANT_INFO, "aep exit success\n");
	}

	return 1;
}

//aep_init,aep_exit_template, aep_join, pv_brk

static DEFINE_SPINLOCK(pv_brk_lock);
SYSCALL_DEFINE1(pv_brk, unsigned long, brk)
{
	unsigned long retval;
	unsigned long newbrk, oldbrk, origbrk;
	unsigned long min_brk;
	struct aep_task_data* aep = current->aep;

	if (!aep) {
		printk("pv_brk is called by normal process.\n");

		return -1;
	}

	spin_lock(&pv_brk_lock);

	origbrk = aep->pv_brk;

	min_brk = aep->pv_brk_start;

	mutant_printk(MUTANT_INFO,"pv_brk: brk %lx, min_brk %lx, pv_brk %lx\n", brk, min_brk, aep->pv_brk);

	if (brk < min_brk) {
		mutant_printk(MUTANT_ERR,"pv_brk: brk < min_brk.\n");

		goto out;
	}

	newbrk = PAGE_ALIGN(brk);

	// unsigned long *user_pages_start_addr = &(((struct template_metadata *)PRIVATE_VMALLOC_START)->user_pages_start_addr);
	// if (newbrk >= *user_pages_start_addr) {
	if (newbrk >= aep->pv_brk_end) {
		mutant_printk(MUTANT_ERR,"pv_brk: brk >= user_pages_start_addr.\n");

		goto out;
	}

	oldbrk = PAGE_ALIGN(aep->pv_brk);
	if (oldbrk == newbrk) {
		aep->pv_brk = brk;
		goto success;
	}

	// shink pv heap
	if (brk <= aep->pv_brk) {
		int ret;

		aep->pv_brk = brk;
		
		unsigned long addr = newbrk;
		for (; addr < oldbrk; addr += PAGE_SIZE) {
			private_vunmap(aep, addr);
		}		
		pv_vpl_free(aep->pv);

		goto success;
	}

	unsigned long addr = oldbrk;
	for (; addr < newbrk; addr += PAGE_SIZE) {
		directfs_private_vmalloc(PAGE_SIZE, addr, addr + PAGE_SIZE);
	}

	aep->pv_brk = brk;

success:
	spin_unlock(&pv_brk_lock);
    printk("pv_brk: success, brk= %lx.\n", aep->pv_brk);

	return brk;

out:
	retval = origbrk;
	spin_unlock(&pv_brk_lock);

	return retval;
}

SYSCALL_DEFINE0(aep_init){
    return init_one_aep_template();
}

SYSCALL_DEFINE1(aep_join, unsigned, template_id)
{
	pid_t pid;
#ifdef CONFIG_MMU
	struct kernel_clone_args args = {
		.exit_signal = SIGCHLD,
	};

	if (!mutant_templates[template_id])
		return -EINVAL;

	pid = aep_kernel_clone(&args, template_id);

	return pid;

#else
	/* can not support in nommu mode */
	return -EINVAL;
#endif
}

SYSCALL_DEFINE1(aep_exit_template, unsigned, template_id)
{
	int ret;
	ret = aep_exit_user_interface(mutant_templates[template_id]);
	if (ret) {
		mutant_templates[template_id] = NULL;
	}
	return ret;
}


//for test only
SYSCALL_DEFINE1(unmap_aep_shadow_entry, unsigned, template_id)
{
	struct aep_task_data * root = mutant_templates[template_id];
	if (root == NULL) return 0;

	printk("enter syscall unmap_aep_shadow_entry\n");

	aep_unmap_shadow_entry(root, root->cb.entry);
	return 0;
}

SYSCALL_DEFINE4(aep_map_cxl, unsigned int, template_id, unsigned long, phys_addr,
		unsigned long, size, unsigned long, prot_flags)
{
	struct aep_task_data *root;
	pgprot_t prot;
	void *addr;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (template_id >= MAX_MUTANT_TEMPLATE_NUM)
		return -EINVAL;

	if (!PAGE_ALIGNED(phys_addr) || !PAGE_ALIGNED(size) || !size)
		return -EINVAL;

	root = mutant_templates[template_id];
	if (!root)
		return -EINVAL;

	prot = (prot_flags & 0x1) ? PAGE_KERNEL_PRIVATE_EXEC : PAGE_KERNEL_PRIVATE;
	addr = aep_private_map_cxl_pfn(root, (phys_addr_t)phys_addr, size, prot);
	if (!addr)
		return -ENOMEM;

	return (unsigned long)addr;
}




