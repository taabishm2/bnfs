#include <iostream>
#include <memory>
#include <string>
#include <fcntl.h>    /* For O_RDWR */
#include <unistd.h>   /* For open(), creat() */
#include <fstream>
#include <sstream>
#include <iomanip>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <sys/stat.h>
#include <dirent.h>

#include <openssl/sha.h>

#include "afs.grpc.pb.h"

using afs::FileServer;
using afs::OpenReq;
using afs::OpenResp;
using afs::PutFileReq;
using afs::PutFileResp;
using afs::DeleteReq;
using afs::DeleteResp;
using afs::SimplePathRequest;
using afs::StatResponse;
// using afs::ReadDirResponse;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::Status;

using namespace std;

#define BUFSIZE 65500
#define FS_ROOT "fs_root"
#define CACHE "cache"

std::string AFS_ROOT_DIR;

class FileServerServiceImpl final : public FileServer::Service
{
  Status GetAttr(ServerContext *context, const SimplePathRequest *request,
                   StatResponse *reply) override
  {
      cout << "[log] recieved GetAttr RPC from client! for " << request -> path() << endl;

      struct stat stbuf;
      int res = lstat(request -> path().c_str(), &stbuf);
      if (res == -1) {
          cout << "[log] lstat on getAttr failed" << endl;
          res = -errno;
      }

      // Do this only if res is not error. also return res 
      reply->set_dev(stbuf.st_dev);
      reply->set_ino(stbuf.st_ino);
      reply->set_mode(stbuf.st_mode);
      reply->set_nlink(stbuf.st_nlink);
      reply->set_rdev(stbuf.st_rdev);
      reply->set_size(stbuf.st_size);
      reply->set_blksize(stbuf.st_blksize);
      reply->set_blocks(stbuf.st_blocks);
      reply->set_atime(stbuf.st_atime);
      reply->set_mtime(stbuf.st_mtime);
      reply->set_ctime(stbuf.st_ctime);
      
      return Status::OK;
  }

  Status Open(ServerContext *context, const OpenReq *request,
                ServerWriter<OpenResp> *writer) override
    {
        cout << "Recieved Open RPC from client!" << endl;
        OpenResp reply;
        // string path = request->path();
        // TOD0(Sweksha) : Use getServerFilepath
      //  string path = getServerFilepath(request->path());
      string path = request->path();
       ifstream file(path,ios::in);
        cout << "Opening: " << path << "\n";

        if (!file.is_open()) {
            reply.set_file_exists(false);
            writer->Write(reply);
            return Status::OK;
        }

        cout << "====setting file exists true\n";
        reply.set_file_exists(true);

       string buf(BUFSIZE, '\0');
        // while (file.read(&buf[0], BUFSIZE)) {
        //     cout << "1111 file contents " << buf << endl;
        //     reply.set_buf(buf);
        //     if (!writer->Write(reply)) {
        //         break;
        //     }
        // }
        // // reached eof
        // if (file.eof()) {
        //   cout << "222 EOF file contents " << buf << endl;
        //     buf.resize(file.gcount());
        //     reply.set_buf(buf);
        //     writer->Write(reply);
        // }

        while (!file.eof()) {
          file.read(&buf[0], BUFSIZE);
          cout << "[server log] sending contents " << buf << endl;
          reply.set_buf(buf);
          writer->Write(reply);
        }

        // Do we need this?
        // file.read(&buf[0], BUFSIZE);
        // buf.resize(file.gcount());
        // reply.set_buf(buf);
        // writer->Write(reply);

        file.close();

        return Status::OK;
    }

  Status PutFile(ServerContext *context, ServerReader<PutFileReq> *reader,
        PutFileResp *reply) override {
    PutFileReq request;

    while (reader->Read(&request)) {
      cout << "got request with path " << request.path() <<
       " contents " << request.contents() << endl;
    }

    reply->set_err(0);
    return Status::OK;
  }

  Status Delete(ServerContext *context, const DeleteReq *request,
        DeleteResp *reply) override
  {
    cout << "Recieved Delete RPC from client!" << endl;

    reply->set_err(0);
    return Status::OK;
  }

  private:
    const std::string getServerFilepath(std::string filepath,  bool is_cache_filepath = false) {
        if (is_cache_filepath)
            return (AFS_ROOT_DIR + CACHE + "/" + hashFilepath(filepath));
        else
            return (AFS_ROOT_DIR + FS_ROOT + "/" + filepath);
    }
    const std::string getServerFilepath(const char* filepath, bool is_cache_filepath = false) {
        return getServerFilepath(std::string(filepath), is_cache_filepath);
    }
    void log(char* msg) {
        std::cout << "[log] " << msg << std::endl;
    }
    const std::string hashFilepath(std::string filepath) {
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256_CTX sha256;
            SHA256_Init(&sha256);
            SHA256_Update(&sha256, filepath.c_str(), filepath.size());
            SHA256_Final(hash, &sha256);
            std::stringstream ss;
            for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
                ss << std::hex << std::setw(2) << std::setfill('0') << ((int)hash[i]);
            }
            return ss.str();
    }
};

void RunServer()
{
  string server_address("0.0.0.0:50051");
  FileServerServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;

  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  unique_ptr<Server> server(builder.BuildAndStart());
  cout << "Server listening on " << server_address << endl;

  server->Wait();
}

int main(int argc, char **argv)
{
  RunServer();
  return 0;
}
