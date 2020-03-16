#include <iostream>
#include <stdio.h>
#include <list>
#include <memory>
#include <functional>
#include <set>
#include "../Server.h"
#include "../Connection.h"

using namespace std;
using namespace net;
using namespace base;
/* tip: 接受到ApdRpc需要重置时间 */
/*  tip:  electdue是一个位于主线程loop的计时器,负责本节点的选举计时,Heartbeat则是位于每个raftNode所属的loop的计时器,负责RPC计时  */

/* 
    tip: fastNet中对新建连接的处理,是在主线程new Connection,然后再将shared_ptr指针交给分配给这个连接的线程,这是unsafe的
    原因是主线程和conn所属线程都访问conn, 主线程中针对conn调用的各种set函数理论上应该加所属线程的锁,但这会引起所属线程访问conn也需要加锁,得不偿失
    本程序中将conn的构建移到了conn被分配的线程上,使conn的访问仅在其所属线程进行,不用加锁
*/

/* onXXXRead 都需要加锁,锁置于解析完buf之后. statMachine_是共享资源,涉及对其更新和读取 */

//vector<nodeInfo> nodes[nodenum - 1];
//vector<Conntion> conns[nodenum - 1];
//getName[conn1] = "node1";
//getName[conn2] = "node2";
//getName[conn3] = "node3";
//getName[conn4] = "node4";


struct NodeInfo
{
    public:
    NodeInfo(int nodeId, string ip, int port)
        :nodeId_(nodeId),
        ip_(ip),
        port_(port),
        name_()
    {
        char buf[10] = {0};
        snprintf(buf, sizeof(buf), "node%d", nodeId_);
        name_ = string(buf);
    }

    NodeInfo()
    {}

    string getIp()
    {
        return ip_;
    }

    int getPort()
    {
        return port_;
    }

    int getNodeId()
    {
        return nodeId_;
    }

    string getName()
    {
        return name_;
    }

    int nodeId_;
    string ip_;
    int port_;
    string name_;
};

/* FIXME raftConn与raftNode同长,理应不会有问题 */
class RaftConnection
{
    /* connect()在loop_所属线程调用 */
    public:
    RaftConnection(NodeInfo info, Eventloop *loop, function<void(shared_ptr<Connection>)> newRaftConnCallback)
        :info_(info),
        loop_(loop),
        newRaftConnCallback_(newRaftConnCallback)
    {
        printf("RaftConn\n");
        connect();
    }

    /* 当节点不再是leader时, 需要将所有raftNode析构,连接也需要释放 */
    ~RaftConnection()
    {
        printf("~RaftConn\n");
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

    Eventloop *getLoop()
    {
        return loop_;
    }

    //getConn只会在rpc->call中被调用
    //而call方法只在raftNode所在线程调用
    shared_ptr<Connection> getConn()
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
            //TODO lock?
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

    int connectPeer(string ip, int port, NetAddr *addr)
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
        int ret = ::connect(clientFd, (struct sockaddr *)&servaddr, sizeof(servaddr));
        if (ret != 0)
        {
            close(clientFd);
            return -1;
        }
        *addr = NetAddr(servaddr);
        return clientFd;
    }

    bool connectInloop()
    {
        /*
            使用binded是为了防止构造函数中connect时没有成功
            避免使用conn_时出现错误地情况(空指针)发送
        */
        //TODO lock?
        string ip = info_.getIp();
        int port = info_.getPort();
        NetAddr addr;
        int sockfd = connectPeer(ip, port, &addr);
        if (sockfd < 0)
        {
            printf("\n\nconnect failed\n");
            binded = false;
            return false;//重连失败
        }
        else
        {
            printf("\n\nconnect success\n");
            //new shared_ptr
            shared_ptr<Connection> conn(new Connection(sockfd, addr, loop_));
            newRaftConnCallback_(conn);/*raftServer传来的保存连接callback*/
            conn_ = conn;
            binded = true;
            return true;
        }
    }

    void connect()
    {
        loop_->runInloop(bind(&RaftConnection::connectInloop, this ));
    }

    private:
    Eventloop *loop_;
    bool binded;
    NodeInfo info_;
    weak_ptr<Connection> conn_;
    function<void(shared_ptr<Connection>)> newRaftConnCallback_;
};

class RPC
{
    /*
        conn->getConn返回false的话,HeartBeat due更新
        返回true则avail设false,HeartBeat due更新
    */
    public:
    RPC(RaftConnection *conn)
        :conn_(conn)
    {
        
    }

    void call(string arg)
    {
        conn_->getLoop()->runInloop(bind(&RPC::callInloop, this, arg));

    }

    void callInloop(string arg)
    {
        shared_ptr<Connection> conn = conn_->getConn();
        if (conn != nullptr)
        {
            printf("\n\n\nsend %s\n\n\n", arg.c_str());
            conn->send(arg);
        }

    }

