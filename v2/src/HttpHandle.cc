#include "HttpHandle.h"


void handleHttp(tcpS_ns::Connection *conn)
{
    if (conn == nullptr || conn->tsp == nullptr)
        return;

    // 循环解析：支持 HTTP 流水线（一个连接连续多个请求）
    while (true)
    {
        HttpRequest req;
        HttpRequest::ParseState state = req.Parse(conn->inbuffer);

        if (state == HttpRequest::ParseState::Incomplete)
            break;

        if (state == HttpRequest::ParseState::Error)
        {
            // 报文非法，回 400 并关闭连接，避免继续解析垃圾数据
            HttpResponse resp = HttpResponse::BadRequest();
            resp.keepAlive = false;
            conn->shoutClose = true;
            conn->outbuffer += resp.ToString();
            break;
        }

        HttpResponse resp;

        // HTTP/1.1 默认长连接，HTTP/1.0 默认短连接
        bool keepAlive = (req.version == "HTTP/1.1");
        auto it = req.headers.find("connection");
        if (it != req.headers.end())
        {
            std::string value = HttpToLower(HttpTrim(it->second));
            if (value == "close")
                keepAlive = false;
            else if (value == "keep-alive")
                keepAlive = true;
        }

        // 只处理 GET，其他方法直接返回 400
        if (req.method != "GET")
        {
            resp = HttpResponse::BadRequest();
            keepAlive = false;
        }
        else
        {
            std::string path = req.path;

            // 去掉查询串 /index.html?a=1 -> /index.html
            size_t query = path.find('?');
            if (query != std::string::npos)
                path.erase(query);

            // URL 解码，防止用 %2e%2e%2f 绕过目录检查
            path = UrlDecode(path);

            // 只允许绝对路径，且禁止目录穿越
            if (path.empty() || path.front() != '/' || path.find("..") != std::string::npos)
            {
                resp = HttpResponse::BadRequest();
                keepAlive = false;
            }
            else
            {
                // 访问根目录 / 自动重定向到 index.html
                if (path == "/")
                    path = "/index.html";

                std::string fullPath = GetWebRoot() + path;
                struct stat st;
                if (stat(fullPath.c_str(), &st) < 0 || !S_ISREG(st.st_mode))
                {
                    // 文件不存在或不是普通文件 → 404
                    resp = HttpResponse::NotFound();
                }
                else
                {
                    std::ifstream ifs(fullPath, std::ios::binary);
                    if (!ifs)
                    {
                        resp = HttpResponse::NotFound();
                    }
                    else
                    {
                        // 一次性读取整个文件到 body
                        std::ostringstream oss;
                        oss << ifs.rdbuf();
                        resp.body = oss.str();
                        resp.contentType = GetMimeType(path);
                        resp.status = 200;
                        resp.statusText = "OK";
                    }
                }
            }
        }

        resp.keepAlive = keepAlive;
        conn->shoutClose = !keepAlive;

        // 把响应报文追加到连接的输出缓冲区，等待发送
        conn->outbuffer += resp.ToString();

        // 短连接：处理完这一个请求就不再解析后续请求
        if (!keepAlive)
            break;
    }

    // 有数据要发就打开 EPOLLOUT，由 Sender 负责发送
    if (!conn->outbuffer.empty())
        tcpS_ns::TcpServer::EnableReadWrite(conn, true, true);
}
