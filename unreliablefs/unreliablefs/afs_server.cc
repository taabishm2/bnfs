#include <iostream>
#include <memory>
#include <string>
#include <fcntl.h>  /* For O_RDWR */
#include <unistd.h> /* For open(), creat() */
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

using afs::BaseResponse;
using afs::DeleteReq;
using afs::DeleteResp;
using afs::FileServer;
using afs::OpenReq;
using afs::OpenResp;
using afs::PutFileReq;
using afs::PutFileResp;
using afs::ReadDirResponse;
using afs::SimplePathRequest;
using afs::StatResponse;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::Status;

using namespace std;

#define BUFSIZE 65500

char *getServerPath(string path)
{
    const std::string serverDirBasePath = "/users/chrahul5/afs_server_dir";
    std::string fullPath = serverDirBasePath + path;
    char *result = new char[fullPath.size() + 1];
    std::strcpy(result, fullPath.c_str());
    return result;
}

class FileServerServiceImpl final : public FileServer::Service
{
    Status GetAttr(ServerContext *context, const SimplePathRequest *request,
                   StatResponse *reply) override
    {
        struct stat stbuf;
        int res = lstat(getServerPath(request->path()), &stbuf);

        BaseResponse baseResponse;
        if (res != 0)
        {
            baseResponse.set_errorcode(-errno);
        }

        reply->set_dev(stbuf.st_dev);
        reply->set_ino(stbuf.st_ino);
        reply->set_mode(stbuf.st_mode);
        reply->set_nlink(stbuf.st_nlink);
        reply->set_uid(stbuf.st_uid);
        reply->set_gid(stbuf.st_gid);
        reply->set_rdev(stbuf.st_rdev);
        reply->set_size(stbuf.st_size);
        reply->set_blksize(stbuf.st_blksize);
        reply->set_blocks(stbuf.st_blocks);
        reply->set_atime(stbuf.st_atime);
        reply->set_mtime(stbuf.st_mtime);
        reply->set_ctime(stbuf.st_ctime);

        reply->mutable_baseresponse()->CopyFrom(baseResponse);
        return Status::OK;
    }

    Status ReadDir(ServerContext *context, const SimplePathRequest *request,
                   ReadDirResponse *reply)
    {
        DIR *dp;
        struct dirent *de;

        dp = opendir(getServerPath(request->path()));

        BaseResponse baseResponse;
        if (dp == NULL)
        {
            baseResponse.set_errorcode(-errno);
        }
        else
        {
            while ((de = readdir(dp)) != NULL)
                reply->add_dirname(de->d_name);
        }

        closedir(dp);

        reply->mutable_baseresponse()->CopyFrom(baseResponse);
        return Status::OK;
    }

    Status Mkdir(ServerContext *context, const SimplePathRequest *request,
                 BaseResponse *reply)
    {
        cout << "SERVER: Mkdir " << request->path().c_str() << endl;

        int res = mkdir(getServerPath(request->path()), 777);

        if (res == -1)
            reply->set_errorcode(-errno);
            
        return Status::OK;
    }

    Status Rmdir(ServerContext *context, const SimplePathRequest *request,
                 BaseResponse *reply)
    {
        int res = rmdir(getServerPath(request->path()));

        if (res == -1)
            reply->set_errorcode(-errno);

        return Status::OK;
    }

    Status Open(ServerContext *context, const OpenReq *request,
                ServerWriter<OpenResp> *writer) override
    {
        cout << "Recieved Open RPC from client!" << endl;
        OpenResp reply;
        string path = getServerPath(request->path());
        ifstream file(path, ios::in);
        cout << "Opening: " << path << "\n";

        if (!file.is_open())
        {
            reply.set_file_exists(false);
            if (request->is_create()) {
                if (open(path.c_str(), request->flag(), 00777) == -1) {
                    reply.set_err(errno);
                }
            }
            writer->Write(reply);
            return Status::OK;
        }
        cout << "File exists";
        reply.set_file_exists(true);

        string buf(BUFSIZE, '\0');
         cout << "Reading";
        while (file.read(&buf[0], BUFSIZE))
        {
             cout << "set buf";
            reply.set_buf(buf);
            if (!writer->Write(reply))
                break;
        }
         cout << "reached eof";
        // reached eof
        if (file.eof())
        {
            buf.resize(file.gcount());
            reply.set_buf(buf);
            writer->Write(reply);
        }
         cout << "closing File";
        file.close();
        // reply.set_err(read_res);
        return Status::OK;
    }

    Status PutFile(ServerContext *context, ServerReader<PutFileReq> *reader,
        PutFileResp *reply) override {
        PutFileReq request;

        while (reader->Read(&request)) {
            cout << "got PutFile request for path " << request.path() <<
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
    void log(char *msg)
    {
        std::cout << "[log] " << msg << std::endl;
    }
    const std::string hashFilepath(std::string filepath)
    {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, filepath.c_str(), filepath.size());
        SHA256_Final(hash, &sha256);
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        {
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
