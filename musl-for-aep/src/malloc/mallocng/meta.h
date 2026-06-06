#ifndef MALLOC_META_H
#define MALLOC_META_H

#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include "glue.h"

__attribute__((__visibility__("hidden")))
extern const uint16_t size_classes[];

#define MMAP_THRESHOLD 131052

#define UNIT 16
#define IB 4

#define print_debug 0

#define DEBUG_PRINT(fmt, ...) \
    do { \
        if (print_debug) { \
            printf(fmt, ##__VA_ARGS__); \
        } \
    } while (0)

struct group {
	struct meta *meta;
	unsigned char active_idx:5;
	char pad[UNIT - sizeof(struct meta *) - 1];
	unsigned char storage[];
};

struct meta {
	struct meta *prev, *next;
	struct group *mem;
	volatile int avail_mask, freed_mask;
	uintptr_t last_idx:5;
	uintptr_t freeable:1;
	uintptr_t sizeclass:6;
	uintptr_t maplen:8*sizeof(uintptr_t)-12;
};

struct meta_area {
	uint64_t check;
	struct meta_area *next;
	int nslots;
	struct meta slots[];
};

struct malloc_context {
	uint64_t secret;
#ifndef PAGESIZE
	size_t pagesize;
#endif
	int init_done;
	unsigned mmap_counter;
	struct meta *free_meta_head;
	struct meta *avail_meta;
	size_t avail_meta_count, avail_meta_area_count, meta_alloc_shift;
	struct meta_area *meta_area_head, *meta_area_tail;
	unsigned char *avail_meta_areas;
	struct meta *active[48];
	size_t usage_by_class[48];
	uint8_t unmap_seq[32], bounces[32];
	uint8_t seq;
	uintptr_t brk;
};

#define POOL_PAGE_SIZE 4096                // 页大小为 4KB
#define POOL_TOTAL_SIZE (4ULL * 1024 * 1024 * 1024) // 4GB
#define POOL_MAX_PAGES (POOL_TOTAL_SIZE / POOL_PAGE_SIZE)
#define POOL_BITMAP_SIZE ((POOL_MAX_PAGES + 63) / 64) // 位图大小（字节）

struct pv_mempool {
    uint8_t* base;        // 基地址
    size_t total_pages;   // 总页数
    size_t free_pages;    // 空闲页数
    uint64_t* page_bitmap; // 位图（每个 bit 表示一页状态）
	size_t last_scan_pos;
	int lock;
};

__attribute__((__visibility__("hidden")))
extern struct pv_mempool pv_pool;

static inline void pv_pool_lock()
{
	LOCK(&pv_pool.lock);
}

static inline void pv_pool_unlock()
{
	UNLOCK(&pv_pool.lock);
}

static inline void pv_pool_resetlock()
{
	pv_pool.lock = 0;
}


static inline void pv_pool_print_bitmap(void) {
    size_t total_qwords = POOL_BITMAP_SIZE; // 计算完整的 64 位块数

    for (size_t qword_idx = 0; qword_idx < total_qwords; qword_idx++) {
        uint64_t qword = pv_pool.page_bitmap[qword_idx]; // 获取当前 64 位块

        // 如果当前块全 0，跳过
        if (qword == 0) {
            continue;
        }

		printf("line%d: ", qword_idx);

        // 打印当前块的每一位
        for (int bit_idx = 63; bit_idx >= 0; bit_idx--) {
            printf("%d", (qword & (1ULL << bit_idx)) ? 1 : 0);
        }
        printf("\n"); // 每行显示一个 64 位块
    }
}

static inline size_t calculate_page_count(size_t memory_size) {
    // 向上取整
    return (memory_size + POOL_PAGE_SIZE - 1) / POOL_PAGE_SIZE;
}

static inline int pv_pool_is_page_free(size_t page) {
    size_t qword = page / 64;
    size_t bit = page % 64;
    return !(pv_pool.page_bitmap[qword] & (1ULL << bit));
}

// 设置页状态
static inline void pv_pool_set_page_used(size_t page, int used) {
    size_t qword = page / 64;
    size_t bit = page % 64;
    if (used) {
        pv_pool.page_bitmap[qword] |= (1ULL << bit);
    } else {
        pv_pool.page_bitmap[qword] &= ~(1ULL << bit);
    }
}

// 快速查找连续空闲页（Next Fit + 块扫描优化）
static inline void* pv_pool_allocate_pages(size_t num_pages) {
    if (num_pages == 0 || num_pages > pv_pool.free_pages) {
        printf("Invalid page count: %zu\n", num_pages);
        return NULL;
    }

	pv_pool_lock();

	if (pv_pool.last_scan_pos >= pv_pool.total_pages){
		printf(" pool is full, no enough consecutive pages: %zu\n", num_pages);
		pv_pool_unlock();
		return NULL;
	}

    size_t start_page = 0;
    size_t current_streak = 0;
    size_t scan_start = pv_pool.last_scan_pos;

    // 从上次扫描位置开始，循环扫描
    for (size_t current_page = scan_start; current_page < pv_pool.total_pages; current_page++) {

        if (pv_pool_is_page_free(current_page)) {
            if (current_streak == 0) start_page = current_page;
            current_streak++;
            if (current_streak == num_pages) {
                pv_pool.last_scan_pos = (current_page + 1) % pv_pool.total_pages;
                goto found;
            }
        } else {
            current_streak = 0;
        }
    }

	pv_pool_unlock();
    // 未找到连续页
    printf("No enough consecutive pages: %zu\n", num_pages);
    return NULL;

found:
    // 标记页为已使用
    for (size_t p = start_page; p < start_page + num_pages; p++) {
        pv_pool_set_page_used(p, 1);
    }
    pv_pool.free_pages -= num_pages;
	//printf("pv_pool alloc: addr: %lx, pages: %d\n", pv_pool.base + start_page * POOL_PAGE_SIZE, num_pages);
	//pv_pool_print_bitmap();
	pv_pool_unlock();
    return pv_pool.base + start_page * POOL_PAGE_SIZE;
}

// 释放内存
static inline void pv_pool_free_pages(void* ptr, size_t num_pages) {
    if (!ptr || num_pages == 0) {
        printf("Invalid free\n");
        return;
    }

	pv_pool_lock();

    size_t start_page = ((uint8_t *)ptr - pv_pool.base) / POOL_PAGE_SIZE;
    if (start_page + num_pages > pv_pool.total_pages) {
        printf("Free out of range\n");
		pv_pool_unlock();
        return;
    }

    // 检查所有页是否已分配
    for (size_t p = start_page; p < start_page + num_pages; p++) {
        if (pv_pool_is_page_free(p)) {
            printf("Page %zu not allocated\n", p);
			pv_pool_unlock();
            return;
        }
    }

    // 标记页为空闲
    for (size_t p = start_page; p < start_page + num_pages; p++) {
        pv_pool_set_page_used(p, 0);
    }

 	if (start_page < pv_pool.last_scan_pos) {
        pv_pool.last_scan_pos = start_page;
    }

    pv_pool.free_pages += num_pages;
	//printf("pv_pool free: addr: %lx, pages: %d\n", ptr, num_pages);
	//pv_pool_print_bitmap();
	pv_pool_unlock();
}


#define NUM_PV_MALLOC_T 20

__attribute__((__visibility__("hidden")))
extern struct malloc_context ctx;

__attribute__((__visibility__("hidden")))
extern struct malloc_context ctx_thread_local[NUM_PV_MALLOC_T];

#ifdef PAGESIZE
#define PGSZ PAGESIZE
#else
#define PGSZ ctx.pagesize
#endif

__attribute__((__visibility__("hidden")))
struct meta *alloc_meta(void);

__attribute__((__visibility__("hidden")))
int is_allzero(void *);

static inline void queue(struct meta **phead, struct meta *m)
{
	assert(!m->next);
	assert(!m->prev);
	if (*phead) {
		struct meta *head = *phead;
		m->next = head;
		m->prev = head->prev;
		m->next->prev = m->prev->next = m;
	} else {
		m->prev = m->next = m;
		*phead = m;
	}
}

static inline void dequeue(struct meta **phead, struct meta *m)
{
	if (m->next != m) {
		m->prev->next = m->next;
		m->next->prev = m->prev;
		if (*phead == m) *phead = m->next;
	} else {
		*phead = 0;
	}
	m->prev = m->next = 0;
}

static inline struct meta *dequeue_head(struct meta **phead)
{
	struct meta *m = *phead;
	if (m) dequeue(phead, m);
	return m;
}

static inline void free_meta(struct meta *m)
{
	*m = (struct meta){0};
	queue(&ctx.free_meta_head, m);
}

static inline void pv_free_meta(struct meta *m, struct malloc_context* ctx_local)
{
	*m = (struct meta){0};
	queue(&ctx_local->free_meta_head, m);
}

static inline uint32_t activate_group(struct meta *m)
{
	assert(!m->avail_mask);
	uint32_t mask, act = (2u<<m->mem->active_idx)-1;
	do mask = m->freed_mask;
	while (a_cas(&m->freed_mask, mask, mask&~act)!=mask);
	return m->avail_mask = mask & act;
}

static inline int get_slot_index(const unsigned char *p)
{
	return p[-3] & 31;
}

static inline struct meta *get_meta(const unsigned char *p)
{
	assert(!((uintptr_t)p & 15));
	int offset = *(const uint16_t *)(p - 2);
	int index = get_slot_index(p);
	if (p[-4]) {
		assert(!offset);
		offset = *(uint32_t *)(p - 8);
		assert(offset > 0xffff);
	}
	const struct group *base = (const void *)(p - UNIT*offset - UNIT);
	const struct meta *meta = base->meta;
	assert(meta->mem == base);
	assert(index <= meta->last_idx);
	assert(!(meta->avail_mask & (1u<<index)));
	assert(!(meta->freed_mask & (1u<<index)));
	const struct meta_area *area = (void *)((uintptr_t)meta & -4096);
	assert(area->check == ctx.secret);
	if (meta->sizeclass < 48) {
		assert(offset >= size_classes[meta->sizeclass]*index);
		assert(offset < size_classes[meta->sizeclass]*(index+1));
	} else {
		assert(meta->sizeclass == 63);
	}
	if (meta->maplen) {
		assert(offset <= meta->maplen*4096UL/UNIT - 1);
	}
	return (struct meta *)meta;
}

static inline struct meta *pv_get_meta(const unsigned char *p, int thread_num)
{
	if (thread_num < 0 || thread_num >= NUM_PV_MALLOC_T){ 
		printf("pv_get_meta receive error thread_num!\n");
		return 0;
	}
	struct malloc_context* ctx_local = &(ctx_thread_local[thread_num]);

	assert(!((uintptr_t)p & 15));
	int offset = *(const uint16_t *)(p - 2);
	int index = get_slot_index(p);
	if (p[-4]) {
		assert(!offset);
		offset = *(uint32_t *)(p - 8);
		assert(offset > 0xffff);
	}

	const struct group *base = (const void *)(p - UNIT*offset - UNIT);

	//printf("pv_get_meta, p: %lx, offset: %d, base: %lx, meta: %lx\n", p, offset, base, base->meta);
	const struct meta *meta = base->meta;
	assert(meta->mem == base);
	assert(index <= meta->last_idx);
	assert(!(meta->avail_mask & (1u<<index)));
	assert(!(meta->freed_mask & (1u<<index)));
	const struct meta_area *area = (void *)((uintptr_t)meta & -4096);
	assert(area->check == ctx_local->secret);
	if (meta->sizeclass < 48) {
		assert(offset >= size_classes[meta->sizeclass]*index);
		assert(offset < size_classes[meta->sizeclass]*(index+1));
	} else {
		assert(meta->sizeclass == 63);
	}
	if (meta->maplen) {
		assert(offset <= meta->maplen*4096UL/UNIT - 1);
	}
	return (struct meta *)meta;
}

static inline struct meta *pv_get_meta_ctx(const unsigned char *p, struct malloc_context* ctx_local)
{
	assert(!((uintptr_t)p & 15));
	int offset = *(const uint16_t *)(p - 2);
	int index = get_slot_index(p);
	if (p[-4]) {
		assert(!offset);
		offset = *(uint32_t *)(p - 8);
		assert(offset > 0xffff);
	}
	const struct group *base = (const void *)(p - UNIT*offset - UNIT);
	const struct meta *meta = base->meta;
	assert(meta->mem == base);
	assert(index <= meta->last_idx);
	assert(!(meta->avail_mask & (1u<<index)));
	assert(!(meta->freed_mask & (1u<<index)));
	const struct meta_area *area = (void *)((uintptr_t)meta & -4096);
	assert(area->check == ctx_local->secret);
	if (meta->sizeclass < 48) {
		assert(offset >= size_classes[meta->sizeclass]*index);
		assert(offset < size_classes[meta->sizeclass]*(index+1));
	} else {
		assert(meta->sizeclass == 63);
	}
	if (meta->maplen) {
		assert(offset <= meta->maplen*4096UL/UNIT - 1);
	}
	return (struct meta *)meta;
}

static inline size_t get_nominal_size(const unsigned char *p, const unsigned char *end)
{
	size_t reserved = p[-3] >> 5;
	if (reserved >= 5) {
		assert(reserved == 5);
		reserved = *(const uint32_t *)(end-4);
		assert(reserved >= 5);
		assert(!end[-5]);
	}
	assert(reserved <= end-p);
	assert(!*(end-reserved));
	// also check the slot's overflow byte
	assert(!*end);
	return end-reserved-p;
}

static inline size_t get_stride(const struct meta *g)
{
	if (!g->last_idx && g->maplen) {
		return g->maplen*4096UL - UNIT;
	} else {
		return UNIT*size_classes[g->sizeclass];
	}
}

static inline void set_size(unsigned char *p, unsigned char *end, size_t n)
{
	int reserved = end-p-n;
	if (reserved) end[-reserved] = 0;
	if (reserved >= 5) {
		*(uint32_t *)(end-4) = reserved;
		end[-5] = 0;
		reserved = 5;
	}
	p[-3] = (p[-3]&31) + (reserved<<5);
}

static inline void *enframe(struct meta *g, int idx, size_t n, int ctr)
{
	size_t stride = get_stride(g);
	size_t slack = (stride-IB-n)/UNIT;
	unsigned char *p = g->mem->storage + stride*idx;
	unsigned char *end = p+stride-IB;
	// cycle offset within slot to increase interval to address
	// reuse, facilitate trapping double-free.
	int off = (p[-3] ? *(uint16_t *)(p-2) + 1 : ctr) & 255;
	assert(!p[-4]);
	if (off > slack) {
		size_t m = slack;
		m |= m>>1; m |= m>>2; m |= m>>4;
		off &= m;
		if (off > slack) off -= slack+1;
		assert(off <= slack);
	}
	if (off) {
		// store offset in unused header at offset zero
		// if enframing at non-zero offset.
		*(uint16_t *)(p-2) = off;
		p[-3] = 7<<5;
		p += UNIT*off;
		// for nonzero offset there is no permanent check
		// byte, so make one.
		p[-4] = 0;
	}
	*(uint16_t *)(p-2) = (size_t)(p-g->mem->storage)/UNIT;
	p[-3] = idx;
	set_size(p, end, n);
	return p;
}

static inline int size_to_class(size_t n)
{
	n = (n+IB-1)>>4;
	if (n<10) return n;
	n++;
	int i = (28-a_clz_32(n))*4 + 8;
	if (n>size_classes[i+1]) i+=2;
	if (n>size_classes[i]) i++;
	return i;
}

static inline int size_overflows(size_t n)
{
	if (n >= SIZE_MAX/2 - 4096) {
		errno = ENOMEM;
		return 1;
	}
	return 0;
}

static inline void step_seq(void)
{
	if (ctx.seq==255) {
		for (int i=0; i<32; i++) ctx.unmap_seq[i] = 0;
		ctx.seq = 1;
	} else {
		ctx.seq++;
	}
}

static inline void record_seq(int sc)
{
	if (sc-7U < 32) ctx.unmap_seq[sc-7] = ctx.seq;
}

static inline void account_bounce(int sc)
{
	if (sc-7U < 32) {
		int seq = ctx.unmap_seq[sc-7];
		if (seq && ctx.seq-seq < 10) {
			if (ctx.bounces[sc-7]+1 < 100)
				ctx.bounces[sc-7]++;
			else
				ctx.bounces[sc-7] = 150;
		}
	}
}

static inline void decay_bounces(int sc)
{
	if (sc-7U < 32 && ctx.bounces[sc-7])
		ctx.bounces[sc-7]--;
}

static inline int is_bouncing(int sc)
{
	return (sc-7U < 32 && ctx.bounces[sc-7] >= 100);
}

static inline void pv_step_seq(struct malloc_context* ctx_local)
{
	if (ctx_local->seq==255) {
		for (int i=0; i<32; i++) ctx_local->unmap_seq[i] = 0;
		ctx_local->seq = 1;
	} else {
		ctx_local->seq++;
	}
}

static inline void pv_record_seq(int sc, struct malloc_context* ctx_local)
{
	if (sc-7U < 32) ctx_local->unmap_seq[sc-7] = ctx_local->seq;
}

static inline void pv_account_bounce(int sc, struct malloc_context* ctx_local)
{
	if (sc-7U < 32) {
		int seq = ctx_local->unmap_seq[sc-7];
		if (seq && ctx_local->seq-seq < 10) {
			if (ctx_local->bounces[sc-7]+1 < 100)
				ctx_local->bounces[sc-7]++;
			else
				ctx_local->bounces[sc-7] = 150;
		}
	}
}

static inline void pv_decay_bounces(int sc, struct malloc_context* ctx_local)
{
	if (sc-7U < 32 && ctx_local->bounces[sc-7])
		ctx_local->bounces[sc-7]--;
}

static inline int pv_is_bouncing(int sc, struct malloc_context* ctx_local)
{
	return (sc-7U < 32 && ctx_local->bounces[sc-7] >= 100);
}

#endif
