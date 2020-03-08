#include "demo.h"

int main(void)
{
    NodeInfo info0(0, "127.0.0.1", 8831);
    NodeInfo info1(1, "127.0.0.1", 8832);
    NodeInfo info2(2, "127.0.0.1", 8833);
    NodeInfo info3(3, "127.0.0.1", 8834);
    NodeInfo info4(4, "127.0.0.1", 8835);

    vector<NodeInfo> infos;
    infos.push_back(info0);
    infos.push_back(info1);
    infos.push_back(info2);
    infos.push_back(info3);
    infos.push_back(info4);
    int id;
    cin >> id;
    if (id < 5)
    {
        RaftServer serv(id, infos, infos[id].getPort());
        serv.start();
    }
}