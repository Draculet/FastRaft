#include "RaftNode.h"
#include "RequestVoteRPC.h"
#include "AppendEntriesRPC.h"
#include "RaftConnection.h"
#include "../FastNet/include/UnixTime.h"

using namespace std;
using namespace net;

RaftNode::RaftNode(NodeInfo info/*包括peer ip port等*/,  string name, Eventloop *loop, function<void(shared_ptr<Connection>)> cb)
    :conn_(new RaftConnection(info, loop, cb)),
    name_(info.getName()),
    requestVoteRpc(new RequestVoteRPC(&*conn_)),
    appendEntryRpc(new AppendEntriesRPC(&*conn_)),
    loop_(loop),
    nodeId_(info.getNodeId())
{}


AppendEntriesRPC *RaftNode::getAppendEntryRpc()
{
    return &*appendEntryRpc;
}

RequestVoteRPC *RaftNode::getRequestVoteRpc()
{
    return &*requestVoteRpc;
}

int RaftNode::getNodeId()
{
    return nodeId_;
}

void RaftNode::resetHeartBeatdue(function<void()> callback)
{
    printf("-%s- ResetHeartBeat  nodeid: %d\n",UnixTime::now().toString().c_str(), nodeId_);
    int ms = 50;/* election due */
    //ms *= 100;
    double sec = ms * 0.001;
    loop_->runTimeAfter(sec, callback, name_);
}

void RaftNode::cancelHeartBeat()
{
    loop_->cancelTime(name_);
}

string RaftNode::getName()
{
    return name_;
}