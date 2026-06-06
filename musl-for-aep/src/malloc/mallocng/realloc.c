#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include "meta.h"

void *realloc(void *p, size_t n)
{
	if (!p) return malloc(n);
	if (size_overflows(n)) return 0;

	struct meta *g = get_meta(p);
	int idx = get_slot_index(p);
	size_t stride = get_stride(g);
	unsigned char *start = g->mem->storage + stride*idx;
	unsigned char *end = start + stride - IB;
	size_t old_size = get_nominal_size(p, end);
	size_t avail_size = end-(unsigned char *)p;
	void *new;

	// only resize in-place if size class matches
	if (n <= avail_size && n<MMAP_THRESHOLD
	    && size_to_class(n)+1 >= g->sizeclass) {
		set_size(p, end, n);
		return p;
	}

	// use mremap if old and new size are both mmap-worthy
	if (g->sizeclass>=48 && n>=MMAP_THRESHOLD) {
		DEBUG_PRINT("realloc: g->sizeclass>=48 && n>=MMAP_THRESHOLD, use mremap\n");
		
		assert(g->sizeclass==63);
		size_t base = (unsigned char *)p-start;
		size_t needed = (n + base + UNIT + IB + 4095) & -4096;
		new = g->maplen*4096UL == needed ? g->mem :
			mremap(g->mem, g->maplen*4096UL, needed, MREMAP_MAYMOVE);
		if (new!=MAP_FAILED) {
			g->mem = new;
			g->maplen = needed/4096;
			p = g->mem->storage + base;
			end = g->mem->storage + (needed - UNIT) - IB;
			*end = 0;
			set_size(p, end, n);
			return p;
		}
	}

	new = malloc(n);
	if (!new) return 0;
	memcpy(new, p, n < old_size ? n : old_size);
	free(p);
	return new;
}

void *pv_realloc(void *p, size_t n, int thread_num)
{
	if (thread_num < 0 || thread_num >= NUM_PV_MALLOC_T) return 0;

	if (!p) return pv_malloc(n, thread_num);
	if (size_overflows(n)) return 0;

	struct meta *g = pv_get_meta(p, thread_num);
	int idx = get_slot_index(p);
	size_t stride = get_stride(g);
	unsigned char *start = g->mem->storage + stride*idx;
	unsigned char *end = start + stride - IB;
	size_t old_size = get_nominal_size(p, end);
	size_t avail_size = end-(unsigned char *)p;
	void *new;

	// only resize in-place if size class matches
	if (n <= avail_size && n<MMAP_THRESHOLD
	    && size_to_class(n)+1 >= g->sizeclass) {
		set_size(p, end, n);
		return p;
	}

	// use mremap if old and new size are both mmap-worthy
	if (g->sizeclass>=48 && n>=MMAP_THRESHOLD) {
		DEBUG_PRINT("realloc: g->sizeclass>=48 && n>=MMAP_THRESHOLD, use mremap\n");
		
		assert(g->sizeclass==63);
		size_t base = (unsigned char *)p-start;
		size_t needed = (n + base + UNIT + IB + 4095) & -4096;
		new = g->maplen*4096UL == needed ? g->mem :
			mremap(g->mem, g->maplen*4096UL, needed, MREMAP_MAYMOVE);  //may have problems
		if (new!=MAP_FAILED) {
			g->mem = new;
			g->maplen = needed/4096;
			p = g->mem->storage + base;
			end = g->mem->storage + (needed - UNIT) - IB;
			*end = 0;
			set_size(p, end, n);
			return p;
		}
	}

	new = pv_malloc(n, thread_num);
	if (!new) return 0;
	memcpy(new, p, n < old_size ? n : old_size);
	pv_free(p, thread_num);
	return new;
}

// for test
void my_lock(volatile int *l)
{
	printf("my_lock: pid = %d\n", getpid());
	__lock(l);
}

void my_unlock(volatile int *l)
{
	__unlock(l);
}
