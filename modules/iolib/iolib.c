#include <sys/filedesc.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/ktrace.h>

#include "iolib.h"

int
kclose(int fd)
{
	if (fd_getfile(fd) != NULL)
		return fd_close(fd);
	return -1;
} 

int
kopen(const char *path, int flags, int mode, int *fd)
{
	return fd_open(path, flags, mode, fd);
}

struct file*
kfopen(const char *path, int flags, int mode, int *fd)
{
	int err;
	struct file *fp;

	if ((err = kopen(path, flags, mode, fd)) != 0)
		return NULL;
	fp = fd_getfile(*fd);
	fd_putfile(*fd);
	
    return fp;
}
 
static int
dofileread_(int fd, struct file *fp, void *buf, size_t nbyte,
	off_t *offset, int flags, register_t *retval)
{
	struct iovec aiov;
	struct uio auio;
	size_t cnt;
	int error;

	aiov.iov_base = (void *)buf;
	aiov.iov_len = nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = nbyte;
	auio.uio_rw = UIO_READ;
	auio.uio_vmspace = vmspace_kernel();

	/*
	 * Reads return ssize_t because -1 is returned on error.  Therefore
	 * we must restrict the length to SSIZE_MAX to avoid garbage return
	 * values.
	 */
	if (auio.uio_resid > SSIZE_MAX) {
		error = EINVAL;
		goto out;
	}

	cnt = auio.uio_resid;
	error = (*fp->f_ops->fo_read)(fp, offset, &auio, fp->f_cred, flags);
	if (error)
		if (auio.uio_resid != cnt && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
	cnt -= auio.uio_resid;
	ktrgenio(fd, UIO_READ, buf, cnt, error);
	*retval = cnt;
 out:
	fd_putfile(fd);
	return (error);
} 
 
int
kread(int fd, void *buf, size_t nbyte, size_t *nr)
{
	file_t *fp;

	if ((fp = fd_getfile(fd)) == NULL) 
		return (EBADF);

	//XXX why, god?
	/*if ((fp->f_flag & FREAD) == 0) {
		printf("bad f2");
		fd_putfile(fd);
		return (EBADF);
	} */

	return dofileread_(fd, fp, buf, nbyte, &fp->f_offset, 
						FOF_UPDATE_OFFSET, nr);
} 

static int
dofilewrite_(int fd, struct file *fp, const void *buf,
	size_t nbyte, off_t *offset, int flags, register_t *retval)
{
	struct iovec aiov;
	struct uio auio;
	size_t cnt;
	int error;

	aiov.iov_base = __UNCONST(buf);		/* XXXUNCONST kills const */
	aiov.iov_len = nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = nbyte;
	auio.uio_rw = UIO_WRITE;
	auio.uio_vmspace = vmspace_kernel();

	/*
	 * Writes return ssize_t because -1 is returned on error.  Therefore
	 * we must restrict the length to SSIZE_MAX to avoid garbage return
	 * values.
	 */
	if (auio.uio_resid > SSIZE_MAX) {
		error = EINVAL;
		goto out;
	}

	cnt = auio.uio_resid;
	error = (*fp->f_ops->fo_write)(fp, offset, &auio, fp->f_cred, flags);
	if (error) {
		if (auio.uio_resid != cnt && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		if (error == EPIPE && !(fp->f_flag & FNOSIGPIPE)) {
			mutex_enter(proc_lock);
			psignal(curproc, SIGPIPE);
			mutex_exit(proc_lock);
		}
	}
	cnt -= auio.uio_resid;
	ktrgenio(fd, UIO_WRITE, buf, cnt, error);
	*retval = cnt;
 out:
	fd_putfile(fd);
	return (error);
} 
 

int
kwrite(int fd, void *buf, size_t nbyte, size_t *nr)
{
	file_t *fp;

	if ((fp = fd_getfile(fd)) == NULL)
		return (EBADF);

	if ((fp->f_flag & FWRITE) == 0) {
		fd_putfile(fd);
		return (EBADF);
	}

	return dofilewrite_(fd, fp, buf, nbyte, &fp->f_offset, 
						FOF_UPDATE_OFFSET, nr);
}  