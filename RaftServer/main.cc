#include "RaftServer.h"
#include "ConfigReader.h"

using namespace std;
using namespace net;
using namespace base;

int main(int argc, char *argv[])
{
    vector<NodeInfo> infos;
    getNodeInfo(infos);

    int id;
    if (argc == 2)
    {
        id = atoi(argv[1]);
    }
    else
    {
        printf("Usage: ./main [nodeid] \n");
        return -1;
    }
    if (id < infos.size())
    {
        RaftServer serv(id, infos, infos[id].getPort());
        serv.start();
    }
}