#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "afs.grpc.pb.h"

using afs::FileServer;
using afs::OpenReq;
using afs::OpenResp;
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
