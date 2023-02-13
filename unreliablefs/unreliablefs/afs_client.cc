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

using afs::DeleteReq;
using afs::DeleteResp;
using afs::FileServer;
using afs::OpenReq;
using afs::OpenResp;
using afs::PutFileReq;
using afs::PutFileResp;
// using afs::ReadDirResponse;
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

  int Open(const char* fileName)
  {

    OpenReq request;
    request.set_path(fileName);

    OpenResp reply;
    ClientContext context;

    cout << "[log] AFS client open called new" << endl;

    // Initialize stub if NULL.
    if (!stub_) {
      cout << "[log] initializing stub" << endl;
      stub_ = FileServer::NewStub(
        grpc::CreateChannel("localhost:50051",
        grpc::InsecureChannelCredentials()));
    }

    Status status = stub_->Open(&context, request, &reply);

    if (status.ok())
    {
      return  0;
    }
    else
    {
      cout << status.error_code() << ": " << status.error_message()
         << endl;
      return -1;
    }

    return 0;
  }

  int GetAttr(const char* fileName, struct stat *stbuf)
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
		} else {
      return -1;
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

  unique_ptr<FileServer::Stub> stub_;
  string cache_root;
};

// Port calls to C-code.

AFSClient* NewAFSClient(char* cache_root) {
  AFSClient* client = new AFSClient(cache_root);

  return client;
}

int AFS_open(AFSClient* client, const char* file_path) {
  return client -> Open(file_path);
}

int AFS_getAttr(AFSClient* client, 
    const char* file_path, struct stat *buf) {
  return client -> GetAttr(file_path, buf);
}

int AFS_close(AFSClient* client, const char* file_path) {
  return client -> Close(file_path);
}

}