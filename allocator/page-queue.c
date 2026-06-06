#include <stdio.h>
#include "cxlmalloc-nodelevel.h"
#include "malloc-clientlevel.h"

void cxl_page_queue_push(page_queue_t *p, page_t* page)
{
    page->next = p->first;
    page->prev = 0;
    page_t* first_page = p->first;
    page_t* last_page = p->last;
    
    if(first_page != NULL)
    {
        first_page->prev = page;
        p->first = page;
    }
    else
    {
        p->first = page;
        p->last = page;
    }
}

void cxl_page_queue_remove(page_queue_t *p, page_t* page)
{
    page_t* prev_page = page->prev;
    page_t* next_page = page->next;
    if(prev_page != NULL) 
    {
        prev_page->next = page->next;
    }
    if(next_page != NULL) next_page->prev = page->prev;
    if(page == p->last) 
    {
        p->last = page->prev;
    }
    if(page == p->first) 
    {
        p->first = page->next;
    }
    page->next = 0;
    page->prev = 0;
}