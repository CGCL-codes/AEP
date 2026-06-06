#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"

//get a empty mailbox cell
mailbox_cell_t *consume_mailbox_cell(mailbox_t *box){
    int end, end_new, start;
    do {
        end = atomic_load_explicit(&box->end, memory_order_acquire);
        start = atomic_load_explicit(&box->start, memory_order_acquire);
        if (end == start-1) continue;

        end_new = (end+1) % MAILBOX_SIZE;
    } while (!atomic_compare_exchange_weak_explicit(
            &box->end,          // 原子变量指针
            &end,               // 期望值指针（会被更新为实际值）
            end_new,            // 新值
            memory_order_release, // 成功时的内存序
            memory_order_relaxed  // 失败时的内存序
    ));

    return &box->cell_list[end];
}

//get a message from mailbox, return NULL means mailbox is empty
mailbox_cell_t *produce_mailbox_cell(mailbox_t *box){
    int start, start_new, end;
    do {
        start = atomic_load_explicit(&box->start, memory_order_acquire);
        end = atomic_load_explicit(&box->end, memory_order_acquire);
        if (end == start) return NULL;

        start_new = (start+1) % MAILBOX_SIZE;
    } while (!atomic_compare_exchange_weak_explicit(
            &box->start,          // 原子变量指针
            &start,               // 期望值指针（会被更新为实际值）
            start_new,            // 新值
            memory_order_release, // 成功时的内存序
            memory_order_relaxed  // 失败时的内存序
    ));

    return &box->cell_list[start];
}