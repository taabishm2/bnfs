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

using afs::FileServer;
using afs::OpenResp;
using afs::SimplePathRequest;
using afs::StatResponse;
using afs::ReadDirResponse;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using namespace std;

#define AFS_CLIENT ((struct AFSClient *) fuse_get_context()->private_data)
#define AFS_DATA ((struct afs_data_t *) fuse_get_context()->private_data)
#define LOCAL_CREAT_FILE "locally_generated_file"

class AFSClient
{
public:
	AFSClient(shared_ptr<Channel> channel, string cache_root)
		: stub_(FileServer::NewStub(channel)), cache_root(cache_root) {}

	string Open(string &fileName)
	{

		SimplePathRequest request;
		request.set_path(fileName);

		OpenResp reply;
		ClientContext context;

		cout << "client open called" << endl;

		// Status status = stub_->Open(&context, request, &reply);

		// if (status.ok())
		// {
		// 	// return "success: received " + to_string(reply.err());
		// }
		// else
		// {
		// 	cout << status.error_code() << ": " << status.error_message()
		// 		 << endl;
		// 	return "RPC failed";
		// }
	}

	Status getAttr(string &fileName, struct stat *stbuf)
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
		}

		return status;
	}

	Status readDir(string &fileName, ReadDirResponse *reply)
	{

		SimplePathRequest request;
		request.set_path(fileName);

		ClientContext context;

		cout << "Client readDir called" << endl;

		Status status = stub_->ReadDir(&context, request, reply);
		return status;
	}

	unique_ptr<FileServer::Stub> stub_;
	string cache_root;
};

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

class last_modified_t {
public:
    last_modified_t(std::string& cache_root) : cache_root(cache_root) {
        snapshot_frequency = 10; // after how many logs, make a snapshot and clear the log
        
        // read the snapshot and log to get the most recent state
        std::vector<std::string> filenames{std::string(cache_root).append("/last_modified_snapshot.txt"), std::string(cache_root).append("/last_modified_log.txt")};
        for (std::string& filename : filenames) {
            std::cout << filename << std::endl;
            if (std::ifstream is{filename, std::ios::in | std::ios::ate}) {
                auto size = is.tellg();
                is.seekg(0);
                std::string fn(64, '\0'), ts(sizeof(struct timespec), '\0'); // filename, timespec
                if (size > 0)
                    while (is.tellg() != -1 && is.tellg() != size) {
                        std::cout << is.tellg() << std::endl;
                        std::getline(is, fn);
                        if (fn.size() != 64) {
                            std::cout << "incorrect last_modified persistent file -- fn" << is.tellg() << std::endl;
                            assert(false);
                        }
                        is.read(&ts[0], sizeof(struct timespec));
                        std::cout << is.get() << std::endl; // strip '\n'. should print 10
                        
                        if (ts == std::string(sizeof(struct timespec), 255) && table.count(fn)) 
                            table.erase(fn);
                        else if (ts.size() != sizeof(struct timespec)) {
                            std::cout << "incorrect last_modified persistent file -- ts" << is.tellg() << std::endl;
                            assert(false);
                        }
                        else
                            table[fn] = ts;
                    }
            }
            print_table();
        }
        log.open((filenames[1]), std::ios::out | std::ios::app);
    }

    std::string get(const char* filename) {
        return get(std::string(filename));
    }
    
    std::string get(std::string filename) {
        // return "not found" if not found in table
        std::string hashed_fn = hashpath(filename.c_str());
        return (table.count(hashed_fn) ? table[hashed_fn] : "not found");
    }

    void set(const char* filename, std::string state) {
        set(std::string(filename), state);
    }

    void set(std::string filename, std::string state) {
        std::string hashed_fn = hashpath(filename.c_str());
        // if nothing is changed, do nothing
        if (table.count(hashed_fn) && table[hashed_fn] == state) { return; }

        // otherwise, set it in memory and flush log
        table[hashed_fn] = state;
        log << hashed_fn << std::endl << state << std::endl;
        log.flush();

        // if there are a lot of logs, make snapshot
        if ((counter = (counter + 1) % snapshot_frequency) == 0)
            do_snapshot();
    }

