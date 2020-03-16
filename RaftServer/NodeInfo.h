#ifndef ________NODEINFO________
#define ________NODEINFO________

#include <string>

struct NodeInfo
{
    public:
    NodeInfo(int nodeId, std::string ip, int port);
    std::string getIp();
    int getPort();
    int getNodeId();
    std::string getName();


    int nodeId_;
    std::string ip_;
    int port_;
    std::string name_;
};

#endif