    private:
    /* new */
    RaftConnection *conn_;
};




/* 每个raftNode属于一个Eventloop */
//raftNode是对其余节点的抽象
/* raftNode 需要用shared_ptr保存 */
class raftNode
{
    public:
    raftNode(NodeInfo info/*包括peer ip port等*/,  string name, Eventloop *loop, function<void(shared_ptr<Connection>)> cb)
        :conn_(new RaftConnection(info, loop, cb)),
        name_(name),
        rpc(new RPC(&*conn_)),
        //appendEntriesRpc(&*conn_),
        loop_(loop)
    {}

    ~raftNode()
    {
        printf("~raftNode");    
    }

    
    void resetHeartBeatdue(function<void()> callback)
    {
        int ms = 50;/* election due */
        double sec = ms * 0.1;
        loop_->runTimeAfter(sec, callback, name_);
    }

    void cancelHeartBeat()
    {
        loop_->cancelTime(name_);
    }

    RPC *getRpc()
    {
        return &*rpc;
    }

    private:
    unique_ptr<RaftConnection> conn_;
    string name_;
    unique_ptr<RPC> rpc;
    //uniqeu_ptr<RequestVoteRPC> reqVoteRpc;
    //unique_ptr<AppendEntriesRPC> appendEntryRpc;
    //unique_ptr<VoteReqRPC> appendEntryRpc;
    Eventloop *loop_;
};

class Log
{
    public:
    Log(int term, int index, int logId, string oper, string key, string value)
        :term_(term),
        index_(index),
        logId_(logId),
        oper_(oper), /* oper: set get del*/
        key_(key),
        value_(value) /* oper为get del时, value_为"" */
        
    {}

    Log()
    {}

    string toString()
    {
        
    }

    static Log fromString(Buffer *buf)
    {
        
    }

    int term_;
    int index_;
    int logId_;//作用是client的幂等性
    string oper_;
    string key_;
    string value_;
};



int nodenum = 4;



class StateMachine
{
    public:
    StateMachine(int nodeId)
        :logs(2),
        firstLogIndex(0),
        lastLogIndex(0),
        nextIndex(nodenum, lastLogIndex + 1), /*将要同步的日志*/
        matchIndex(nodenum, 0),
        nodeId_(nodeId)
    {}

    vector<Log> getEntry(int nextIndex)
    {
        vector<Log> tmp;
        if (lastLogIndex >= nextIndex)
        {
            //每次只取一个日志
            tmp.push_back(logs[nextIndex]);
        }
        return tmp;
    }

    /* 应在插入日志之后init */
    void leaderInit()
    {
        for (int i = 0; i < nextIndex.size(); i++)
        {
            if (i != nodeId_)
            {
                nextIndex[i] = lastLogIndex + 1;
            }
        }

        /* 
        for (auto &index : nextIndex)
        {
            index = lastLogIndex + 1;
        }
        */
        for (auto &index : matchIndex)
        {
            index = 0;
        }
    }


    void insertLog(int index, Log log)
    {
        int size = logs.size();
        if (index <= size - 1)
        {
            logs[index] = log;
            lastLogIndex = index;
            nextIndex[nodeId_] = lastLogIndex + 1;
            if (lastLogIndex == size - 1)
            {
                //logs.push_back(Log());
            }
            //覆盖日志无需开辟空间
        }
        else
        {
            //报错
        }
        
    }

    private:

    int currentTerm = 0;
    int voteFor = -1;
    vector<Log> logs;//第一个index为1
    int commitIndex = 0;
    int lastApplied = 0;
    //是否更改为map?
    vector<int> nextIndex; //size为节点数,初始化为 lastLogIndex + 1;
    vector<int> matchIndex; //size同上,初始化为0
    int firstLogIndex = 0;/*使用快照则需要此字段*/
    int lastLogIndex = 0;
    int nodeId_;
};

class FlagBuilder
{
    public:
    static string getFlag()
    {
        struct timeval tval;
        gettimeofday(&tval, nullptr);
        const int million = 1000000;
        int64_t n = tval.tv_sec * million + tval.tv_usec;
        char buf[20] = {0};
        snprintf(buf, sizeof(buf), "%ld", n);
        return string(buf + 12, 4);
    }
};


class raftServer
{
    public: /* new */
    raftServer(int nodeId, vector<NodeInfo> infos, int port)
        :serv_(new Server(4, port)), 
        conns_( new set< shared_ptr<Connection> >() ),
        nodes_(nullptr),
        statMachine_(new StateMachine(nodeId) ),
        role_(follower),
        nodeId_(nodeId),
        voteGranted_(0),
        infos_(infos),
        mutex_()
    {
        serv_->setReadCallback( bind(&raftServer::onRpcReqRead, this, placeholders::_1, placeholders::_2) );
        //serv_->getLoop()->runTimeAfter(20, bind(&raftServer::Test, this),  "test");
    }

