#pragma once

#include "TcpServer.hpp"
#include "HttpProtocol.hpp"

// 业务函数：输入请求字节，输出响应字节和没解析完的数据
tcpS_ns::BusinessResult handleHttp(std::string data);
