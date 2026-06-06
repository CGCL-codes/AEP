#ifndef _MALLOC_H
#define _MALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_size_t

#include <bits/alltypes.h>

void *malloc (size_t);
void *calloc (size_t, size_t);
void *pv_calloc(size_t, size_t, int);
void *realloc (void *, size_t);
void free (void *);
void *valloc (size_t);
void *memalign(size_t, size_t);

size_t malloc_usable_size(void *);
size_t pv_malloc_usable_size(void *, int);

#ifdef __cplusplus
}
#endif

#endif
