#ifndef AEPLIB_H
#define AEPLIB_H

#include <stdlib.h>
#include <sched.h>

#define NUM_AEP_MALLOC_INIT     1
#define NUM_AEP_PRINT_BITMAP    2
#define NUM_AEP_COUNT           3
#define NUM_AEP_MALLOC          4
#define NUM_AEP_FREE            5
#define NUM_AEP_LINK            6
#define NUM_AEP_UNLINK          7
#define NUM_AEP_SEND            8
#define NUM_AEP_RECV            9
#define NUM_AEP_RECOVER         10

#define FENCE  __asm__ __volatile__ ("sfence" ::: "memory")
#define FLUSH(addr) __asm__ __volatile__ ("clwb (%0)" :: "r"(addr))


#define AEP_LOCK_INIT(lock) lock=0;

#define AEP_LOCK(lock) \
	while(!__sync_bool_compare_and_swap(&lock, 0, 1)) \
		;

#define AEP_TRY_LOCK(lock) __sync_bool_compare_and_swap(&lock, 0, 1)
#define AEP_UNLOCK(lock) __sync_lock_release(&lock);
#define ATOMIC_ADD(lock) __sync_fetch_and_add(&lock, 1);
#define ATOMIC_DESC(lock) __sync_sub_and_fetch(&lock, 1);

unsigned long aep_entry(int call_number, ...);

#endif

