#include "TcpServer.hpp"
#include "HttpProtocol.hpp"
#include "HttpHandle.h"
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <memory>

using namespace tcpS_ns;

static void usage(std::string proc)
{
    std::cerr << "Usage:\n\t" << proc << " port"
              << "\n\n";
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        usage(argv[0]);
        return USAGE_ERR;
    }

    uint16_t port = atoi(argv[1]);

    std::unique_ptr<TcpServer> tsvr(new TcpServer(port));
    // 在initServer之前注册好业务处理函数
    TcpServer::SetHandler(handleHttp);
    tsvr->initServer();
    tsvr->Dispatcher();

    return 0;
}
