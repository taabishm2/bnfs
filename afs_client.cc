#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
#define _XOPEN_SOURCE 700
#endif


#include <sstream>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <chrono>

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

// openssl library for SHA
#include <openssl/sha.h>

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "afs.grpc.pb.h"

using afs::BaseResponse;
using afs::FileServer;
using afs::OpenResp;
using afs::OpenReq;
using afs::SimplePathRequest;
using afs::StatResponse;
using afs::ReadDirResponse;
using afs::ReadDirResponse;
using afs::SimplePathRequest;
using afs::StatResponse;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using namespace std;

#define AFS_CLIENT ((struct AFSClient *) fuse_get_context()->private_data)
#define LOCAL_CREAT_FILE "locally_generated_file"


class AFSClient
{
public:
	AFSClient(shared_ptr<Channel> channel, string cache_root)
		: stub_(FileServer::NewStub(channel)), cache_root(cache_root) {}

    int Open(const char *path, struct fuse_file_info *fi) {
        std::cout << "Open: start\n";
        std::string path_str(path);
        OpenReq request;
        request.set_path(path_str);
        request.set_flag(fi->fh);

        // for getattr()
        SimplePathRequest filepath;
        filepath.set_path(path_str);
        ClientContext context;
        StatResponse getattr_reply;
    
        // TODO(Sweksha) : Use cache based on GetAttr() result.
            stub_->GetAttr(&context, filepath, &getattr_reply);
        
        // Get file from server.
        std::cout << "Open: Opening file from server." << std::endl;
        
        OpenResp reply;


        std::unique_ptr<grpc::ClientReader<OpenResp> > reader(
            stub_->Open(&context, request));

        // Read the stream from server. 
        if (!reader->Read(&reply)) {
            std::cout << "[err] Open: failed to download file: " << path_str
                      << " from server." << std::endl;
            return -EIO;
        }

        if (reply.file_exists()) {
            // open file with O_TRUNC
            std::ofstream ofile(cachepath(path),
                std::ios::binary | std::ios::out | std::ios::trunc);
            // TODO: check failure
            ofile << reply.buf();
            while (reader->Read(&reply)) {
                ofile << reply.buf();
            }

            Status status = reader->Finish();
            if (!status.ok()) {
                std::cout << "[err] Open: failed to download from server." << std::endl;
                return -EIO;
            }
            ofile.close(); // the cache is persisted

            std::cout << "Open: finish download from server\n";
        }
        else {
            std::cout << "Open: created a new file\n";
            close(creat(cachepath(path).c_str(), 00777));
        }

        // give user the file
        fi->fh = open(cachepath(path).c_str(), fi->flags);
        if (fi->fh < 0) {
            std::cout << "[err] Open: error open downloaded cache file.\n";
            return -errno;
        }

        return 0;   
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

		cout << "Client readDir called" << endl;

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

    std::string cachepath(const char* rel_path) {
        return cache_root + hashpath(rel_path);
    }

    std::string cachepath(std::string cache_root, const char* rel_path) {
        // local cached filename is SHA-256 hash of the path
        // referencing https://stackoverflow.com/questions/2262386/generate-sha256-with-openssl-and-c
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, rel_path, strlen(rel_path));
        SHA256_Final(hash, &sha256);
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << ((int)hash[i]);
        }
        std::cout << "hashed hex string is " << ss.str() << std::endl; // debug
        return cache_root + ss.str();
    }

    std::string hashpath(const char* rel_path) {
        // local cached filename is SHA-256 hash of the path
        // referencing https://stackoverflow.com/questions/2262386/generate-sha256-with-openssl-and-c
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, rel_path, strlen(rel_path));
        SHA256_Final(hash, &sha256);
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << ((int)hash[i]);
        }
        return ss.str();
    }

	unique_ptr<FileServer::Stub> stub_;
	string cache_root;
};

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

// static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
// 					struct fuse_file_info *fi)
// {
// 	// Call AFS server.
// 	string path_str = path;
// 	AFS_CLIENT->Open(path_str);

// 	res = open(path, fi->flags);
// 	if (res == -1)
// 		return -errno;

// 	close(res);
// 	return 0;
// }

// 	// Call AFS server.
// 	string path_str = path;
// 	AFS_CLIENT-> Read(path_str);

// 	(void)fi;
// 	fd = open(path, O_RDONLY);
// 	if (fd == -1)
// 		return -errno;

// 	res = pread(fd, buf, size, offset);
// 	if (res == -1)
// 		res = -errno;

// 	close(fd);
// 	return res;
// }

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
	//.open = bnfs_open, // TODO: commented this because error in build
	// .read = xmp_read,
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
	// .access = xmp_access,
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
