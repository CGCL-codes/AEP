#include <unistd.h>
#include "syscall.h"
#include <directfs.h>
#include <string.h>

int trap_count = 0;

ssize_t read(int fd, void *buf, size_t count)
{
	return syscall_cp(SYS_read, fd, buf, count);
}

ssize_t pv_read(int fd, int group_fd, void *buf, off_t start, size_t count)
{
	struct DirectNode* node = find_file_node(group_fd);

	if (!is_pv_addr(node)){
		DEBUG_PRINT("musl lib read ERROR:  cannot find DirectNode! fd: %d, group_fd: %d.\n", fd, group_fd);
		return 0;
	}

	DEBUG_PRINT("musl lib: read direct file, fd: %d, buffer addr: %lx, count: %lx.\n", fd, (unsigned long)buf, (unsigned long)count);

	node_lock(node->lock);
	off_t s_off = start;
	DEBUG_PRINT("musl lib: read direct file, s_off %lx.\n", s_off);
	off_t e_off = (s_off + count > node->true_length) ? node->true_length : (s_off + count);
	size_t copyed = 0;
	size_t stride = 0; // stride of appended page
	size_t avail_len_fixed = PAGE_ALIGN_UP(node->length) - s_off;
	size_t avail_len_all = node->true_length - s_off;

	count = avail_len_all < count ? avail_len_all : count;

	if (s_off >= node->true_length) {
		//printf("musl lib: read direct file exceed! fd: %d, file_offset %lx, true_length: %lx, start: %lx, count: %lx.\n", 
		//									fd, 
		//									(unsigned long)s_off, 
		//									(unsigned long)node->true_length,
		//									(unsigned long) start,
		//									(unsigned long) count);
		node_unlock(node->lock);
		return 0;
	}

	if (s_off < PAGE_ALIGN_UP(node->length)) {
		if (test_file_in_pv(node, s_off, count) == 0) 
		{
			DEBUG_PRINT("musl lib: part of data not in pagecache, trap into kernel. curr offset: %lx\n", (unsigned long)s_off);
			trap_count++;

			// flush log, set file offset into kernel file struct and read data from kernel
			copyed = syscall_cp(SYS_DIRECTFS_READ, fd, buf, count, start); 

			if (copyed != e_off - s_off)
				printf("Error: read data too less, read %ld, expect %ld\n", copyed, e_off - s_off);
		}
		else 
		{
			DEBUG_PRINT("musl lib: part of data in pagecache, read from pagecache.\n");

			if (e_off > PAGE_ALIGN_UP(node->length)) {
				copyed = PAGE_ALIGN_UP(node->length) - s_off;
				memcpy(buf, (char*)(groupfd_addr[node->group_fd] + s_off), copyed);
				s_off = PAGE_ALIGN_UP(node->length);
			}
			else {
				copyed = e_off - s_off;
				memcpy(buf, (char*)(groupfd_addr[node->group_fd] + s_off), copyed);
			}
		}
	}

	count -= copyed;
	s_off += copyed;

	// read from appended page
	if (count > 0) {
		DEBUG_PRINT("musl lib: read from appended page.\n");

		struct DirectFSPage *page = node->seq_page_list, *pre_page = page;
		stride = (s_off - PAGE_ALIGN_UP(node->length)) ;
		while (stride > 0) {
			stride -= page->length;
			pre_page = page;
			page = page->seq_next;
		}

		// reach the copyed page
		page = pre_page;

		while (count > 0 && page) {
			off_t page_off = s_off - page->index * PAGE_SIZE;
			off_t page_left = page->length - page_off;
			if (count > page_left) {
				memcpy((char*)buf+copyed, (char*)(page->addr + page_off), page_left);
				copyed += page_left;
				count -= page_left;
				s_off += page_left;
			}
			else {
				memcpy((char*)buf+copyed, (char*)(page->addr + page_off), count);
				copyed += count;
				count = 0;
			}
			page = page->seq_next;
		}

	}

	node_unlock(node->lock);

	//DEBUG_PRINT("musl lib: read end, return: %lx\n", (unsigned long)copyed);

	return copyed;
}

void print_count(int fd)
{
	printf("musl lib: trap_count: %d\n", trap_count);
	return;
}
