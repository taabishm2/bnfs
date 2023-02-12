#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <sys/stat.h>
#include <dirent.h>

#include "afs.grpc.pb.h"

using afs::FileServer;
using afs::OpenReq;
using afs::OpenResp;
using afs::ReadDirResponse;
using afs::SimplePathRequest;
using afs::StatResponse;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using namespace std;

class FileServerServiceImpl final : public FileServer::Service
{
    Status Open(ServerContext *context, const OpenReq *request,
                OpenResp *reply) override
    {
        cout << "Recieved RPC from client!" << endl;

        reply->set_err(0);
        return Status::OK;
    }

    Status GetAttr(ServerContext *context, const SimplePathRequest *request,
                   StatResponse *reply) override
    {
        cout << "Recieved GetAttr RPC from client!" << endl;

        struct stat stbuf;
        // int res = lstat(request->path().c_str, &stbuf);
        int res = lstat("/testdir", &stbuf);
        if (res == -1)
            res = -errno;

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

    Status ReadDir(ServerContext *context, const SimplePathRequest *request,
                   ReadDirResponse *reply)
    {

        cout << "Recieved ReadDir RPC from client!" << endl;
        DIR *dp;
        int res = 0;
        struct dirent *de;

        // dp = opendir(request->path().c_str());
        dp = opendir("/testdir");
        if (dp == NULL)
            res = -errno;

        while ((de = readdir(dp)) != NULL)
        {
            reply->add_dirname(de->d_name);
        }

        closedir(dp);
        return Status::OK;
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
