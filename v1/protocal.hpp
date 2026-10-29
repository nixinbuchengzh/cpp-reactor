#pragma once

#include <iostream>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>

using namespace std;

#define SEP " "
#define SEP_LEN strlen(SEP)
#define LINE_SEP "\r\n"
#define LINE_SEP_LEN strlen(LINE_SEP)

enum
{
    OK = 0,
    DIV_ZERO,
    MOD_ZERO,
    OP_ERROR
};

string enLength(const string &text)
{
    string len = to_string(text.size());
    string send_string = len + LINE_SEP + text + LINE_SEP;
    return send_string;
}

bool deLength(const string &text, string *out)
{
    int pos = text.find(LINE_SEP);
    if (pos == string::npos)
        return false;
    int len = stoi(text.substr(0, pos));
    *out = text.substr(pos + LINE_SEP_LEN, len);
    return true;
}

class Request
{
public:
    Request() : _x(0), _y(0), _op(0)
    {
    }
    Request(int x, int y, char op) : _x(x), _y(y), _op(op)
    {
    }

    bool seriasize(string *out)
    {
        *out = to_string(_x) + SEP + _op + SEP + to_string(_y);
    }

    bool deseriasize(const string &in)
    {
        int pos1 = in.find(SEP);
        int pos2 = in.rfind(SEP);
        if (pos1 == string::npos || pos1 == pos2)
            return false;

        _x = stoi(in.substr(0, pos1));
        _y = stoi(in.substr(pos2 + SEP_LEN));
        _op = in[pos1 + SEP_LEN];
        return true;
    }

    ~Request() {}

public:
    int _x;
    int _y;
    char _op;
};

class Response
{
public:
    Response() : result(0), exitcode(0)
    {
    }
    bool seriasize(string *out)
    {
        *out = to_string(result) + SEP + to_string(exitcode);
        return true;
    }

    bool deseriasize(const string &in)
    {
        int pos = in.find(SEP);
        if (pos == string::npos)
            return false;
        result = stoi(in.substr(0, pos));
        exitcode = stoi(in.substr(pos + SEP_LEN));
        return true;
    }

public:
    int result;
    int exitcode;
};

bool ParseRequest(string &inbuffer, string *text)
{
    int pos = inbuffer.find(LINE_SEP);
    if (pos == std::string::npos) return false;

    int len = stoi(inbuffer.substr(0, pos));
    int totle_len = len + 2 * LINE_SEP_LEN + pos;
    if ((int)inbuffer.size() < totle_len) return false;
    
    *text = inbuffer.substr(0, totle_len);
    inbuffer.erase(0, totle_len);

    return true;
}

bool recvRequest(int sock, string &inbuffer, string *text)
{
    char buffer[1024];
    while (true)
    {
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n > 0)
        {
            std::cout << "DEBUG IN" << std::endl;
            buffer[n] = 0;
            inbuffer += buffer;
            int pos = inbuffer.find(LINE_SEP);
            if (pos == string::npos)
            {
                std::cout << "error1" << std::endl;
                continue;
            }
            int len = stoi(inbuffer.substr(0, pos));
            int totle_len = len + 2 * LINE_SEP_LEN + pos;
            if (totle_len > inbuffer.size())
            {
                std::cout << "error2" << std::endl;
                continue;
            }
            *text = inbuffer.substr(0, totle_len);
            inbuffer.erase(0, totle_len);
            std::cout << "DEBUG OUT" << std::endl;
            return true;
        }
        else
            return false;
    }
    return true;
}