 /*    void Test()
    {
        printf("TEST--------\n");
        bool isFollow;
        {
            MutexGuard guard(mutex_);
            if (role_ == leader)
                isFollow = true;
            else
                isFollow = false;
        }
        if (isFollow)
        {
            becomeFollower();
        }
    }
*/
    void onRpcRespRead(Buffer *buf, shared_ptr<Connection> conn)
    {
        string resp = buf->retrieveAllAsString();
        MutexGuard guard(mutex_);
        printf("\n\n\n\nrecv str:%s\n", resp.c_str() );
        if (resp == "yes" && role_ == candidate)
        {
            voteGranted_++;
            if (voteGranted_ > 1)
            {
                printf("\n\n\nBecomeLeader\n\n\n\n");
                becomeLeaderWithLock();
            }
        }
        else if (resp == "copy")
        {
            printf("leader heartbeat Ok\n");
        }
    }

    void onRpcReqRead(Buffer *buf, shared_ptr<Connection> conn)
    {
        string req = buf->retrieveAllAsString();
        printf("receive %s\n", req.c_str());
        if (req == "VoteReq")
        {
            MutexGuard guard(mutex_);
            if (role_ == follower)
            {
                conn->send("yes");
                resetElectionDue();
            }
            else
                conn->send("no");
        }
        else if (req == "Append")
        {
            MutexGuard guard(mutex_);
            if (role_ != follower)
            {
                conn->send("ok");
                resetElectionDue();
                becomeFollower();
            }
            else
            {
                conn->send("copy");
                resetElectionDue();
                printf("follower Heartbeat OK\n");
            }
        }
        else
        {
            printf("error\n");
        }
    }/* end onReadReq */

    void start()
    {
        srand(time(nullptr));
        int electms = rand( ) % 100 + 100;/* election due  100ms-200ms */ 
        //cout << "\n\nRand elect get electsec" << electms * 0.001 << "\n" << endl;
        double electsec = electms * 0.1;                  /* new */
        serv_->getLoop()->runTimeAfter(electsec, bind(&raftServer::becomeCandidate, this) , "election");
        serv_->start();
    }
    
    void becomeCandidate()
    {
        printf("\n\n\n\nbecomeCandidate\n\n\n\n");
        //锁
        /*
            建立连接并发送VotReq
            raftNode的建立会建立连接
            连接raftConn只包含Conn的weak_ptr
            Conn的shared_ptr在raftServer
            即Conn生命的保证在raftServer
        */
        /* 由于rpc接下来会被更新,无需重新初始化rpc,即使由于role不相符无法被release的rpc也不会造成问题,因为flag无法match */
        MutexGuard guard(mutex_);

        if (role_ == follower)
        {
            nodes_.reset(nullptr);
            if (!nodes_)
            {
                nodes_.reset(new vector< shared_ptr<raftNode> >());
                for (auto &info : infos_)
                {
                    if (info.getNodeId() != nodeId_)
                    {
                        printf("nodeptr before\n");
                        shared_ptr<raftNode> nodeptr(new raftNode(
                            info, info.getName(), serv_->getNextLoop()
                                , bind(&raftServer::newRaftConnCallback, this, placeholders::_1)));
                        //printf("add before\n");
                        nodes_->push_back(nodeptr);
                    }
                    printf("nodeptr add\n");
                    /* raftNoode构造时就直接建立tcp连接 */
                    /* newRaftConnCallback做绑定Connection和ConnectionEstablish的调用 */
                }
            }
            role_ = candidate;
        }
        /* 选举超时的情况 */
       
        if (role_ == candidate)
        {
            if (nodes_)
            {
                for (auto &node : *nodes_)
                {
                    //string flag = FlagBuilder::getFlag();
                    RPC *rpc = node->getRpc();
                    cout << "\n\nrpcCall\n\n" << endl;
                    rpc->call(string("VoteReq"));
                    node->resetHeartBeatdue( bind(&raftServer::requestVoteDueCallback, this, weak_ptr<raftNode>(node)) ); 
                    /* tips: 此处若不先构建weak_ptr对象则存入的是shared_ptr,会造成raftNode引用计数无法变为0 */


                    //每个节点发RPC
                    //在onRPCRead中更新选票数
                     //最终如果选票达到大多数,调用becomeLeader
                }
            }
        }
        resetElectionDue();
    }
    
