#pragma once

#include "log.hpp"
#include "protocal.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class CalClient
{
public:
    CalClient(const std::string &ip, const uint16_t &port)
        : _sock(-1), _serverip(ip), _serverport(port)
    {
    }

    void initClient()
    {
        _sock = socket(AF_INET, SOCK_STREAM, 0);
        if (_sock < 0)
        {
            std::cerr << "socket creat error" << std::endl;
        }
    }

    void start()
    {
        struct sockaddr_in server;
        memset(&server, 0, sizeof(server));
        server.sin_addr.s_addr = inet_addr(_serverip.c_str());
        server.sin_family = AF_INET;
        server.sin_port = htons(_serverport);

        if (connect(_sock, (struct sockaddr *)&server, sizeof(server)))
        {
            std::cerr << "connect error" << std::endl;
        }
        else
        {
            std::string line, inbuffer;
            while (true)
            {
                std::cout << "please Enter: ";
                std::getline(std::cin, line);

                Request req = ParseLine(line);
                std::string content, send_str;
                req.seriasize(&content);
                send_str = enLength(content);
                send(_sock, send_str.c_str(), send_str.size(), 0);

                std::string package, text;
                if (!recvRequest(_sock, inbuffer, &package))
                {
                    std::cout << "recvRequest error" << std::endl;
                    continue;
                }
                if (!deLength(package, &text))
                {
                    std::cout << "deLength error" << std::endl;
                    continue;
                }

                Response resp;
                resp.deseriasize(text);
                std::cout << "exitcode: " << resp.exitcode << std::endl;
                std::cout << "result: " << resp.result << std::endl;
            }
        }
    }

    // Request ParseLine(const std::string &line)
    // {
    //     int status = 0;
    //     int i = 0;
    //     int cnt = line.size();
    //     std::string left, right;
    //     char op;
    //     while (i < cnt)
    //     {
    //         switch (status)
    //         {
    //         case 0:
    //         {
    //             if(!isdigit(line[i]))
    //             {
    //                 op = line[i];
    //                 status = 1;
    //             }
    //             else left.push_back(line[i++]);
    //         }
    //         break;
    //         case 1:
    //             i++;
    //             status = 2;
    //             break;
    //         case 2:
    //             right.push_back(line[i++]);
    //             break;
    //         }
    //     }
    //     //std::cout << std::stoi(left)<<" " << std::stoi(right) << " " << op << std::endl;
    //     return Request(std::stoi(left), std::stoi(right), op);
    // }

    Request ParseLine(const std::string &line)
    {
        int x, y;
        char op;
        std::stringstream ss(line);
        ss >> x >> op >> y;
        return Request(x, y, op);
    }

    ~CalClient() 
    {
        if (_sock != -1) close(_sock);
    }

private:
    int _sock;
    std::string _serverip;
    uint16_t _serverport;
};