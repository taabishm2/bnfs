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

using afs::AccessPathRequest;
using afs::BaseResponse;
using afs::UnlinkReq;
using afs::UnlinkResp;
using afs::FileServer;
using afs::MkdirReq;
using afs::OpenReq;
using afs::OpenResp;
using afs::PutFileReq;
using afs::PutFileResp;
using afs::ReadDirResponse;
using afs::SimplePathRequest;
using afs::StatResponse;
using afs::UnlinkReq;
using afs::UnlinkResp;
using afs::RenameReq;
using afs::RenameResp;

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientWriter;
using grpc::Status;

using namespace std;

#define BUFSIZE 65500

extern "C"
{

  // gRPC client.
  struct AFSClient
  {
    // Member functions.

    AFSClient(string cache_root)
        : stub_(FileServer::NewStub(
              grpc::CreateChannel("localhost:50051",
                                  grpc::InsecureChannelCredentials()))),
          cache_root(cache_root) {}

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

      if (res == -1)
        return res;

      for (int i = 0; i < reply.dirname_size(); i++)
        if (filler(buf, reply.dirname(i).c_str(), NULL, 0) != 0)
          return -ENOMEM;

      return 0;
    }

    int mkdir(const char *fileName, mode_t mode)
    {
      ClientContext context;
      BaseResponse reply;
      MkdirReq request;

      request.set_path(fileName);
      request.set_mode(mode);

      Status status = stub_->Mkdir(&context, request, &reply);
      int res = reply.errorcode();

      if (res < 0)
      {
        return res;
      }
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

      if (res < 0)
      {
        return res;
      }
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
        return res;

      return 0;
    }

    bool requiresCompleteFetchAndSync(int o_fl)
    {
      return (o_fl == 2 || o_fl == 3 || o_fl == 7 || o_fl == 15 || o_fl == 18 || o_fl == 19 || o_fl == 23 || o_fl == 31);
    }

    bool requiresCacheToTempSync(int o_fl)
    {
      return (o_fl == 4 || o_fl == 5 || o_fl == 6 || o_fl == 20 || o_fl == 21 || o_fl == 22);
    }

    bool requiresNewCacheAndTemp(int o_fl)
    {
      return (o_fl == 16 || o_fl == 17);
    }

    bool requiresNoChange(int o_fl)
    {
      return (o_fl == 12 || o_fl == 13 || o_fl == 14 || o_fl == 28 || o_fl == 29 || o_fl == 30);
    }

    bool noFilesExist(int o_fl)
    {
      return (o_fl == 0 || o_fl == 1);
    }

    bool isErrorState(int o_fl)
    {
      return (o_fl == 8 || o_fl == 9 || o_fl == 10 || o_fl == 11 || o_fl == 24 || o_fl == 25 || o_fl == 26 || o_fl == 27);
    }

    int getOpenFlags(bool is_create, const char *path, int *temp_fd, int *cache_fd)
    {
      struct stat getAttrData;
      int getAttrRes = getAttr(path, &getAttrData);
      cout << "got get attr as " <<  getAttrRes << endl;

      // In case of a is_create call of a file not found on server, 
      // return 16.
      if (getAttrRes < 0 && getAttrRes == -ENOENT) {
        return (is_create << 4);
      }

      bool bA, bB, bC, bD, bE;
      int server_time = static_cast<int>(static_cast<time_t>(getAttrData.st_atim.tv_sec));

      bA = is_create;
      bB = cache_helper->getCheckInTemp(path, temp_fd, false, O_RDWR, false);
      bC = cache_helper->getCheckInCache(path, cache_fd, true, O_RDONLY);
      bD = getAttrRes == 0;
      bE = cache_helper->isCacheOutOfDate(path, server_time, cache_fd, true, O_RDONLY);

      cout << "FLAGS ARE: " << bA << " " << bB << " " << bC << " " << bD << " " << bE << endl;
      return (bA << 4) | (bB << 3) | (bC << 2) | (bD << 1) | bE;
    }

    // Returns file descriptor to local file copy on success.
    int Open(const char *path, struct fuse_file_info *fi, bool is_create)
    {
      int temp_fd, cache_fd;

      // We don't allow opening of dirty files. (to prevent read after write conflicts).
      if (cache_helper->isFileDirty(path))
        return -EIO;

      int o_fl = getOpenFlags(is_create, path, &temp_fd, &cache_fd);
      cout << "[log] Open Flags: " << o_fl << endl;

      if (requiresCompleteFetchAndSync(o_fl))
        fetchAndSyncServerFile(path, fi, is_create, &temp_fd, &cache_fd);

      else if (requiresCacheToTempSync(o_fl)) {
        cache_helper->getCheckInTemp(path, &temp_fd, false, fi->flags, true);
      }

      else if (requiresNewCacheAndTemp (o_fl))
      {
        // mark file as dirty.
        cache_helper -> markFileDirty(path);

        cache_helper->syncFileToCache(path, "", false, fi->flags);
        temp_fd = cache_helper->syncFileToTemp(path, "", false, fi->flags);
      }

      else if (requiresNoChange (o_fl))
      {
      }

      else if (noFilesExist (o_fl))
        return ENOENT;

      else if (isErrorState (o_fl))
        return EIO;

      cout << "[LOG] Final FID: " << temp_fd << endl;
      if (temp_fd > -1)
      {
        return temp_fd;
      }

      // Safely delete file in case of open failures.
      cache_helper -> deleteFromTemp(path);

      return -1;
    }

    int fetchAndSyncServerFile(const char *path, struct fuse_file_info *fi, bool is_create, int *temp_fd, int *cache_fd)
    {
      OpenReq request;
      ClientContext open_context;
      request.set_path(path);
      request.set_flag(fi->flags);
      // TODO: No need for this right?
      request.set_is_create(is_create);

      OpenResp reply;
      unique_ptr<grpc::ClientReader<OpenResp>> reader(stub_->Open(&open_context, request));

      if (!reader->Read(&reply))
      {
        cout << "[err] Open: failed to download file: " << path << endl;
        return -EIO;
      }

      string buf = string();
      buf += reply.buf();
      while (reader->Read(&reply))
        buf += reply.buf();

      Status status = reader->Finish();
      if (!status.ok())
      {
        cout << "[err] Open: failed to download from server. \n";
        return -EIO;
      }

      *cache_fd = cache_helper->syncFileToCache(path, buf.c_str(), false, fi->flags);
      *temp_fd = cache_helper->syncFileToTemp(path, buf.c_str(), false, fi->flags);

      return 0;
    }

    int Unlink(const char* file_path) {
      // Remove file from cache and temp.

      // Remove file from server.

      // Prepare grpc messages.
      ClientContext context;
      UnlinkReq request;
      UnlinkResp reply;

      request.set_path(file_path);

      Status status = stub_->Unlink(&context, request, &reply);
      
      if (!status.ok())
      {
        cout << "unlink rpc failed" << endl;

        return -1;
      }

      return 0;
  }

  int Rename(const char* old_path, const char* new_path) {
      // Prepare grpc messages.
      ClientContext context;
      RenameReq request;
      RenameResp reply;

      request.set_old_path(old_path);
      request.set_new_path(new_path);

      Status status = stub_->Rename(&context, request, &reply);
      
      if (!status.ok())
      {
        cout << "rename rpc failed" << endl;

        return -1;
      }

      // flush old path changes.
      cache_helper -> deleteFromTemp(old_path);

    return 0;
  }

  int Flush(const char *path)
   {
      // Get temp file path to close.
      string temp_path = cache_helper->getTempPath(path);

      ifstream file(temp_path, ios::in);
      if (!file) {
        cout << "temp file not present: " << temp_path << endl;
        return -1;
      }

      if (!cache_helper->isFileDirty(path))
      {
        cout << "File isn't dirty: " << path << endl;
        return 0;
      }

      cout << "CLIENT: Put File " << temp_path << " for " << path << " to server\n";

      ClientContext context;
      PutFileReq request;
      PutFileResp reply;
      request.set_path(path);

      unique_ptr<ClientWriter<PutFileReq>> writer(stub_->PutFile(&context, &reply));
      string buf(BUFSIZE, '\0');

      if (!writer->Write(request))
      {
        cache_helper->deleteFromTemp(path);
        return -1;
      }

      while (!file.eof())
      {
        file.read(&buf[0], BUFSIZE);
        request.set_contents(buf);

        if (!writer->Write(request))
        {
          cache_helper->deleteFromTemp(path);
          break;
        }
      }
      writer->WritesDone();
      Status status = writer->Finish();

      cout << "CLIENT: Got last modification time " << reply.lastmodifiedtime() << endl;

      if (!status.ok())
      {
        cout << "CLIENT: PutFile rpc failed: " << status.error_message() << endl;
        cache_helper->deleteFromTemp(path);
        return -1;
      }
      else
      {
        cout << "CLIENT: Finished sending file with path " << path << endl;
        cache_helper->commitToCache(path, reply.lastmodifiedtime());
      }
      // Note that deleting from temp or commiting to cache unmarks file as dirty.

      return 0;
    }

    unique_ptr<FileServer::Stub> stub_;
    string cache_root;
    CacheHelper *cache_helper;
  };

  // Port calls to C-code.

  AFSClient *NewAFSClient(char *cache_root)
  {
    AFSClient *client = new AFSClient(cache_root);

    // Initialize the cache helper.
    client->cache_helper = NewCacheHelper();
    client->cache_helper->initCache();

    return client;
  }

  int AFS_open(AFSClient *client, const char *file_path, struct fuse_file_info *fi, bool is_create)
  {
    return client->Open(file_path, fi, is_create);
  }

  int AFS_flush(AFSClient *client, const char *file_path)
  {
    return client->Flush(file_path);
  }

  int AFS_getAttr(AFSClient *client, const char *file_path, struct stat *buf)
  {
    return client->getAttr(file_path, buf);
  }

  int AFS_readDir(AFSClient *client, const char *path, void *buf, fuse_fill_dir_t filler)
  {
    return client->readDir(path, path, buf, filler);
  }

  int AFS_mkdir(AFSClient *client, const char *file_path, mode_t mode)
  {
    return client->mkdir(file_path, mode);
  }

  int AFS_rmdir(AFSClient *client, const char *file_path)
  {
    return client->rmdir(file_path);
  }

  int AFS_access(AFSClient *client, const char *file_path, int mode)
  {
    return client->access(file_path, mode);
  }

  int AFS_unlink(AFSClient* client, const char* file_path) {
    return client -> Unlink(file_path);
  }

  int AFS_rename(AFSClient* client, const char* old_path, const char* new_path) {
    return client -> Rename(old_path, new_path);
  }

  void Cache_markFileDirty(AFSClient* client, const char *path) {
    return client -> cache_helper -> markFileDirty(path);
  }

  char* Cache_path(AFSClient* client, const char *path) {
    string cache_path = client -> cache_helper -> getCachePath(path);
    cout << "cache path for " << path << " is " << cache_path << endl; 

    char* res = (char*) malloc(sizeof(char) * cache_path.length());
    strcpy(res, cache_path.c_str());

    return res;
  }
}