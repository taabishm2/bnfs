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
#include "cache_helper.h"

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
using grpc::ClientWriter;

using namespace std;

#define BUFSIZE 65500

extern "C" {

// gRPC client.
struct AFSClient
{
  // Member functions.

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

		int res = reply.baseresponse().errorcode();
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

  int Open(const char* path, struct fuse_file_info *fi, bool is_create)
  {
    std::cout << "[log] open: start\n";
    std::string path_str(path);
    OpenReq request;
    request.set_path(path_str);
    request.set_flag(fi->flags);
    request.set_is_create(is_create);

    // for getattr()
    SimplePathRequest filepath;
    filepath.set_path(path_str);
    ClientContext getattr_context;
    ClientContext open_context;
    StatResponse getattr_reply;

    string cachepath = "/tmp/cache_new.txt";

    // TODO(Sweksha) : Use cache based on GetAttr() result.
    // stub_->GetAttr(&getattr_context, filepath, &getattr_reply);
    
    // Get file from server.
    std::cout << "Open: Opening file from server." << std::endl;
    
    OpenResp reply;

    std::unique_ptr<grpc::ClientReader<OpenResp> > reader(
        stub_->Open(&open_context, request));

    
    // Read the stream from server. 
    if (!reader->Read(&reply)) {
        std::cout << "[err] Open: failed to download file: " << path_str
                  << " from server." << std::endl;
        return -EIO;
    }

    // if (reply.file_exists()) {
    //   string buf = std::string();
    //   // buf.reserve(size);
    //   cout << "file exists!!!=====";
    //     // open file with O_TRUNC
        
    //    buf += reply.buf();
    //     while (reader->Read(&reply)) {
    //         buf += reply.buf();
    //     }

    //     Status status = reader->Finish();
    //     cout << "reader finished!!!=====";
    //     if (!status.ok()) {
    //         std::cout << "[err] Open: failed to download from server." << std::endl;
    //         return -EIO;
    //     }
    //     cout << "BROOOOOOO BUF ======" <<  buf;

    //     std::cout << "Open: finish download from server\n";
    // }

    if (reply.file_exists()) {
      cout << "file exists!!!=====";
        // open file with O_TRUNC
        std::ofstream ofile(cachepath,
            std::ios::binary | std::ios::out | std::ios::trunc);
            // std::ofstream ofile(cachepath(path),
            // std::ios::binary | std::ios::out | std::ios::trunc);
        // TODO: check failure
        ofile << reply.buf();
        while (reader->Read(&reply)) {
            ofile << reply.buf();
        }

        Status status = reader->Finish();
        cout << "reader finished!!!=====";
        if (!status.ok()) {
            std::cout << "[err] Open: failed to download from server." << std::endl;
            return -EIO;
        }
        ofile.close(); // the cache is persisted

        std::cout << "Open: finish download from server\n";
    }
    else {
        std::cout << "Open: created a new file\n";
        // close(creat(cachepath(path).c_str(), 00777));
        int new_fd = creat(cachepath.c_str(), 00777);

        cout << "NEW CACHE FILE FD=== " << new_fd;
        close(new_fd);
    }

    // give user the file
    // fi->fh = open(cachepath(path).c_str(), fi->flags);
    cout << "==== before OPEN LOCAL TO cachepath: " << cachepath;
    int ret = open(cachepath.c_str(), fi->flags);
    if (ret < 0) {
        std::cout << "[err] Open: error open downloaded cache file.\n";
        return -errno;
    }

    cout << "==== cachepath: " << cachepath << " AFTER fd " << fi->fh;

    return ret;
  }

  int Close(const char* file_path) {

    // Read file from cache and make gRPC put file writes.
    // string cache_path = cache_helper.getCachedPath(file_path);
    ifstream file(file_path, ios::in);
    if(!file) {
      cout << "File not found at path " << file_path << endl;

      return -1;
    }
    cout << "Putting file contents from path: " << file_path << "\n";

    // Prepare grpc messages.
    ClientContext context;
    PutFileReq request;
    PutFileResp reply;

    std::unique_ptr<ClientWriter<PutFileReq>> writer(stub_->PutFile(&context, &reply));
    string buf(BUFSIZE, '\0');
    while (!file.eof()) {
      // Read file contents into buf.
      file.read(&buf[0], BUFSIZE);

      request.set_path(file_path);
      request.set_contents(buf);

      if (!writer->Write(request)) {
          // Revert cache changes.
          // cache_helper.uncommit(file_path);

          // Broken stream.
          break;
      }
    }
    writer->WritesDone();
    Status status = writer->Finish();
    
    if (!status.ok()) {
      cout << "PutFile rpc failed: " << status.error_message() << std::endl;

      // Revert cache changes.
      // cache_helper.uncommit(file_path);

      return -1;
    }
    else {
      cout << "Finished sending file with path " << file_path << endl;

      // commit cache changes.
      // cache_helper.commit(file_path);
    }

    return 0;
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
  CacheHelper* cache_helper;
};

// Port calls to C-code.

AFSClient* NewAFSClient(char* cache_root) {
  AFSClient* client = new AFSClient(cache_root);

  // Initialize the cache helper.
  client -> cache_helper = NewCacheHelper();
  client -> cache_helper -> initCache();

  return client;
}

int AFS_open(AFSClient* client, const char* file_path, struct fuse_file_info *fi, bool is_create) {
  return client -> Open(file_path, fi, is_create);
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