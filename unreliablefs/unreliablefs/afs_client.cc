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

    bool isOpenWithCreate(int flags) {
      return ((flags & O_CREAT) == O_CREAT);
    }

    bool isOpenWithCreateNExclusive(int flags) {
      return isOpenWithCreate(flags) && ((flags & O_EXCL) == O_EXCL);
    }

    // 1 - Requires full fetch.
    // 2 - read from local cache.
    // 3 - create file in local cache and mark dirty.
    int getOpenFlags(bool is_create, int flags, const char *path)
    {
      struct stat getAttrData;
      int getAttrRes = getAttr(path, &getAttrData);
      cout << "got get attr as " <<  getAttrRes << endl;

      // In case of a is_create call of a file not found on server, 
      if (getAttrRes == -ENOENT && (is_create | isOpenWithCreate(flags))) {
        return 3;
      }

      int server_time = static_cast<int>(static_cast<time_t>(getAttrData.st_mtim.tv_sec));

      int cache_fd = cache_helper->isPresentInCache(path);
      int temp_fd = cache_helper->isPresentInTemp(path);

      bool outOfDate = true;
      if(temp_fd > 0) {
        outOfDate = cache_helper->isOutOfDate(path, server_time, temp_fd);
      }
      cout << " [[CLIENT]] OUT OF DATE: " << outOfDate << endl; 

      if(temp_fd < 0 && cache_fd < 0) {
        cout << "Local temp/cache not present" << endl;
        return 1;
      }

      if(temp_fd < 0 && cache_fd >= 0 && !outOfDate) {
        cout << "Fresh cache present, temp absent" << endl;

        // close cache fd.
        close(cache_fd);

        return 4;
      }

      close(temp_fd);
      if(cache_fd > 0) {
        close(cache_fd);
      }

      if(outOfDate) {
        cout << "file is out of date" << endl;
        return 1;
      }

      return 2;
    }

    // Returns file descriptor to local file copy on success.
    int Open(const char *path, struct fuse_file_info *fi, bool is_create)
    {
      // We don't allow opening of dirty files. (to prevent read after write conflicts).
      // if (cache_helper->isFileDirty(path))
      //   return -EIO;

      int fd;
      int o_fl = getOpenFlags(is_create, fi -> flags, path);
      cout << "[log] Open Flags: " << o_fl << endl;

      if (o_fl == 1) // File out of date
        fetchAndSyncServerFile(path, fi);

      else if (o_fl == 2)
      { 
        // Nothing to be done. Just open.
      }
      else if (o_fl == 3)
      {
        // cache_helper->writeFileToCache(path, "");
        close(creat(cache_helper -> getTempPath(path).c_str(), 00777));
        close(creat(cache_helper -> getCachePath(path).c_str(), 00777));

        // mark file as dirty.
        cache_helper -> markFileDirty(path);

        createOnServer(path, fi);
      }

      else if (o_fl == 4) {
        cache_helper->getCheckInTemp(path, &fd, true, O_RDONLY, true);
      }

      int flags = fi -> flags;
      if(isOpenWithCreateNExclusive(flags)) {
        flags ^= O_EXCL;
      }

      int temp_fd = open(cache_helper -> getTempPath(path).c_str(), flags);
      cout << "[LOG] Final FID: " << temp_fd << endl;
      cout << "cache path: " << cache_helper -> getTempPath(path) << endl;
      cout << "logical path: " << path << endl;
      cout << "using flasg: " << flags << endl;

      if (temp_fd > -1)
      {
        return temp_fd;
      }

      // Safely delete file in case of open failures.
      cache_helper -> deleteFromTemp(path);

      return -errno;
    }

    int createOnServer(const char *path, struct fuse_file_info *fi)
    {
      OpenReq request;
      ClientContext open_context;
      request.set_path(path);
      request.set_flag(fi->flags);
      request.set_is_create(true);


      OpenResp reply;
      unique_ptr<grpc::ClientReader<OpenResp>> reader(stub_->Open(&open_context, request));

      if (!reader->Read(&reply))
      {
        cout << "[err] Open: failed to download file: " << path << endl;
        return -EIO;
      }

      Status status = reader->Finish();
      if (!status.ok())
      {
        cout << "[err] Open: failed to download from server. \n";
        return -EIO;
      }

      return 0;
    }

    int fetchAndSyncServerFile(const char *path, struct fuse_file_info *fi)
    {
      OpenReq request;
      ClientContext open_context;
      request.set_path(path);
      request.set_flag(fi->flags);

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

      cache_helper->writeFileToCache(path, buf.c_str());
      cache_helper->writeFileToTemp(path, buf.c_str());

      return 0;
    }

    int Unlink(const char* file_path) {
      // Remove file from cache and temp.
      cache_helper -> deleteFromTemp(file_path);

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
      int fd = cache_helper -> isPresentInCache(old_path);
      if(fd > 0) {
        string old_cache_path = cache_helper-> getTempPath(old_path);
        string new_cache_path = cache_helper-> getTempPath(new_path);
        int ret = rename(
          old_cache_path.c_str(), new_cache_path.c_str());
          if (ret == -1) {
              cout << "renaming temp failed " <<
              " old path " << old_cache_path << 
              " new path " << new_cache_path << errno << endl;
              return -errno;
          }
        cout << "renamed " << old_cache_path << " " << new_cache_path << endl;

        if(cache_helper->isFileDirty(old_path)) {
          cache_helper -> unmarkFileDirty(old_path);
          cache_helper -> markFileDirty(new_path);
        }

        close(fd);
      }

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

    return 0;
  }

  int Flush(const char *path, struct fuse_file_info *fi)
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

      while (file.read(&buf[0], BUFSIZE))
      {
        request.set_contents(buf);

        if (!writer->Write(request))
        {
          break;
        }
      }
      if (file.eof()) {
            buf.resize(file.gcount());
            request.set_contents(buf);
            writer->Write(request);
        }
      
      writer->WritesDone();
      Status status = writer->Finish();

      cout << "CLIENT: Got last modification time " << reply.lastmodifiedtime() << endl;

      if (!status.ok())
      {
        cout << "CLIENT: PutFile rpc failed: " << status.error_message() << endl;
        //cache_helper->deleteFromTemp(path);
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

  int AFS_flush(AFSClient *client, const char *file_path, struct fuse_file_info *fi)
  {
    return client->Flush(file_path, fi);
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
    string cache_path = client -> cache_helper -> getTempPath(path);
    cout << "cache path for " << path << " is " << cache_path << endl; 

    char* res = (char*) malloc(sizeof(char) * cache_path.length());
    strcpy(res, cache_path.c_str());

    return res;
  }
}