#ifndef ____RAFTNODE_H____
#define ____RAFTNODE_H____

#include <functional>
#include <memory>
#include "NodeInfo.h"
#include "../FastNet/include/Server.h"

class RaftConnection;
class AppendEntriesRPC;
class RequestVoteRPC;
class RaftNode
{
    public:
    RaftNode(NodeInfo info/*包括peer ip port等*/,  std::string name, net::Eventloop *loop, std::function<void(std::shared_ptr<net::Connection>)> cb);
    AppendEntriesRPC *getAppendEntryRpc();
    RequestVoteRPC *getRequestVoteRpc();
    int getNodeId();
    void resetHeartBeatdue(std::function<void()> callback);
    void cancelHeartBeat();
    std::string getName();

    private:
    std::unique_ptr<RaftConnection> conn_;
    std::string name_;
    std::unique_ptr<RequestVoteRPC> requestVoteRpc;
    std::unique_ptr<AppendEntriesRPC> appendEntryRpc;
    net::Eventloop *loop_;
    int nodeId_;
};

#endif