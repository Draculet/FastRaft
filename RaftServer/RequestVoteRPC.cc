#include "RequestVoteRPC.h"
#include "RaftNode.h"
#include "RaftConnection.h"
#include "RequestVoteArg.h"
#include "RpcEncode.h"
#include "../FastNet/include/UnixTime.h"


using namespace std;
using namespace net;
RequestVoteRPC::RequestVoteRPC(RaftConnection *conn)
    :conn_(conn)
{}

void RequestVoteRPC::callInloop(RequestVoteArg arg, shared_ptr<RaftNode> node)
{
    printf("-%s- ReqVote Call In loop\n", UnixTime::now().toString().c_str());
    shared_ptr<Connection> conn = conn_->getConn();
    if (conn != nullptr)
    {
        string data = arg.toString();
        string flag = arg.getFlag();
        occupyRPC(flag);
        conn->send(data);
    }
    else
    {
        //连接经过重连仍无法建立连接,等待延时到再次重试
    }
}

/* *WARNNING* 绑定RaftNode,防止其析构造成crash */
void RequestVoteRPC::call(RequestVoteArg arg, shared_ptr<RaftNode> node)
{   
    conn_->getLoop()->insertQueue(bind(&RequestVoteRPC::callInloop, this, arg, node));
}


void RequestVoteRPC::occupyRPC(string flag)
{
    //锁?
    flag_ = flag;
    busy_ = true;
}

void RequestVoteRPC::releaseRPC()
{
    //锁?
    busy_ = false;
    removeFlag();
}

void RequestVoteRPC::setFlag(string flag)
{
    flag_ = flag;
}

void RequestVoteRPC::removeFlag()
{
    flag_ = "";
}

bool RequestVoteRPC::match(string flag)
{
    if (busy_ && flag == flag_)
    {
        return true;
    }
    else
    {
        return false;
    }
}