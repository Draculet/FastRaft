#include "AppendEntriesRPC.h"
#include "RaftConnection.h"
#include "AppendEntryArg.h"
#include "RaftNode.h"
#include "RpcEncode.h"

using namespace std;
using namespace net;

AppendEntriesRPC::AppendEntriesRPC(RaftConnection *conn)
    :busy_(false),
    conn_(conn),
    flag_("")
{}

void AppendEntriesRPC::call(AppendEntryArg arg, shared_ptr<RaftNode> node)
{
    conn_->getLoop()->insertQueue(bind(&AppendEntriesRPC::callInloop, this, arg, node));
}


void AppendEntriesRPC::callInloop(AppendEntryArg arg, shared_ptr<RaftNode> node)
{
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


/* 下列操作都在持有锁状态下进行 */
void AppendEntriesRPC::occupyRPC(string flag)
{
    //锁?
    flag_ = flag;
    busy_ = true;
}

void AppendEntriesRPC::releaseRPC()
{
    //锁?
    busy_ = false;
    removeFlag();
}

void AppendEntriesRPC::setFlag(string flag)
{
    flag_ = flag;
}

void AppendEntriesRPC::removeFlag()
{
    flag_ = "";
}

bool AppendEntriesRPC::match(string flag)
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