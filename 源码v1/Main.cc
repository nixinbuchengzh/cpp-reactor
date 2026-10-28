#include "TcpServer.hpp"
#include <memory>

using namespace tcpS_ns;

static void usage(std::string proc)
{
    std::cerr << "Usage:\n\t" << proc << " port"
              << "\n\n";
}

bool cal(const Request &req, Response &resp)
{
    resp.exitcode = OK;
    resp.result = OK;

    switch (req._op)
    {
    case '+':
        resp.result = req._x + req._y;
        break;
    case '-':
        resp.result = req._x - req._y;
        break;
    case '*':
        resp.result = req._x * req._y;
        break;
    case '/':
    {
        if (req._y == 0)
            resp.exitcode = DIV_ZERO;
        else
            resp.result = req._x / req._y;
    }
    break;
    case '%':
    {
        if (req._y == 0)
            resp.exitcode = MOD_ZERO;
        else
            resp.result = req._x % req._y;
    }
    break;
    default:
        resp.exitcode = OP_ERROR;
        break;
    }

    return true;
}


void calculate(Connection *conn)
{
    std::string Package;
    while (ParseRequest(conn->inbuffer, &Package))
    {
        std::string reqStr;
        if (!deLength(Package, &reqStr))
            return;
        std::cout << "去掉报头的正文：\n"
                  << reqStr << std::endl;

        Request req;
        if (!req.deseriasize(reqStr))
            return;

        Response resp;
        cal(req, resp);

        std::string respStr;
        resp.seriasize(&respStr);
        std::string send_string = enLength(respStr);
        conn->outbuffer += send_string;

        //logMessage(DEBUG,"respons: %s",respStr.c_str());
        if (conn->_send) 
        {
            conn->_send(conn);
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        usage(argv[0]);
        exit(USAGE_ERR);
    }
    uint16_t port = atoi(argv[1]);

    std::unique_ptr<TcpServer> tsvr(new TcpServer(calculate, port));
    tsvr->initServer();
    tsvr->Dispatcher();

    return 0;
}