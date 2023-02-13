#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "afs.grpc.pb.h"

using afs::BaseResponse;
using afs::FileServer;
using afs::OpenReq;
using afs::OpenResp;
using afs::ReadDirResponse;
using afs::SimplePathRequest;
using afs::StatResponse;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using namespace std;

#define AFS_CLIENT ((struct AFSClient *)fuse_get_context()->private_data)

class AFSClient
{
public:
	AFSClient(shared_ptr<Channel> channel, string cache_root)
		: stub_(FileServer::NewStub(channel)), cache_root(cache_root) {}

	string Open(string &fileName)
	{

		OpenReq request;
		request.set_path(fileName);

		OpenResp reply;
		ClientContext context;

		cout << "client open called" << endl;

		Status status = stub_->Open(&context, request, &reply);

		if (status.ok())
		{
			return "success: received " + to_string(reply.err());
		}
		else
		{
			cout << status.error_code() << ": " << status.error_message()
				 << endl;
			return "RPC failed";
		}
	}

	Status getAttr(string &fileName, struct stat *stbuf, int *res)
	{

		SimplePathRequest request;
		request.set_path(fileName);

		StatResponse reply;
		ClientContext context;

		cout << "Client getAttr called" << endl;

		Status status = stub_->GetAttr(&context, request, &reply);

		if (status.ok())
		{
			stbuf->st_dev = reply.dev();
			stbuf->st_ino = reply.ino();
			stbuf->st_mode = reply.mode();
			stbuf->st_nlink = reply.nlink();
			stbuf->st_rdev = reply.rdev();
			stbuf->st_size = reply.size();
			stbuf->st_blksize = reply.blksize();
			stbuf->st_blocks = reply.blocks();
			stbuf->st_atime = reply.atime();
			stbuf->st_mtime = reply.mtime();
			stbuf->st_ctime = reply.ctime();
			*res = reply.baseresponse().errorcode();
		}

		return status;
	}

	Status readDir(string &fileName, ReadDirResponse *reply, int *res)
	{

		SimplePathRequest request;
		request.set_path(fileName);

		ClientContext context;
		Status status = stub_->ReadDir(&context, request, reply);
		*res = reply->baseresponse().errorcode();

		return status;
	}

	Status mkdir(string &fileName, BaseResponse *reply, int *res)
	{

		SimplePathRequest request;
		request.set_path(fileName);

		ClientContext context;
		Status status = stub_->Mkdir(&context, request, reply);
		*res = reply->errorcode();

		return status;
	}

	unique_ptr<FileServer::Stub> stub_;
	string cache_root;
};

static int xmp_getattr(const char *path, struct stat *stbuf)
{
	int res;

	// Call AFS server.
	string path_str = path;
	AFS_CLIENT->Open(path_str);

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;

	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

static int bnfs_getattr(const char *path, struct stat *stbuf)
{
	cout << "************* getattr" << endl;

	int res;
	string path_str = path;
	Status status = AFS_CLIENT->getAttr(path_str, stbuf, &res);

	return res;
}

static int bnfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
						off_t offset, struct fuse_file_info *fi)
{
	cout << "************* readdir" << endl;

	int res;
	ReadDirResponse reply;
	string path_str = path;

	Status status = AFS_CLIENT->readDir(path_str, &reply, &res);
	
	if (!status.ok() || res == -1)
		return -errno;

	for (int i = 0; i < reply.dirname_size(); i++)
		if (filler(buf, reply.dirname(i).c_str(), NULL, 0) != 0)
			return -ENOMEM;

	return 0;
}

static int bnfs_mkdir(const char *path, mode_t mode)
{
	cout << "************* mkdir" << endl;
	
	int res;
	string path_str = path;
	BaseResponse reply;
	Status status = AFS_CLIENT->mkdir(path_str, &reply, &res);

	if (!status.ok())
		return -errno;

	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	cout << "************* mknod" << endl;
	int res;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode))
	{
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	}
	else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	cout << "************* unlink" << endl;
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	cout << "************* rmdir" << endl;
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	cout << "************* symlink" << endl;
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to)
{
	cout << "************* renam" << endl;
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	cout << "************* link" << endl;
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	cout << "************* chmod" << endl;
	int res;

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	cout << "************* chown" << endl;
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
	cout << "************* truncate" << endl;
	int res;

	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
	cout << "************* utimens" << endl;
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	cout << "************* open" << endl;
	int res;

	// Call AFS server.
	string path_str = path;
	AFS_CLIENT->Open(path_str);

	res = open(path, fi->flags);
	if (res == -1)
		return -errno;

	close(res);
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
					struct fuse_file_info *fi)
{
	cout << "************* read" << endl;
	int fd;
	int res;

	(void)fi;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -errno;

	res = pread(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
					 off_t offset, struct fuse_file_info *fi)
{
	cout << "************* write" << endl;
	int fd;
	int res;

	(void)fi;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	cout << "************* statfs" << endl;
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	cout << "************* release" << endl;
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void)path;
	(void)fi;
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
					 struct fuse_file_info *fi)
{
	cout << "************* fsync" << endl;
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void)path;
	(void)isdatasync;
	(void)fi;
	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
						 off_t offset, off_t length, struct fuse_file_info *fi)
{
	cout << "************* fallocate" << endl;
	int fd;
	int res;

	(void)fi;

	if (mode)
		return -EOPNOTSUPP;

	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	close(fd);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
						size_t size, int flags)
{
	cout << "************* setxattr" << endl;
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
						size_t size)
{
	cout << "************* lgetxattr" << endl;
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	cout << "************* listxattr" << endl;
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	cout << "************* removexattr" << endl;
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper = {
	.getattr = bnfs_getattr,
	.readlink = xmp_readlink,
	.mknod = xmp_mknod,
	.mkdir = bnfs_mkdir,
	.unlink = xmp_unlink,
	.rmdir = xmp_rmdir,
	.symlink = xmp_symlink,
	.rename = xmp_rename,
	.link = xmp_link,
	.chmod = xmp_chmod,
	.chown = xmp_chown,
	.truncate = xmp_truncate,
	.open = xmp_open,
	.read = xmp_read,
	.write = xmp_write,
	.statfs = xmp_statfs,
	.release = xmp_release,
	.fsync = xmp_fsync,
#ifdef HAVE_SETXATTR
	.setxattr = xmp_setxattr,
	.getxattr = xmp_getxattr,
	.listxattr = xmp_listxattr,
	.removexattr = xmp_removexattr,
#endif
	.readdir = bnfs_readdir,
	.access = xmp_access,
#ifdef HAVE_UTIMENSAT
	.utimens = xmp_utimens,
#endif
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate = xmp_fallocate,
#endif

};

int main(int argc, char *argv[])
{
	umask(0);

	AFSClient *afsClient = new AFSClient(
		grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()),
		"/");
	cout << "Initialized AFS client" << endl;

	return fuse_main(argc, argv, &xmp_oper, afsClient);
}