    void erase(const char* filename) {
        erase(std::string(filename));
    }

    void erase(std::string filename) {
        std::string hashed_fn = hashpath(filename.c_str());
        if (table.count(hashed_fn)) {
            table.erase(hashed_fn);
            log << hashed_fn << std::endl << std::string(sizeof(struct timespec), 255) << std::endl; // 0xFF means erase
            log.flush();
        }
    }

    std::size_t count(std::string filename) {
        return table.count(hashpath(filename.c_str()));
    }

    std::size_t size() {
        return table.size();
    }

    void print_table() {
        std::cout << "=========================\n";
        std::cout << "last_modified table\n";
        for (auto item : table) {
            const struct timespec* ts = (const struct timespec*)item.second.c_str();
            std::cout << "\t" << item.first << " = {" << ts->tv_sec << ", " << ts->tv_nsec << "}\n";
        }
        std::cout << "=========================\n";
    }
private:
    bool do_snapshot() {
        std::cout << "[log] last_modified snapshot start" << std::endl;
        std::string old_name(std::string(cache_root) + "/last_modified_snapshot.txt.tmp");
        std::string new_name(std::string(cache_root) + "/last_modified_snapshot.txt");

        if (std::ofstream os{old_name, std::ios::out | std::ios::trunc}) {
            for (auto& entry : table) {
                os << entry.first << std::endl << entry.second << std::endl;
                // os.write(hashpath(entry.first.c_str()).c_str(), 32);
                // os.write(entry.second ? "1" : "0", 1);
            }
            os.close();
            rename(old_name.c_str(), new_name.c_str());
            // truncate the log
            log.close();
            std::cout << "[log] log closed" << std::endl;
            log.open((std::string(cache_root).append("/last_modified_log.txt")), std::ios::out | std::ios::trunc);
            std::cout << "[log] log opened? " << (log.is_open() ? "true" : "false") << std::endl;
            std::cout << "[log] last_modified snapshot finish" << std::endl;
            return true;
        }
        return false;
    }
    std::unordered_map<std::string, std::string> table;
    std::fstream log;
    std::string cache_root;
    int counter;
    int snapshot_frequency;
};

class is_dirty_t {
public:
    is_dirty_t(std::string& cache_root) : cache_root(cache_root) {
        snapshot_frequency = 10; // after how many logs, make a snapshot and clear the log
        
        // read the snapshot and log to get the most recent state
        std::vector<std::string> filenames{std::string(cache_root).append("/is_dirty_snapshot.txt"), std::string(cache_root).append("/is_dirty_log.txt")};
        for (std::string& filename : filenames) {
            std::cout << filename << std::endl;
            if (std::ifstream is{filename, std::ios::in | std::ios::ate}) {
                auto size = is.tellg();
                is.seekg(0);
                std::string buf(100, '\0');
                if (size > 0)
                    while (is.tellg() != -1 && is.tellg() != size) {
                        std::getline(is, buf);
                        if (buf.size() != 66) {
                            std::cout << "offset=" << is.tellg() << std::endl;
                            assert(false);
                        }
                        switch (buf.back()) {
                            case '2':
                                table.erase(buf.substr(0, 64));
                                break;
                            case '1':
                                table[buf.substr(0, 64)] = true;
                                break;
                            case '0':
                                table[buf.substr(0, 64)] = false;
                                break;
                            default:
                                std::cerr << "error in reading is_dirty snapshot and log\n";

                        }
                    }
            }
        }
        log.open((filenames[1]), std::ios::out | std::ios::app);
        std::cout << "[log] log is open? " << (log.is_open() ? "true" : "false") << std::endl;
    }

    bool get(const char* filename) {
        return get(std::string(filename));
    }
    
    bool get(std::string filename) {
        // return true if filename exists in table and is_dirty is true
        std::string hashed_fn = hashpath(filename.c_str());
        return table.count(hashed_fn) && table[hashed_fn];
    }

    void set(const char* filename, bool state) {
        set(std::string(filename), state);
    }

