#include <iostream>
#include <memory>
#include "calClient.hpp"

using namespace std;

static void Usage(string proc)
{
    cerr << "Usage: " << proc << " server_ip server_port" << endl;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        Usage(argv[0]);
        exit(1);
    }

    string ip = argv[1];
    uint16_t port = stoi(argv[2]);

    unique_ptr<CalClient> tclt(new CalClient(ip, port));

    tclt->initClient();
    tclt->start();

    return 0;
}