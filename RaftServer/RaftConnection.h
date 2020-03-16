#ifndef _______RAFTCONNECTION_H_______
#define _______RAFTCONNECTION_H_______

#include "NodeInfo.h"
#include "../FastNet/include/Eventloop.h"
#include "../FastNet/include/Connection.h"

class RaftConnection
{
    /* connect()在loop_所属线程调用 */
    public:
    RaftConnection(NodeInfo info, net::Eventloop *loop, std::function<void(std::shared_ptr<net::Connection>)> newRaftConnCallback);
    ~RaftConnection();
    net::Eventloop *getLoop();
    std::shared_ptr<net::Connection> getConn();
    int connectPeer(std::string ip, int port, net::NetAddr *addr);
    bool connectInloop();
    void connect();

    private:
    net::Eventloop *loop_;
    bool binded;
    NodeInfo info_;
    std::weak_ptr<net::Connection> conn_;
    std::function<void(std::shared_ptr<net::Connection>)> newRaftConnCallback_;
};

#endif