    void set(std::string filename, bool state) {
        std::string hashed_fn = hashpath(filename.c_str());
        // if nothing is changed, do nothing
        if (table.count(hashed_fn) && table[hashed_fn] == state) { return; }

        // otherwise, set it in memory and flush log
        table[hashed_fn] = state;
        log << hashed_fn << " " << (state ? '1' : '0') << std::endl;
        log.flush();

        // if there are a lot of logs, make snapshot
        if ((counter = (counter + 1) % snapshot_frequency) == 0)
            do_snapshot();
    }

    void erase(const char* filename) {
        erase(std::string(filename));
    }

    void erase(std::string filename) {
        std::string hashed_fn = hashpath(filename.c_str());
        if (table.count(hashed_fn)) {
            table.erase(hashed_fn);
            log << hashed_fn << " 2" << std::endl; // 2 means erase the entry
            log.flush();
        }
    }
private:
    bool do_snapshot() {
        std::cout << "[log] enter snapshot" << std::endl;
        std::string old_name(std::string(cache_root) + "/is_dirty_snapshot.txt.tmp");
        std::string new_name(std::string(cache_root) + "/is_dirty_snapshot.txt");

        if (std::ofstream os{old_name, std::ios::out | std::ios::trunc}) {
            for (auto& entry : table) {
                os << entry.first << " " << (entry.second ? "1" : "0") << std::endl;
                // os.write(hashpath(entry.first.c_str()).c_str(), 32);
                // os.write(entry.second ? "1" : "0", 1);
            }
            os.close();
            rename(old_name.c_str(), new_name.c_str());
            // truncate the log
            log.close();
            log.open((std::string(cache_root).append("/is_dirty_log.txt")), std::ios::out | std::ios::trunc);
            return true;
        }
        return false;
    }
    std::unordered_map<std::string, bool> table;
    std::fstream log;
    std::string cache_root;
    int counter;
    int snapshot_frequency;
};

class afs_data_t {
public:
    afs_data_t(std::string cache_root) : cache_root(cache_root), is_dirty{cache_root}, last_modified{cache_root} {}
    std::string cache_root; // must contain forward slash at the end.
    std::unique_ptr<FileServer::Stub> stub_;
    // std::unordered_map<std::string, std::string> last_modified; // path to st_mtim
    last_modified_t last_modified;
    is_dirty_t is_dirty;
};

std::string cachepath(const char* rel_path) {
    return AFS_DATA->cache_root + hashpath(rel_path);
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
	int res;

	string path_str = path;
	Status status = AFS_CLIENT->getAttr(path_str, stbuf);

	if (!status.ok())
		return -errno;
	return 0;
}

