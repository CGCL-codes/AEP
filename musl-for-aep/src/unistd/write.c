#include <unistd.h>
#include "syscall.h"
#include <directfs.h>
#include <assert.h>
#include <string.h>


#define __NR_mutant_ctl2 447

#define MUTANT_TEST_PAGE_ADDR_MAP 5

ssize_t write(int fd, const void *buf, size_t count)
{
	return syscall_cp(SYS_write, fd, buf, count);
}

ssize_t pv_write(int fd, int group_fd, const void *buf, off_t start, size_t count, int append, unsigned long struct_file_addr)
{
	struct DirectNode* node = find_file_node(group_fd);

	if (!is_pv_addr(node)){
		DEBUG_PRINT("musl lib read ERROR:  cannot find DirectNode! fd: %d, group_fd: %d.\n", fd, group_fd);
		return 0;
	}

	//printf("musl lib: write direct file, fd: %d, buffer addr: %lx, start: %lx, count: %lx.\n", fd, (unsigned long)buf, (unsigned long)start, (unsigned long)count);

	node_lock(node->lock);
	off_t s_off = append ? node->true_length : start;
	DEBUG_PRINT("musl lib: write direct file, s_off %lx.\n", s_off);
	size_t written = 0;
	size_t stride = 0; // stride of appended page		

	if (s_off < PAGE_ALIGN_UP(node->length)) {
		size_t len = PAGE_ALIGN_UP(node->length) - s_off;
		written = MIN(len, count);

		if (test_file_in_pv(node, s_off, count) == 0) 
		{
			DEBUG_PRINT("musl lib: wirte part of data not in pagecache, trap into kernel.\n");

			// flush log, set file offset into kernel file struct and write data into kernel
			size_t ret = syscall_cp(SYS_DIRECTFS_WRITE, fd, buf, written, s_off); 
			if (ret != written) {
				DEBUG_PRINT("Error: write data too less, write %ld, expect %ld\n", ret, written);
			}
		}
		else 
		{
			DEBUG_PRINT("musl lib: write part of data in pagecache, write into pagecache.\n");

			memcpy((char*)(groupfd_addr[node->group_fd] + s_off), buf, written);

			append_log_entry(node->group_fd, 
								s_off, 
								written, 
								false, 
								(unsigned long)(groupfd_addr[node->group_fd] + s_off),
								struct_file_addr);
		}

		count -= written;
		s_off += written;
	}

	// write into appended page
	if (count > 0) {
		//DEBUG_PRINT("musl lib: write into appended page.\n");

		struct DirectFSPage *page = node->seq_page_list, *pre_page = page;
		stride = (s_off - PAGE_ALIGN_UP(node->length)); // stride from end of fixed part of file
		while (stride > 0 && page) {
			stride -= page->length;
			pre_page = page;
			page = page->seq_next;
		}

		// reach the copyed page
		page = pre_page;

		while (count > 0) {
			if (page == NULL) {
				DEBUG_PRINT("musl lib: write into new malloc appended page.\n");

				// create new page
				page = (struct DirectFSPage*)malloc(sizeof(struct DirectFSPage));

				if (!page) {
					printf("musl lib: write malloc failed!\n");
					node_unlock(node->lock);

					return -ENOMEM; 
				}
				// page->addr = (unsigned long)malloc(PAGE_ALIGN_UP(count));
				page->addr = alloc_pages(PAGE_ALIGN_UP(count) >> PAGE_SHIFT);
				page->length = PAGE_ALIGN_UP(count);
				page->index = OFFSET_TO_INDEX(s_off);
				page->seq_next = NULL;

				// insert into page list
				if (node->seq_page_list == NULL) {
					DEBUG_PRINT("no append page, inerst new page to head\n");
					node->seq_page_list = page;
				}
				else {
					DEBUG_PRINT("insert new page to tail\n");
					pre_page->seq_next = page;
				}

				append_log_entry(node->group_fd, 
								s_off, 
								count, 
								true, 
								page->addr, 
								struct_file_addr);
				memcpy((char*)(page->addr), (char*)buf+written, count);

				//printf("musl lib: s_off %ld, page->addr %lx, page->length %ld, page->index %d\n", 
				//								s_off, page->addr, page->length, page->index);
				written += count;
				s_off += count;

				break;
			}
			//DEBUG_PRINT("musl lib: write into old malloc appended page.\n");

			off_t page_off = s_off - page->index * PAGE_SIZE;
			off_t page_left = page->length - page_off;
			size_t length = MIN(page_left, count);

			append_log_entry(node->group_fd, 
								s_off,
								length, 
								false, 
								page->addr+page_off,
								struct_file_addr);
			memcpy((char*)(page->addr + page_off), (char*)buf+written, length);
			// syscall_cp(__NR_mutant_ctl2, MUTANT_TEST_PAGE_ADDR_MAP, (unsigned long)(page->addr + page_off), length);
			written += length;
			count -= length;
			s_off += length;

			//printf("musl lib: s_off %ld, page->addr %lx, page->length %ld, page->index %d\n", 
			//								s_off, page->addr, page->length, page->index);
				// printf("musl lib: page->addr+page_off %lx, data %s\n", page->addr+page_off, (char*)(page->addr + page_off));
			page = page->seq_next;
		}
	}

	if (s_off > node->true_length) 
		node->true_length = s_off;
		
	node_unlock(node->lock);

	return written;
}
