#include <fcntl.h>
#include <stdarg.h>
#include "syscall.h"
#include <directfs.h>

int open(const char *filename, int flags, ...)
{
	mode_t mode = 0;

	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}

	int fd = __sys_open_cp(filename, flags, mode);
	if (fd>=0 && (flags & O_CLOEXEC))
		__syscall(SYS_fcntl, fd, F_SETFD, FD_CLOEXEC);

	return __syscall_ret(fd);
}

unsigned long pv_open(const char *filename, int flags, ...)
{
	mode_t mode = 0;
	int fd = 0;
	int group_fd = 0;

	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}

	// int _fd = __sys_open_cp(filename, flags, mode);
	unsigned long _fd = __syscall(SYS_open, filename, flags, mode);
	printf("musl lib: open, filename: %s, flags: %d, fd: %lx\n", filename, flags, (unsigned long)_fd);

	open_direct_pre_handle(_fd, flags, &fd, &group_fd);  //create struct DirectNode

	if (fd>=0 && (flags & O_CLOEXEC))
		__syscall(SYS_fcntl, fd, F_SETFD, FD_CLOEXEC);

	printf("musl lib: open, end, fd: %d, group_fd: %d\n", fd, group_fd);
	
	if (group_fd > 0)
		return _fd;
	
	printf("musl lib: open, end 2, fd: %d\n", fd);
	
	return fd;
	//__syscall_ret(fd);
}
