#include "RaftConnection.h"
using namespace std;
using namespace net;
using namespace base;

RaftConnection::RaftConnection(NodeInfo info, Eventloop *loop, function<void(shared_ptr<Connection>)> newRaftConnCallback)
    :loop_(loop),
    binded(false),
    info_(info),
    newRaftConnCallback_(newRaftConnCallback)
{
    //printf("RaftConnection()\n");
    connect();
}

/* 当节点不再是leader时, 需要将所有RaftNode析构,连接也需要释放 */
RaftConnection::~RaftConnection()
{
    //printf("~RaftConnection()\n");
    if (binded)
    {
        shared_ptr<Connection> conn = conn_.lock();
        if (conn != nullptr)
        {
            Eventloop *loop = conn->getLoop();
            loop->runInloop(bind(&Connection::handleClose, conn));
        }
    }
    
}

/*
bool Connect(NodeInfo info)
{
    if (connect() == 0)
    {
        bind_ = true;
    }
    else
    {
        bind_ = false;
    }
}
*/

Eventloop *RaftConnection::getLoop()
{
    return loop_;
}

//getConn只会在rpc->call中被调用
//而call方法只在RaftNode所在线程调用
shared_ptr<Connection> RaftConnection::getConn()
{
    if (binded)
    {
        //TODO lock?
        shared_ptr<Connection> conn = conn_.lock();
        if (conn != nullptr)
        {
            return conn;
        }
    }
    /* !binded || conn == nullptr */
    if (connectInloop())
    {
        shared_ptr<Connection> conn = conn_.lock();
        if (conn != nullptr)
        {
            return conn;
        }
    }
    else
    {
        return nullptr;
    }
}

/* 需要异步 */
int RaftConnection::connectPeer(string ip, int port, NetAddr *addr)
{
    struct sockaddr_in servaddr;
    int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd < 0)
    {
        return -1;
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr);
    int ret = ::connect(clientFd, (struct sockaddr *)(&servaddr), sizeof(servaddr));
    if (ret != 0)
    {
        close(clientFd);
        return -1;
    }
    *addr = NetAddr(servaddr);
    return clientFd;
}

bool RaftConnection::connectInloop()
{
    /*
        使用binded是为了防止构造函数中connect时没有成功
        避免使用conn_时出现错误地情况(空指针)发送
    */
    string ip = info_.getIp();
    int port = info_.getPort();
    NetAddr addr;
    int sockfd = connectPeer(ip, port, &addr);
    if (sockfd < 0)
    {
        //printf("ConnectInloop Failed\n");
        binded = false;
        return false;//重连失败
    }
    else
    {
        //printf("ConnectInloop Succ\n");
        //new shared_ptr
        shared_ptr<Connection> conn(new Connection(sockfd, addr, loop_));
        newRaftConnCallback_(conn);/*RaftServer传来的保存连接callback*/
        conn_ = conn;
        binded = true;
        return true;
    }
}

void RaftConnection::connect()
{
    loop_->runInloop(bind(&RaftConnection::connectInloop, this ));
}
