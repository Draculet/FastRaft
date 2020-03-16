#include "NodeInfo.h"
#include <cstring>

using namespace std;

NodeInfo::NodeInfo(int nodeId, string ip, int port)
    :nodeId_(nodeId),
    ip_(ip),
    port_(port),
    name_()
{
    char buf[10] = {0};
    snprintf(buf, sizeof(buf), "node%d", nodeId_);
    name_ = string(buf);
}

string NodeInfo::getIp()
{
    return ip_;
}

int NodeInfo::getPort()
{
    return port_;
}

int NodeInfo::getNodeId()
{
    return nodeId_;
}

string NodeInfo::getName()
{
    return name_;
}