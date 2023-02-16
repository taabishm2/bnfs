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
using afs::AccessPathRequest;
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

  int access(const char *path, int mode)
  {
    ClientContext context;
    BaseResponse reply;
    AccessPathRequest request;
    request.set_path(path);
    request.set_mode(mode);

    Status status = stub_->Access(&context, request, &reply);
    int res = reply.errorcode();

    if (res < 0)
    {
      return res;
    }
    return 0;
  }

  int Open(const char* path, struct fuse_file_info *fi, bool is_create)
  {
    std::cout << "[log] open: start\n";
    std::string path_str(path);

    // Check if a temp file exists for the file trying to be opened.
    int temp_fd = -1;
    if (cache_helper->getCheckInTemp(path, &temp_fd, false, O_RDONLY, false))
    {
      // Another client has written dirty data to this file. Reject open() in
      // this case.
      // Input/output error?
      return -EIO;
    }

    OpenReq request;
    ClientContext open_context;
    request.set_path(path_str);
    request.set_flag(fi->flags);
    request.set_is_create(is_create);

    // for getattr(). TODO: Use getattr only if there is a cached copy.
    SimplePathRequest filepath;
    filepath.set_path(path_str);
    ClientContext getattr_context;
    StatResponse getattr_reply;

    string cachepath = cache_helper->getCachePath(path);
    // If cache is valid, cache valid must be updated to a positive number.
    int cache_fd = -1;
    bool use_cache = false;

    // Use cached file based on GetAttr() result or if it absent in cache.
    Status getattr_status =
      stub_->GetAttr(&getattr_context, filepath, &getattr_reply);
    
    if (getattr_status.ok()) {
			use_cache = !(cache_helper->isCacheOutOfDate(
          path, getattr_reply.mtime(), &cache_fd, false, fi->flags));
		}
    
    // Get file from server.
    if (!use_cache || cache_fd < 0) {
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

      string buf = std::string();
      if (is_create) {
          // New file created. Update/create the cached copy.
          cache_fd = cache_helper->syncFileServerToCache(
            path, buf.c_str(), false, O_CREAT);
          std::cout << "Open: created a new cache file with fd: " << cache_fd << "\n";
      } else if (reply.file_exists()) {
          cout << "Open: file found on the server";
          // open file with O_TRUNC 
          buf += reply.buf();
          while (reader->Read(&reply)) {
              buf += reply.buf();
          }

          Status status = reader->Finish();
          if (!status.ok()) {
              std::cout << "[err] Open: failed to download from server. \n";
              return -EIO;
          }
          cout << " ======" <<  buf;
          cache_fd = cache_helper->syncFileServerToCache(
            path, buf.c_str(), false, fi->flags);

          std::cout << "Open: finish download from server \n";
      } else {
          // Opened(not create) a file not present on server.
          cache_helper->getCheckInTemp(path, &temp_fd, false, O_CREAT, true) ;
          std::cout << "Open: created a new temp file with fd " << temp_fd << "\n";
      }
    }

    if (temp_fd > -1) {
      fi->fh = temp_fd;
      return temp_fd;
    } else if (cache_fd > -1) {
      fi->fh = cache_fd;
      return cache_fd;
    }
      std::cout << "[err] Open: error open downloaded cache file.\n";
      return -1;
  }

  int Close(const char* file_path) {

    // Get temp file path to close.
    string temp_path = cache_helper -> getTempPath(file_path);

    ifstream file(temp_path, ios::in);
    if(!file) {
      cout << "File " << file_path << " not found in temp dir. Returning success\n";

      return 0;
    }
    cout << "PutFiile " << temp_path << " to server\n";

    // Prepare grpc messages.
    ClientContext context;
    PutFileReq request;
    PutFileResp reply;

    std::unique_ptr<ClientWriter<PutFileReq>> writer(stub_->PutFile(&context, &reply));
    string buf(BUFSIZE, '\0');

    // Send one request for filepath.
    request.set_path(file_path);
    if (!writer->Write(request)) {
      // cache uncommit.

      return -1;
    }

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

    cout << "got last modification time " << reply.lastmodifiedtime();
    
    if (!status.ok()) {
      cout << "PutFile rpc failed: " << status.error_message() << std::endl;

      // Revert cache changes.
      // cache_helper.uncommit(file_path);

      return -1;
    }
    else {
      cout << "Finished sending file with path " << file_path << endl;

      // commit temp file to cache.
      cache_helper -> commitToCache(file_path, reply.lastmodifiedtime());
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

int AFS_access(AFSClient* client, const char* file_path, int mode) {
  return client -> access(file_path, mode);
}

}