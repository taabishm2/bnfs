#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/file.h>
#ifdef HAVE_XATTR
#include <sys/xattr.h>
#endif /* HAVE_XATTR */

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#define ERRNO_NOOP -999
#define ERR_INSERT_TO_QUEUE -1000

#include "unreliablefs_ops.h"

const char *fuse_op_name[] = {
    "getattr",
    "readlink",
    "mknod",
    "mkdir",
    "unlink",
    "rmdir",
    "symlink",
    "rename",
    "link",
    "chmod",
    "chown",
    "truncate",
    "open",
    "read",
    "write",
    "statfs",
    "flush",
    "release",
    "fsync",
#ifdef HAVE_XATTR
    "setxattr",
    "getxattr",
    "listxattr",
    "removexattr",
#endif /* HAVE_XATTR */
    "opendir",
    "readdir",
    "releasedir",
    "fsyncdir",
    "access",
    "creat",
    "ftruncate",
    "fgetattr",
    "lock",
#if !defined(__OpenBSD__)
    "ioctl",
#endif /* __OpenBSD__ */
#ifdef HAVE_FLOCK
    "flock",
#endif /* HAVE_FLOCK */
#ifdef HAVE_FALLOCATE
    "fallocate",
#endif /* HAVE_FALLOCATE */
#ifdef HAVE_UTIMENSAT
    "utimens",
#endif /* HAVE_UTIMENSAT */
    "lstat"
};

struct AFSClient* afsClient;
struct CacheHelper* cacheHelper;

extern int error_inject(const char* path, fuse_op operation);

int unreliable_lstat(const char *path, struct stat *buf)
{
    int ret = error_inject(path, OP_LSTAT);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    const char* cache_path = Cache_path(afsClient, path);
    memset(buf, 0, sizeof(struct stat));
    if (lstat(cache_path, buf) == -1) {
        return -errno;
    }

    return 0;
}

int unreliable_readlink(const char *path, char *buf, size_t bufsiz)
{
    int ret = error_inject(path, OP_READLINK);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    ret = readlink(path, buf, bufsiz);
    if (ret == -1) {
        return -errno;
    }
    buf[ret] = 0;

    return 0;
}

int unreliable_mknod(const char *path, mode_t mode, dev_t dev)
{
    int ret = error_inject(path, OP_MKNOD);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    printf("mknod being called \n");
    const char* cache_path = Cache_path(afsClient, path);
    ret = mknod(cache_path, mode, dev);    
    if (ret == -1) {
        return -errno;
    }

    return 0;
}

int unreliable_unlink(const char *path)
{
    int ret = error_inject(path, OP_UNLINK);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    // Unlink call to server.
	ret = AFS_unlink(afsClient, path);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

int unreliable_symlink(const char *target, const char *linkpath)
{
    int ret = error_inject(target, OP_SYMLINK);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    ret = symlink(target, linkpath);
    if (ret == -1) {
        return -errno;
    }

    return 0;
}

int unreliable_rename(const char *oldpath, const char *newpath)
{
    int ret = error_inject(oldpath, OP_RENAME);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    ret = AFS_rename(afsClient, oldpath, newpath);
    if (ret < 0) {
        return -ret;
    }

    return 0;
}

int unreliable_link(const char *oldpath, const char *newpath)
{
    int ret = error_inject(oldpath, OP_LINK);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    ret = link(oldpath, newpath);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

int unreliable_chmod(const char *path, mode_t mode)
{
    int ret = error_inject(path, OP_CHMOD);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }
    
    const char* cache_path = Cache_path(afsClient, path);
    printf("chmod is being called for %s %s\n", path, cache_path);
    ret = chmod(cache_path, mode);
    if (ret < 0) {
        printf("chmod failed\n");
        return -errno;
    }

    return 0;
}

int unreliable_chown(const char *path, uid_t owner, gid_t group)
{
    int ret = error_inject(path, OP_CHOWN);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    const char* cache_path = Cache_path(afsClient, path);
    ret = chown(cache_path, owner, group);
    if (ret == -1) {
        return -errno;
    }

    return 0;
}

int unreliable_truncate(const char *path, off_t length)
{
    int ret = error_inject(path, OP_TRUNCATE);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    const char* cache_path = Cache_path(afsClient, path);
    ret = truncate(cache_path, length); 
    if (ret == -1) {
        return -errno;
    }

    return 0;
}

int unreliable_open(const char *path, struct fuse_file_info *fi)
{
    printf("IN unreliable_open");
    int ret = error_inject(path, OP_OPEN);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    // Open call to server.
	ret = AFS_open(afsClient, path, fi, false);
    if (ret < 0) {
        return ret;
    }
    fi->fh = ret;

    return 0;
}

int unreliable_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_READ);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    int fd;
    if (fi == NULL) {
        printf("hi1 I'M NUL :(((((())))))");
	    fd = open(path, O_RDONLY);
    } else {
    	fd = fi->fh;
        printf("HURRAYYYY fd ==== %d\n", fd);
    }

    if (fd == -1) {
	return -errno;
    }

    ret = pread(fd, buf, size, offset);
    if (ret == -1) {
        ret = -errno;
    }

    if (fi == NULL) {
	close(fd);
    }

    return ret;
}

