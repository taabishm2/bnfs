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

#include <openssl/sha.h>

#include <sstream>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "afs.grpc.pb.h"

using afs::DeleteReq;
using afs::DeleteResp;
using afs::FileServer;
using afs::OpenReq;
using afs::OpenResp;
using afs::PutFileReq;
using afs::PutFileResp;
using afs::ReadDirResponse;
using afs::BaseResponse;
using afs::SimplePathRequest;
using afs::StatResponse;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using namespace std;

extern "C" {

// gRPC client.
struct AFSClient
{
  AFSClient(string cache_root)
    : stub_(FileServer::NewStub(
      grpc::CreateChannel("localhost:50051",
      grpc::InsecureChannelCredentials()))), cache_root(cache_root) {}

	int getAttr(const char *fileName, struct stat *stbuf)
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

    cout << "** CLIENT GOT FOR getATTR: " << reply.baseresponse().errorcode() << endl;
		int res = reply.baseresponse().errorcode();
    cout << "** FINAL CLIENT GOT FOR getATTR: " << res << endl;
    return res;
	}

	int readDir(const char *fileName, const char *path, void *buf, fuse_fill_dir_t filler)
	{

    ClientContext context;
    ReadDirResponse reply;
		SimplePathRequest request;
		request.set_path(fileName);

		Status status = stub_->ReadDir(&context, request, &reply);
		int res = reply.baseresponse().errorcode();

    if (res == -1) return res;

    for (int i = 0; i < reply.dirname_size(); i++)
      if (filler(buf, reply.dirname(i).c_str(), NULL, 0) != 0)
        return -ENOMEM;

		return 0;
	}

	int mkdir(const char *fileName)
	{
    ClientContext context;
    BaseResponse reply;
		SimplePathRequest request;
		request.set_path(fileName);

		Status status = stub_->Mkdir(&context, request, &reply);
    int res = reply.errorcode();
    
    cout << "CLIENT: mkdir GOT: " << reply.errorcode() << endl;
    cout << "CLIENT: FINAL mkdir GOT: " << res << endl;
    
    if (res < 0) { return res; }
    return 0;
	}

int rmdir(const char *fileName)
	{
    ClientContext context;
    BaseResponse reply;
		SimplePathRequest request;
		request.set_path(fileName);

		Status status = stub_->Rmdir(&context, request, &reply);
    int res = reply.errorcode();
    
    cout << "CLIENT: rmdir GOT: " << reply.errorcode() << endl;
    cout << "CLIENT: FINAL rmdir GOT: " << res << endl;

    if (res < 0) { return res; }
    return 0;
	}

  int Open(const char* path, struct fuse_file_info *fi)
  {
    std::cout << "[log] open: start\n";
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

  int Close(const char* file_path)
  {
    // Read file from local cache, if present.
    int fd = open(file_path, O_RDONLY);
      if (fd == -1)
        return -errno;

    int size = 1000; // TODO: read and put file in chunks using streaming.
    char* buf = (char*) malloc(sizeof(char) * (size + 1));
    int res = pread(fd, buf, size, 0);
    if (res == -1)
      return -errno;

    // Prepare grpc messages.
    PutFileReq request;
    PutFileResp reply;
    ClientContext context;

    request.set_path(file_path);
    request.set_contents(buf);

    Status status = stub_->PutFile(&context, request, &reply);

    if (status.ok())
    {
      cout << "[log] AFS file contents sent " << request.contents() << endl;
      return 0;
    }
    else
    {
      cout << status.error_code() << ": " << status.error_message()
         << endl;
      return -errno;
    }
  }

  // TODO: CALL FROM CACHE HELPER
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

// Port calls to C-code.

AFSClient* NewAFSClient(char* cache_root) {
  AFSClient* client = new AFSClient(cache_root);

  return client;
}

int AFS_open(AFSClient* client, const char* file_path, struct fuse_file_info *fi) {
  return client -> Open(file_path, fi);
}

int AFS_close(AFSClient* client, const char* file_path) {
  return client -> Close(file_path);
}

int AFS_getAttr(AFSClient* client, const char* file_path, struct stat *buf) {
  return client -> getAttr(file_path, buf);
}

int AFS_readDir(AFSClient* client, const char *path, void *buf, fuse_fill_dir_t filler) {
  return client -> readDir(path, path, buf, filler);
}

int AFS_mkdir(AFSClient* client, const char* file_path) {
  return client -> mkdir(file_path);
}

int AFS_rmdir(AFSClient* client, const char* file_path) {
  return client -> rmdir(file_path);
}

}