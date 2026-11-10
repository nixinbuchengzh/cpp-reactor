#pragma once
#include <string>
#include <sstream>
#include <unordered_map>
#include <fstream>
#include <cctype>
#include <unistd.h>
#include <sys/stat.h>

// 去掉首尾空白
inline std::string HttpTrim(const std::string &s)
{
    size_t begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(begin, end - begin + 1);
}

inline std::string HttpToLower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// 把 %XX 还原成原始字符，用于 URL 路径
inline std::string UrlDecode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2])))
        {
            auto hex = [](char c) {
                if (c >= '0' && c <= '9') return c - '0';
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return c - 'a' + 10;
            };
            out.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
            i += 2;
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}

// 静态文件根目录。用可执行文件所在目录推导，避免依赖启动时的工作目录
inline const std::string &GetWebRoot()
{
    static const std::string root = []() {
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        std::string exe;
        if (n > 0)
            exe.assign(buf, static_cast<size_t>(n));

        size_t slash = exe.find_last_of('/');
        std::string dir = (slash == std::string::npos) ? "." : exe.substr(0, slash);
        return dir + "/../www";
    }();
    return root;
}

class HttpRequest
{
public:
    enum class ParseState
    {
        Incomplete, // 半包，继续等数据
        Complete,   // 解析成功
        Error       // 报文非法
    };

    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;

    // 从 inbuffer 解析，成功返回 Complete 并消费掉这一个请求
    ParseState Parse(std::string &inbuffer)
    {
        // 1. 找头部结束标志 \r\n\r\n
        size_t pos = inbuffer.find("\r\n\r\n");
        if (pos == std::string::npos)
        {
            // 防止恶意客户端一直发头部把内存撑爆
            return inbuffer.size() > kMaxHeaderSize ? ParseState::Error
                                                    : ParseState::Incomplete;
        }
        if (pos > kMaxHeaderSize)
            return ParseState::Error;

        // 先解析到临时对象，全部成功后再消费 inbuffer，避免半途失败丢数据
        HttpRequest req;
        std::string headerStr = inbuffer.substr(0, pos);

        // 2. 解析请求行
        std::istringstream iss(headerStr);
        std::string line;
        if (!std::getline(iss, line))
            return ParseState::Error;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::istringstream lineStream(line);
        if (!(lineStream >> req.method >> req.path >> req.version))
            return ParseState::Error;
        if (req.version.rfind("HTTP/", 0) != 0)
            return ParseState::Error;

        // 3. 解析头部（header 名统一转小写，查找时不区分大小写）
        while (std::getline(iss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            std::string key = HttpToLower(HttpTrim(line.substr(0, colon)));
            std::string val = HttpTrim(line.substr(colon + 1));
            req.headers[key] = val;
        }

        // 4. 如果有请求体，必须等请求体到齐并一起消费掉，否则会影响流水线里的下一个请求
        size_t bodyLen = 0;
        auto it = req.headers.find("content-length");
        if (it != req.headers.end())
        {
            try
            {
                long v = std::stol(it->second);
                if (v < 0)
                    return ParseState::Error;
                bodyLen = static_cast<size_t>(v);
            }
            catch (...)
            {
                return ParseState::Error;
            }
        }
        if (inbuffer.size() < pos + 4 + bodyLen)
            return ParseState::Incomplete;

        inbuffer.erase(0, pos + 4 + bodyLen);
        *this = std::move(req);
        return ParseState::Complete;
    }

private:
    static constexpr size_t kMaxHeaderSize = 8 * 1024;
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

        std::ifstream ifs(GetWebRoot() + "/404.html", std::ios::binary);
        std::ostringstream oss;
        oss << ifs.rdbuf();
        resp.body = oss.str();
        if (resp.body.empty())
            resp.body = "<h1>404 Not Found</h1>";
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
    if (dot == std::string::npos)
        return "application/octet-stream";
    std::string ext = HttpToLower(path.substr(dot));

    static const std::unordered_map<std::string, std::string> mime = {
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".ico", "image/x-icon"},
        {".txt", "text/plain"},
    };
    auto it = mime.find(ext);
    return it == mime.end() ? "application/octet-stream" : it->second;
}