// TODO: update this code to write on local cache.
int unreliable_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_WRITE);
     printf("the ret is ==== %d\n", ret);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret == -ERR_INSERT_TO_QUEUE) {
        printf("YAYAYYYYY ADDING TO QQQQQQ \n");
        // populate queue structs for write here.
        QUEUE_addToQueue(afsClient, 2, path, fi, buf, size, offset);
        QUEUE_shuffleQueue(afsClient);
        return size;
    } else if (ret) {
        return ret;
    }

    int fd;
    (void) fi;
    if(fi == NULL) {
    printf("this is wrong\n");
	fd = open(path, O_WRONLY);
    } else {
	fd = fi->fh;
    }

    printf("am writing %lu bytes to path %s\n", size, path);

    if (fd == -1) {
	return -errno;
    }

    ret = pwrite(fd, buf, size, offset);
    if (ret == -1) {
        ret = -errno;
    }

    Cache_markFileDirty(afsClient, path);

    if(fi == NULL) {
        close(fd);
    }

    return ret;
}

int unreliable_statfs(const char *path, struct statvfs *buf)
{
    int ret = error_inject(path, OP_STATFS);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    const char* cache_path = Cache_path(afsClient, path);
    ret = statvfs(cache_path, buf);
    if (ret == -1) {
        return -errno;
    }

    return 0;
}

int unreliable_flush(const char *path, struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_FLUSH);
    printf("the ret is ==== %d\n", ret);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret == -ERR_INSERT_TO_QUEUE) {
        printf("YAYAYYYYY ADDING TO QQQQQQ \n");
        // populate the queue
        QUEUE_addToQueue(afsClient, 1, path, fi, NULL, 0, 0);
        QUEUE_shuffleQueue(afsClient);
        return 0;
    } else if (ret) {
        return ret;
    }

    printf("flushing fd %lu for %s\n", fi-> fh, path);
    ret = close(dup(fi->fh));
    if (ret == -1) {
        return -errno;
    }

	ret = AFS_flush(afsClient, path, fi);

    return 0;
}

int unreliable_release(const char *path, struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_RELEASE);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    // Close local cache file descriptor.
    ret = close(fi->fh);
    if (ret == -1) {
        return -errno;
    }

    // Flush changes from local file to afs.
	//ret = AFS_flush(afsClient, path, fi);

    return 0;
}

int unreliable_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_FSYNC);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    if (datasync) {
        ret = fdatasync(fi->fh);
        if (ret == -1) {
            return -errno;
        }
    } else {
        ret = fsync(fi->fh);
        if (ret == -1) {
            return -errno;
        }
    }

    return 0;
}