    void requestVoteDueCallback(weak_ptr<raftNode> node)
    {
        MutexGuard guard(mutex_);
        if (role_ == candidate)
        {
            shared_ptr<raftNode> ptr = node.lock();
            if (ptr)
            {
                string flag = FlagBuilder::getFlag();
                RPC *rpc = ptr->getRpc();
                rpc->call(string("VoteReq") + flag);
                ptr->resetHeartBeatdue(bind(&raftServer::requestVoteDueCallback, this, weak_ptr<raftNode>(node)));
            }
        }
    }

    void appendEntryDueCallback(weak_ptr<raftNode> node)
    {
        MutexGuard guard(mutex_);
        if (role_ == leader)
        {
            shared_ptr<raftNode> ptr = node.lock();
            if (ptr)
            {
                //string flag = FlagBuilder::getFlag();
                RPC *rpc = ptr->getRpc();
                    printf("Call Append\n");
                rpc->call(string("Append"));
                ptr->resetHeartBeatdue(bind(&raftServer::appendEntryDueCallback, this, weak_ptr<raftNode>(node)));
            }
        }
    }

    void becomeLeaderWithLock()
    {
        cancelElectTime();
        printf("\n\n\n\nBecome Leader\n\n\n\n");
        printf("I'm OK before\n\n");
        //MutexGuard guard(mutex_);
        /* **FIXME** 由新日志请求来触发日志同步 */
        //statMachine_->leaderInit();
        //statMachine_->insertLog(/* empty op */);
        //TODO 调用用户请求函数插入一个空操作的日志

        printf("I'm OK\n\n");
        role_ = leader;
        if (nodes_)
        {
            printf("I'm still OK\n\n");
            for (auto &node : *nodes_)
            {   
                //string flag = FlagBuilder::getFlag();
                printf("Call Append\n");
                RPC *rpc = node->getRpc();
                rpc->call(string("Append"));
                node->resetHeartBeatdue(bind(&raftServer::appendEntryDueCallback, this, weak_ptr<raftNode>(node)));
            }
        }
    }



    //在conn对应的线程调用
    void newRaftConnCallback(shared_ptr<Connection> conn)
    {
        MutexGuard guard(mutex_);
        //加锁 或 runInLoop套壳函数
        Eventloop *loop = conn->getLoop();
        conns_->insert(conn);
        //onRPCRead是对RPC的response的逻辑

        //考虑此处操作conn是否会有竞态条件,conn会在主线程被修改，在conn所在线程会被访问
        conn->setReadCallback( bind(&raftServer::onRpcRespRead, this, placeholders::_1, placeholders::_2) );
            //fdconn->setWriteFinishCallBack(writeFinishCallBack_);
        //closeCallback一般是节点断线等,直接删去shared_ptr即可
        conn->setCloseCallback( bind(&raftServer::raftConnClose, this, placeholders::_1) );
        loop->runInloop( bind(&Connection::handleEstablish, conn) );
    }

    void raftConnClose(shared_ptr<Connection> conn)
    {
        serv_->getLoop()->runInloop(bind(&raftServer::raftConnCloseInloop, this, conn));
        // 需要runInloop内层函数
        // 从set中移除计数指针指向的连接
        // 在raftConnection析构时,析构函数调用: loop->runInloop(bind(handleClose)
    }

    void raftConnCloseInloop(shared_ptr<Connection> conn)
    {
        auto iter = conns_->find(conn);
        if (iter != conns_->end())
        {
            conns_->erase(iter);
        }
    }

    /* 对RPC的response进行的index更新 */
    /* 此函数在raftNode所属线程被调用,对主线程的statMachine的操作需要加锁/runInloop/主线程runInloop函数 */
    

    void becomeFollower()
    {
        MutexGuard guard(mutex_);
        printf("BecomeFollower\n");
        /* 此操作会将所有的raftNode释放 */
        nodes_.reset(nullptr);
        role_ = follower;
        resetElectionDue();
    }

    void resetElectionDue()
    {
        int electms = rand( ) % 100 + 100;/* election due */
        double electsec = electms * 0.1;
        cout << "\n\nRand elect get electsec" << electms * 0.01 << "\n" << endl;
        serv_->getLoop()->runTimeAfter(electsec, bind(&raftServer::becomeCandidate, this), "election");
    }

    void cancelElectTime()
    {
        serv_->getLoop()->cancelTime("election");
    }

    

    private:
    unique_ptr< Server > serv_;
    unique_ptr< set< shared_ptr<Connection> > > conns_;//此处的set用于维护连接
    unique_ptr< vector< shared_ptr<raftNode> > > nodes_;
    unique_ptr< StateMachine > statMachine_;
    enum role{follower, leader, candidate} role_;
    int nodeId_;
    int voteGranted_ = 0;
    /* new  */
    vector<NodeInfo> infos_;
    Mutex mutex_;
};

/* 方便起见每次只同步一条日志 */
