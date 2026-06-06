#include <unistd.h>
#include "syscall.h"
#include <directfs.h>
#include <assert.h>

off_t __lseek(int fd, off_t offset, int whence)
{
#ifdef SYS__llseek
	off_t result;
	return syscall(SYS__llseek, fd, offset>>32, offset, &result, whence) ? -1 : result;
#else
	//printf("musl lib: enter lseek syscall, fd: %d, offset: %lx, whence: %d\n", fd, offset, whence);
	return syscall(SYS_lseek, fd, offset, whence);
#endif
}

weak_alias(__lseek, lseek);


off_t pv_lseek(int fd, int group_fd, off_t offset, int whence)
{
	DEBUG_PRINT("musl lib: lseek direct file, fd: %d.\n", fd);
	struct DirectNode* node = find_file_node(group_fd);

	if (!is_pv_addr(node)){
		DEBUG_PRINT("musl lib lseek ERROR:  cannot find DirectNode! fd: %d, group_fd: %d.\n", fd, group_fd);
		return -1;
	}

	return node->true_length;
}