static int bnfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
						off_t offset, struct fuse_file_info *fi)
{

	ReadDirResponse *reply;
	int res;

	string path_str = path;
	Status status = AFS_CLIENT->readDir(path_str, reply);

	if (!status.ok())
		return -errno;

	DIR *dp;
	struct dirent *de;

	// Nothing to do on server with them
	(void)offset;
	(void)fi;
	// filler to be used locally

	dp = opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL)
	{
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
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

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to)
{
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	int res;

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
	int res;

	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
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

int afs_open(const char *path, struct fuse_file_info *fi)
{
    struct timespec t, u;
    clock_gettime(CLOCK_MONOTONIC, &t);
    std::cout << "[log] afs_open: start\n";
    std::string path_str(path);
    SimplePathRequest filepath;
    filepath.set_path(path_str);
    char fpath[PATH_MAX];
    if (AFS_DATA->last_modified.count(path_str) == 1) {
        // cache exists 
        // check if it is locally created file
        if (AFS_DATA->last_modified.get(path_str) == LOCAL_CREAT_FILE) {
            fi->fh = open(cachepath(path).c_str(), fi->flags);
            if (fi->fh < 0) {
                return -errno;
            }
            std::cout << "[log] afs_open: is locally created file\n";
            return 0;
        }
        
        StatResponse stat_content;
        ClientContext context;
        // set_deadline(context);
        AFS_DATA->stub_->GetAttr(&context, filepath, &stat_content);

        if ( true) {
            //***********stat_content.mtime() == AFS_DATA->last_modified.get(path_str)) {
            // can use cache
            std::cout << "[log] afs_open: can use local cache\n";
            fi->fh = open(cachepath(path).c_str(), fi->flags);
            if (fi->fh < 0) {
                std::cout << "[err] afs_open: failed to open cache file\n";
                return -errno;
            }
            clock_gettime(CLOCK_MONOTONIC, &u);
            std::cout << "[log] afs_open: end. took " << 
                      ((u.tv_sec - t.tv_sec) * 1000000000 + (u.tv_nsec - t.tv_nsec)) << "ns.\n";
            return 0;
        }
        else {
            // can't use cache.
            AFS_DATA->last_modified.erase(path_str); // delete last_modified entry. 
            unlink(cachepath(path).c_str()); // delete cached file
        }
    }
    
    // get file from server, or create a new one
    std::cout << "[log] afs_open: start downloading from server." << std::endl;
    OpenResp msg;
    ClientContext context;
    // set_deadline(context);
    OpenResp reply;


    //std::unique_ptr<ClientReader<ReadSReply> > reader(stub_->ReadS(&context, request));

    std::unique_ptr<grpc::ClientReader<OpenResp> > reader(
        AFS_DATA->stub_->Open(&context, filepath));

        // 
    if (!reader->Read(&msg)) {
        std::cout << "[err] afs_open: failed to download from server." << std::endl;
        return -EIO;
    }
    if (msg.file_exists()) {
        // open file with O_TRUNC
        std::ofstream ofile(cachepath(path),
            std::ios::binary | std::ios::out | std::ios::trunc);
        // TODO: check failure
        ofile << msg.buf();
        while (reader->Read(&msg)) {
            ofile << msg.buf();
        }

        Status status = reader->Finish();
        if (!status.ok()) {
            std::cout << "[err] afs_open: failed to download from server." << std::endl;
            return -EIO;
        }
        ofile.close(); // the cache is persisted
        
        AFS_DATA->last_modified.set(path_str, msg.timestamp());
        // writeMapIntoFile();
        std::cout << "[log] afs_open: finish download from server\n";
    }
    else {
        std::cout << "[log] afs_open: create a new file\n";
        AFS_DATA->last_modified.set(path_str, LOCAL_CREAT_FILE);
        // writeMapIntoFile();
        close(creat(cachepath(path).c_str(), 00777));
    }

    // set is_dirty to clean
    AFS_DATA->is_dirty.set(path, 0);

    // give user the file
    fi->fh = open(cachepath(path).c_str(), fi->flags);
    if (fi->fh < 0) {
        std::cout << "[err] afs_open: error open downloaded cache file.\n";
        return -errno;
    }
    clock_gettime(CLOCK_MONOTONIC, &u);
    std::cout << "[log] afs_open: end. took " << 
                ((u.tv_sec - t.tv_sec) * 1000000000 + (u.tv_nsec - t.tv_nsec)) << "ns.\n";
    return 0;   
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
					struct fuse_file_info *fi)
{
	// int fd;
	// int res;

    ssize_t rc = pread(fi->fh, buf, size, offset);
    if (rc < 0)
        return -errno;
    return rc;
}

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
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void)path;
	(void)fi;
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
					 struct fuse_file_info *fi)
{
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
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
						size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
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
	.mkdir = xmp_mkdir,
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

    char* cache_root = "";
	afs_data_t* afs_data = new afs_data_t(std::string(cache_root));
    afs_data->stub_ = FileServer::NewStub(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));
    if (afs_data->cache_root.back() != '/') { afs_data->cache_root += '/'; }
    std::cout<<"[STARTUP-------------------]"<< afs_data->last_modified.size() <<"\n"; 

	// afs_data_t* afs_data = new afs_data_t(std::string(cache_root));
	// afs_data->stub_ = AFS::NewStub(grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials()));

	AFSClient * afsClient = new AFSClient(
        //  afs_data->stub_,
		grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()),
		"/");
	cout << "Initialized AFS client" << endl;

	return fuse_main(argc, argv, &xmp_oper, afsClient);
}
