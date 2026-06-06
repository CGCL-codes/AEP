// SPDX-License-Identifier: GPL-2.0
#ifndef _LINUX_AEP_H
#define _LINUX_AEP_H

#ifdef CONFIG_ATOMIC_EXECUTION_PROTECTION
#include <asm/aep.h>
#else
static inline int aep_init(struct task_struct *tsk) { return 0; }
static inline void aep_exit(struct task_struct *tsk) {}
#endif

#endif /* _LINUX_AEP_H */

