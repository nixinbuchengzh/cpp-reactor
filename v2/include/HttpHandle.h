#pragma once

#include "TcpServer.hpp"
#include "HttpProtocol.hpp"

void handleHttp(tcpS_ns::Connection *conn);
