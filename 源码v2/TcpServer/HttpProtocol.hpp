#pragma once
#include <string>
#include <sstream>
#include <unordered_map>
#include <fstream>
#include <sys/stat.h>

class HttpRequest
{
public:
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;

    // 从 inbuffer 解析，成功返回 true 并消费掉请求
    bool Parse(std::string &inbuffer)
    {
        // 1. 找头部结束标志 \r\n\r\n
        size_t pos = inbuffer.find("\r\n\r\n");
        if (pos == std::string::npos) return false;  // 半包，等下次

        std::string headerStr = inbuffer.substr(0, pos);
        inbuffer.erase(0, pos + 4);  // 消费掉头部

        // 2. 解析请求行
        std::istringstream iss(headerStr);
        std::string line;
        if (!std::getline(iss, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream lineStream(line);
        if (!(lineStream >> method >> path >> version)) return false;

        // 3. 解析头部
        while (std::getline(iss, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            // 去掉 val 前导空格
            size_t start = val.find_first_not_of(" ");
            if (start != std::string::npos) val = val.substr(start);
            headers[key] = val;
        }
        return true;
    }
};

class HttpResponse
{
public:
    int status = 200;
    std::string statusText = "OK";
    std::string contentType = "text/plain";
    std::string body;
    bool keepAlive = false;

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << statusText << "\r\n";
        oss << "Content-Type: " << contentType << "\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
        oss << "\r\n";
        oss << body;
        return oss.str();
    }

    static HttpResponse NotFound()
    {
        HttpResponse resp;
        resp.status = 404;
        resp.statusText = "Not Found";
        resp.contentType = "text/html";
        std::string path = "./www/404.html";
        std::ifstream ifs(path, std::ios::binary);
        std::ostringstream oss;
        oss << ifs.rdbuf();
        resp.body = oss.str();
        return resp;
    }

    static HttpResponse BadRequest()
    {
        HttpResponse resp;
        resp.status = 400;
        resp.statusText = "Bad Request";
        resp.contentType = "text/html";
        resp.body = "<h1>400 Bad Request</h1>";
        return resp;
    }
};

// MIME 类型映射
inline std::string GetMimeType(const std::string &path)
{
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);

    static const std::unordered_map<std::string, std::string> mime = {
        {".html", "text/html"},
        {".htm",  "text/html"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".json", "application/json"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".ico",  "image/x-icon"},
        {".txt",  "text/plain"},
    };
    auto it = mime.find(ext);
    return it == mime.end() ? "application/octet-stream" : it->second;
}