#include "stdio_impl.h"
#include <sys/uio.h>
#include <directfs.h>
#include <string.h>
#include <unistd.h>

size_t __stdio_read(FILE *f, unsigned char *buf, size_t len)
{
	struct iovec iov[2] = {
		{ .iov_base = buf, .iov_len = len - !!f->buf_size },
		{ .iov_base = f->buf, .iov_len = f->buf_size }
	};
	ssize_t cnt;

	if (iov[0].iov_len) {
		printf("musl lib: stdio_read, sys_readv\n");

		cnt = read(f->fd, iov[0].iov_base, iov[0].iov_len);
		// cnt += read(f->fd, iov[1].iov_base, iov[1].iov_len);
		// 不需要去预读取数据，因为需要目前文件系统中已有的数据也只是需要memcpy
	}
	else {
		printf("musl lib: stdio_read, sys_read\n");
		cnt = read(f->fd, iov[1].iov_base, iov[1].iov_len);
	}
	// cnt = iov[0].iov_len ? syscall(SYS_readv, f->fd, iov, 2)
	// 	: syscall(SYS_read, f->fd, iov[1].iov_base, iov[1].iov_len);
	if (cnt <= 0) {
		f->flags |= cnt ? F_ERR : F_EOF;
		return 0;
	}
	if (cnt <= iov[0].iov_len) return cnt;
	cnt -= iov[0].iov_len;
	f->rpos = f->buf;
	f->rend = f->buf + cnt;
	if (f->buf_size) buf[len-1] = *f->rpos++;
	return len;
}
