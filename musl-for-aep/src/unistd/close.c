#include <unistd.h>
#include <errno.h>
#include "aio_impl.h"
#include "syscall.h"
#include <directfs.h>

static int dummy(int fd)
{
	return fd;
}

weak_alias(dummy, __aio_close);

int close(int fd)
{
	fd = __aio_close(fd);
	int r = __syscall_cp(SYS_close, fd);
	if (r == -EINTR) r = 0;

	return __syscall_ret(r);
}

int pv_close(int fd, int group_fd)
{
	put_DirectNode(group_fd);

	fd = __aio_close(fd);
	int r = __syscall_cp(SYS_close, fd);
	if (r == -EINTR) r = 0;

	return __syscall_ret(r);
}
