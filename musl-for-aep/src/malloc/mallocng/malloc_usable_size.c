#include <stdlib.h>
#include "meta.h"

size_t malloc_usable_size(void *p)
{
	if (!p) return 0;
	struct meta *g = get_meta(p);
	int idx = get_slot_index(p);
	size_t stride = get_stride(g);
	unsigned char *start = g->mem->storage + stride*idx;
	unsigned char *end = start + stride - IB;
	return get_nominal_size(p, end);
}

size_t pv_malloc_usable_size(void *p, int thread_num)
{
	if (thread_num < 0 || thread_num >= NUM_PV_MALLOC_T) return 0;
	
	if (!p) return 0;
	struct meta *g = pv_get_meta(p, thread_num);
	int idx = get_slot_index(p);
	size_t stride = get_stride(g);
	unsigned char *start = g->mem->storage + stride*idx;
	unsigned char *end = start + stride - IB;
	return get_nominal_size(p, end);
}