#ifdef HAVE_XATTR
int unreliable_setxattr(const char *path, const char *name,
                        const char *value, size_t size, int flags)
{
    int ret = error_inject(path, OP_SETXATTR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

 const char* cache_path = Cache_path(afsClient, path);
 printf("setxattr being called\n");

#ifdef __APPLE__
    ret = setxattr(cache_path, name, value, size, 0, flags);
#else
    ret = setxattr(cache_path, name, value, size, flags);
#endif /* __APPLE__ */
    if (ret == -1) {
        return -errno;
    }

    return 0;
}

int unreliable_getxattr(const char *path, const char *name,
                        char *value, size_t size)
{
    int ret = error_inject(path, OP_GETXATTR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    const char* cache_path = Cache_path(afsClient, path);

    #ifdef __APPLE__
        ret = getxattr(cache_path, name, value, size, 0, XATTR_NOFOLLOW);
    #else
        ret = getxattr(cache_path, name, value, size);
    #endif /* __APPLE__ */
        if (ret == -1) {
            return -errno;
        }
    
    return 0;
}

int unreliable_listxattr(const char *path, char *list, size_t size)
{
    int ret = error_inject(path, OP_LISTXATTR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

 const char* cache_path = Cache_path(afsClient, path);

#ifdef __APPLE__
    ret = listxattr(cache_path, list, size, XATTR_NOFOLLOW);
#else
    ret = listxattr(cache_path, list, size);
#endif /* __APPLE__ */
    if (ret == -1) {
        return -errno;
    }
    
    return ret;
}

int unreliable_removexattr(const char *path, const char *name)
{
    int ret = error_inject(path, OP_REMOVEXATTR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

 const char* cache_path = Cache_path(afsClient, path);

#ifdef __APPLE__
    ret = removexattr(cache_path, name, XATTR_NOFOLLOW);
#else
    ret = removexattr(cache_path, name);
#endif /* __APPLE__ */
    if (ret == -1) {
        return -errno;
    }
    
    return 0;    
}
#endif /* HAVE_XATTR */

int unreliable_opendir(const char *path, struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_OPENDIR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    // DIR *dir = opendir(path);

    // if (!dir) {
    //     return -errno;
    // }
    // fi->fh = (int64_t) dir;

    return 0;    
}

int unreliable_getattr(const char *path, struct stat *buf)
{
    int ret = error_inject(path, OP_GETATTR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }
    
	return AFS_getAttr(afsClient, path, buf);
}

int unreliable_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_READDIR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

	return AFS_readDir(afsClient, path, buf, filler);
}

int unreliable_mkdir(const char *path, mode_t mode)
{
    int ret = error_inject(path, OP_MKDIR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

	return AFS_mkdir(afsClient, path, mode);
}

int unreliable_rmdir(const char *path)
{
    int ret = error_inject(path, OP_RMDIR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    return AFS_rmdir(afsClient, path);
}

int unreliable_releasedir(const char *path, struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_RELEASEDIR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    DIR *dir = (DIR *) fi->fh;

    ret = closedir(dir);
    if (ret == -1) {
        return -errno;
    }
    
    return 0;    
}

int unreliable_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_FSYNCDIR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        return -errno;
    }

    if (datasync) {
        ret = fdatasync(dirfd(dir));
        if (ret == -1) {
            return -errno;
        }
    } else {
        ret = fsync(dirfd(dir));
        if (ret == -1) {
            return -errno;
        }
    }
    closedir(dir);

    return 0;
}

void *unreliable_init(struct fuse_conn_info *conn)
{
    return NULL;
}

void unreliable_destroy(void *private_data)
{

}

int unreliable_access(const char *path, int mode)
{
    int ret = error_inject(path, OP_ACCESS);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

	return AFS_access(afsClient, path, mode);
}

int unreliable_create(const char *path, mode_t mode,
                      struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_CREAT);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }
    
    // Open call to server.
	ret = AFS_open(afsClient, path, fi, true);
    if (ret < 0) {
        return ret;
    }
    fi->fh = ret;

    return 0;
}

int unreliable_ftruncate(const char *path, off_t length,
                         struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_FTRUNCATE);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    ret = truncate(path, length);
    if (ret == -1) {
        return -errno;
    }
    
    return 0;    
}

int unreliable_fgetattr(const char *path, struct stat *buf,
                        struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_FGETATTR);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    ret = fstat((int) fi->fh, buf);
    if (ret == -1) {
        return -errno;
    }
    
    return 0;    
}

int unreliable_lock(const char *path, struct fuse_file_info *fi, int cmd,
                    struct flock *fl)
{
    int ret = error_inject(path, OP_LOCK);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    ret = fcntl((int) fi->fh, cmd, fl);
    if (ret == -1) {
        return -errno;
    }

    return 0;
}

#if !defined(__OpenBSD__)
int unreliable_ioctl(const char *path, int cmd, void *arg,
                     struct fuse_file_info *fi,
                     unsigned int flags, void *data)
{
    int ret = error_inject(path, OP_IOCTL);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    ret = ioctl(fi->fh, cmd, arg);
    if (ret == -1) {
        return -errno;
    }
    
    return ret;
}
#endif /* __OpenBSD__ */

#ifdef HAVE_FLOCK
int unreliable_flock(const char *path, struct fuse_file_info *fi, int op)
{
    int ret = error_inject(path, OP_FLOCK);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    ret = flock(((int) fi->fh), op);
    if (ret == -1) {
        return -errno;
    }
    
    return 0;    
}
#endif /* HAVE_FLOCK */

#ifdef HAVE_FALLOCATE
int unreliable_fallocate(const char *path, int mode,
                         off_t offset, off_t len,
                         struct fuse_file_info *fi)
{
    int ret = error_inject(path, OP_FALLOCATE);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    int fd;
    (void) fi;

    if (mode) {
	return -EOPNOTSUPP;
    }

    if(fi == NULL) {
	fd = open(path, O_WRONLY);
    } else {
	fd = fi->fh;
    }

    if (fd == -1) {
	return -errno;
    }

    ret = fallocate((int) fi->fh, mode, offset, len);
    if (ret == -1) {
        return -errno;
    }

    if(fi == NULL) {
	close(fd);
    }
    
    return 0;    
}
#endif /* HAVE_FALLOCATE */

#ifdef HAVE_UTIMENSAT
int unreliable_utimens(const char *path, const struct timespec ts[2])
{
    int ret = error_inject(path, OP_UTIMENS);
    if (ret == -ERRNO_NOOP) {
        return 0;
    } else if (ret) {
        return ret;
    }

    /* don't use utime/utimes since they follow symlinks */
    // const char* cache_path = Cache_path(afsClient, path);
    // printf("\n\npath is %s, cache path is %s\n", path, cache_path);
    // ret = utimensat(0, cache_path, ts, AT_SYMLINK_NOFOLLOW);
    // if (ret == -1) {
    //     return -errno;
    // }

    return 0;
}
#endif /* HAVE_UTIMENSAT */
