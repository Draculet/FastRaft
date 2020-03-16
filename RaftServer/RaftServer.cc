#include "RaftServer.h"
#include "../FastNet/include/Server.h"
#include "../FastNet/include/UnixTime.h"
#include "RpcEncode.h"
#include "RpcDecoder.h"
#include "NodeInfo.h"
#include "StateMachine.h"
#include "AppendEntryArg.h" 
#include "AppendEntryRes.h"
#include "AppendEntriesRPC.h"

#include "ClientReqArg.h"
#include "ClientRes.h"

#include "RequestVoteArg.h"
#include "RequestVoteRes.h"
#include "RequestVoteRPC.h"

#include "FlagBuilder.h"
#include "RaftConnection.h"
#include "RaftNode.h"
#include <memory>
#include <functional>

using namespace std;
using namespace net;
using namespace base;

RaftServer::RaftServer(int nodeId, vector<NodeInfo> infos, int port)
    :serv_(new Server(4, port)),
    conns_(new set< shared_ptr<Connection> >()),
    nodes_(nullptr),
    statMachine_(new StateMachine( nodeId, infos.size() )),
    role_(follower),
    nodeId_(nodeId),
    voteGranted_(0),
    infos_(infos),
    mutex_(),
    worker_(new ThreadLoop()),
    mapkv_(new map<string, string>()),
    curLogId_(0),
    lastApplied(0)
{
    serv_->setReadCallback(bind(&RaftServer::onRpcReqRead, this, placeholders::_1, placeholders::_2));
}

void RaftServer::start()
{
    worker_->start();
    int workerDueMs = 20;
    //workerDueMs *= 100;
    worker_->getLoop()->runTimeEach(workerDueMs * 0.001, bind(&RaftServer::workerCallback, this), "worker");
    struct timeval tval;
    gettimeofday(&tval, nullptr);
    const int million = 1000000;
    int64_t n = tval.tv_sec * million + tval.tv_usec;
    srand(n);
    int electms = rand( ) % 100 + 100;/* election due  100ms-200ms */ 
    //electms *= 100;
    double electsec = electms * 0.001;
    /* 此处回调函数被回调的位置是主线程 */
    serv_->getLoop()->runTimeAfter(electsec, bind(&RaftServer::becomeCandidate, this), "election");
    serv_->start();
}

void RaftServer::workerCallback()
{
    int commitIndex = 0;
    int curLogId = 0;
    vector<Log> logs;
    {
        MutexGuard guard(mutex_);
        commitIndex = statMachine_->getCommitIndex();
        printf("-%s- worker get commitIndex: %d\n", UnixTime::now().toString().c_str(), commitIndex);
        printf("-%s- worker get lastApplied: %d\n", UnixTime::now().toString().c_str(), lastApplied);
        curLogId = curLogId_;
        if (commitIndex > statMachine_->getLastLogIndex() || commitIndex <= lastApplied)
        {
            return;
        }
        for (int i = lastApplied + 1; i <= commitIndex; i++)
        {
            logs.push_back(statMachine_->getLog(i));
        }
    }
    
    for (auto &log : logs)
    {
        printf("-%s- worker thread get cur LogId %d\n", UnixTime::now().toString().c_str(), curLogId_);
        printf("-%s- log LogId %d\n", UnixTime::now().toString().c_str(), log.getLogId());
        if (log.getOper() == "get" || log.getLogId() == curLogId + 1)
        {
            applyLog(log);
            if (log.getLogId() == curLogId + 1)
            {
                curLogId++;
            }
        }
    }
    printf("-%s- worker thread update cur LogId %d\n", UnixTime::now().toString().c_str(), curLogId_);
    {
        MutexGuard guard(mutex_);
        lastApplied = commitIndex;
        curLogId_ = curLogId;
    }
}

void RaftServer::applyLog(Log &log)
{
    string oper = log.getOper();
    string key = log.getKey();
    string value = log.getValue();
    string resp = "";
    if (oper == "set")
    {
        printf("-%s- set kv\n", UnixTime::now().toString().c_str());
        auto iter = mapkv_->find(key);
        if (iter != mapkv_->end())
        {
            (*mapkv_)[key] = value;
        }
        else
        {
            (*mapkv_)[key] = value;
        }
    }
    else if (oper == "get")
    {
        printf("-%s- get key\n", UnixTime::now().toString().c_str());
        auto iter = mapkv_->find(key);
        if (iter != mapkv_->end())
        {
            resp = iter->second;
        }
    }
    else if (oper == "del")
    {
        printf("-%s- del key\n", UnixTime::now().toString().c_str());
        auto iter = mapkv_->find(key);
        if (iter != mapkv_->end())
        {
            mapkv_->erase(iter);
        }
    }
    printf("-%s- Show KV map\n", UnixTime::now().toString().c_str());
    showKV();
    log.callCallback(string("apply") + resp );
}

void RaftServer::showKV()
{
    for (auto &pair : *mapkv_)
    {
        printf(" -key: %s, value: %s\n", pair.first.c_str(), pair.second.c_str());
    }
}

/* 可能被主线程调用也可能被所属线程调用 */
void RaftServer::becomeCandidate()
{
    //锁
    /*
        建立连接并发送VotReq
        RaftNode的建立会建立连接
        连接raftConn只包含Conn的weak_ptr
        Conn的shared_ptr在RaftServer
        即Conn生命的保证在RaftServer
    */
    /* 由于rpc接下来会被更新,无需重新初始化rpc,即使由于role不相符无法被release的rpc也不会造成问题,因为flag无法match */
    MutexGuard guard(mutex_);

    voteGranted_ = 1;
    statMachine_->setVoteFor(nodeId_);
    statMachine_->incTerm();
    printf("-%s- becomeCandidate  curTerm: %d\n", UnixTime::now().toString().c_str(), statMachine_->getTerm());
    if (role_ == follower)
    {
        nodes_.reset(nullptr);
        if (!nodes_)
        {
            nodes_.reset(new vector< shared_ptr<RaftNode> >());
            nodes_->resize(infos_.size());
            /*
            tip: 对于保存shared_ptr的vector,尽量避免使用push_back
            push_back如果vector空间不足时会析构原有元素,开辟新的更大的空间,将原有元素拷贝
            这可能会造成不正常的析构行为

            for (int i = 0; i < infos_.size(); i++)
            {
                nodes_->push_back(nullptr);
                printf("nodes_ size :%d\n", nodes_->size());
            }
            */
            for (auto &info : infos_)
            {
                int nodeId = info.getNodeId();
                if (nodeId != nodeId_)
                {
                    /* 
                        FIXME 3/8 此处由于RaftConnection::connect是跨线程调用需要加锁版本的newRaftConnCallback
                        需要修改为WithLock版本
                        最终采用将runInloop修改为insertQueue的方式,不提供WithLock版本
                        FIN
                    */
                    shared_ptr<RaftNode> nodeptr(new RaftNode(
                        info, info.getName(), serv_->getNextLoop()
                            , bind(&RaftServer::newRaftConnCallback, this, placeholders::_1)));
                    (*nodes_)[nodeId] = nodeptr;
                    /* raftNoode构造时就直接建立tcp连接 */
                    /* newRaftConnCallback做绑定Connection和ConnectionEstablish的调用 */
                }
            }
        }
        role_ = candidate;
    }/* 选举超时的情况 */
    /* FIXE NEW */
    if (role_ == candidate)
    {
        if (nodes_)
        {
            for (auto &node : *nodes_)
            {
                if (node)
                {
                    //各节点在各自线程上使用runInloop()
                    //Inloop函数
                    //由于共同访问statMachine_,需要加锁
                    string flag = FlagBuilder::getFlag();
                    RequestVoteArg arg(nodeId_, statMachine_->getTerm(), statMachine_->getLastLogIndex(), statMachine_->getLastLogTerm(), flag);
                    RequestVoteRPC *rpc = node->getRequestVoteRpc();
                    printf("-%s- ReqestVote Call to nodeid %d\n", UnixTime::now().toString().c_str(), node->getNodeId());
                    rpc->call(arg, node);
                    node->resetHeartBeatdue(bind(&RaftServer::requestVoteDueCallback, this, weak_ptr<RaftNode>(node))); 
                    /* tips: 此处若不先构建weak_ptr对象则存入的是shared_ptr,会造成RaftNode引用计数无法变为0 */


                    //每个节点发RPC
                    //在onRPCRead中更新选票数
                    //最终如果选票达到大多数,调用becomeLeader
                }
            }
        }
    }
    resetElectionDue();
}

void RaftServer::requestVoteDueCallback(weak_ptr<RaftNode> node)
{
    MutexGuard guard(mutex_);
    if (role_ == candidate)
    {
        shared_ptr<RaftNode> ptr = node.lock();
        if (ptr)
        {
            string flag = FlagBuilder::getFlag();
            RequestVoteArg arg(nodeId_, statMachine_->getTerm(), statMachine_->getLastLogIndex(), statMachine_->getLastLogTerm(), flag);
            RequestVoteRPC *rpc = ptr->getRequestVoteRpc();
            printf("-%s- ReqestVote Call to nodeid %d\n",UnixTime::now().toString().c_str(), ptr->getNodeId());
            // *WARNNING* 绑定node防止call执行过程中RaftNode析构造成crash
            rpc->call(arg, ptr);
            ptr->resetHeartBeatdue(bind(&RaftServer::requestVoteDueCallback, this, weak_ptr<RaftNode>(node)));
        }
    }
}

void RaftServer::becomeLeaderWithLock()
{
    printf("-%s- becomeLeader curTerm:%d\n",UnixTime::now().toString().c_str(), statMachine_->getTerm());
    statMachine_->leaderInit();
    int nextIndex = statMachine_->getNextIndex();
    Log log(statMachine_->getTerm(), nextIndex, -1, "set", "", "");
    statMachine_->insertLog(nextIndex, log);
    //TODO 调用用户请求函数插入一个空操作的日志
    role_ = leader;
    if (nodes_)
    {
        for (auto &node : *nodes_)
        {   
            if (node)
            {
                int nodeid = node->getNodeId();
                if (statMachine_->getLastLogIndex() >= statMachine_->getNextIndex(nodeid))
                {
                    //各节点在各自线程上使用runInloop()
                    //Inloop函数
                    //由于共同访问statMachine_,需要加锁
                    int term = statMachine_->getTerm();
                    int preIndex = statMachine_->getPreIndex(nodeid);
                    int preTerm = statMachine_->getPreTerm(nodeid);
                    vector<Log> log;
                    statMachine_->getEntry(preIndex + 1, log);//序列化时会加上logsize
            
                    int commitIndex = statMachine_->getCommitIndex();
                    string flag = FlagBuilder::getFlag();
                    AppendEntryArg arg(nodeId_, term, preIndex, preTerm, log, commitIndex, flag);
                    AppendEntriesRPC *rpc = node->getAppendEntryRpc();
                    printf("-%s- AppendEntry Call to nodeid %d\n", UnixTime::now().toString().c_str(), node->getNodeId());
                    // *WARNNING* 绑定node防止call执行过程中RaftNode析构造成crash
                    rpc->call(arg, node);
                    node->resetHeartBeatdue(bind(&RaftServer::appendEntryDueCallback, this, weak_ptr<RaftNode>(node)));
                }
            }
        }
    }
    cancelElectTime();
}

void RaftServer::appendEntryDueCallback(weak_ptr<RaftNode> node)
{
    MutexGuard guard(mutex_);
    if (role_ == leader)
    {
        shared_ptr<RaftNode> ptr = node.lock();
        if (ptr)
        {
            int nodeid = ptr->getNodeId();
            int term = statMachine_->getTerm();
            int preIndex = statMachine_->getPreIndex(nodeid);
            int preTerm = statMachine_->getPreTerm(nodeid);
            vector<Log> log;
            statMachine_->getEntry(preIndex + 1, log);//序列化时会加上logsize
        
            int commitIndex = statMachine_->getCommitIndex();
            string flag = FlagBuilder::getFlag();
            AppendEntryArg arg(nodeId_, term, preIndex, preTerm, log, commitIndex, flag );
            AppendEntriesRPC *rpc = ptr->getAppendEntryRpc();
            printf("-%s- AppendEntry Call to nodeid %d\n",UnixTime::now().toString().c_str(), ptr->getNodeId());
            //tip: 2020/3/8 FIXME HeartBeat计时器回调callback是在其所属线程回调, 即可同步回调callInloop
            //注意该函数已上锁,在newRaftCallback函数也进行上锁,call调用runInloop实际上是同步调用
            //因此会造成死锁
            //已修复

            // *WARNNING* 绑定node防止call执行过程中RaftNode析构造成crash
            rpc->call(arg, ptr);
            /*  call的runInloop是对同一个线程是同步调用 */
            ptr->resetHeartBeatdue(bind(&RaftServer::appendEntryDueCallback, this, weak_ptr<RaftNode>(node)));
        }
    }
}

/*  在conn对应的线程调用 */
void RaftServer::newRaftConnCallback(shared_ptr<Connection> conn)
{
    MutexGuard guard(mutex_);
    //加锁 或 runInLoop套壳函数
    Eventloop *loop = conn->getLoop();
    conns_->insert(conn);
    //onRPCRead是对RPC的response的逻辑

    //考虑此处操作conn是否会有竞态条件,conn会在主线程被修改，在conn所在线程会被访问
    conn->setReadCallback( bind(&RaftServer::onRpcRespRead, this, placeholders::_1, placeholders::_2) );
        //fdconn->setWriteFinishCallBack(writeFinishCallBack_);
    //closeCallback一般是节点断线等,直接删去shared_ptr即可
    conn->setCloseCallback( bind(&RaftServer::raftConnClose, this, placeholders::_1) );
    loop->runInloop( bind(&Connection::handleEstablish, conn) );
}

void RaftServer::raftConnClose(shared_ptr<Connection> conn)
{
    serv_->getLoop()->runInloop(bind(&RaftServer::raftConnCloseInloop, this, conn));
    // 需要runInloop内层函数
    // 从set中移除计数指针指向的连接
    // 在RaftConnection析构时,析构函数调用: loop->runInloop(bind(handleClose)
}

void RaftServer::raftConnCloseInloop(shared_ptr<Connection> conn)
{
    auto iter = conns_->find(conn);
    if (iter != conns_->end())
    {
        conns_->erase(iter);
    }
}

/* 对RPC的response进行的index更新 */
/* 此函数在RaftNode所属线程被调用,对主线程的statMachine的操作需要加锁/runInloop/主线程runInloop函数 */
void RaftServer::onRpcRespRead(Buffer *buf, shared_ptr<Connection> conn)
{
    /* 
        **FIXME**:
        对于Buffer的处理必须考虑buffer中有多个请求到达的情况
        因此考虑设计成发送一个完整req/resp之前需要先发送一个4字节的包长
        接收时先不收缩Buffer窗口地读出包长,再判断包是否完整,完整则继续解析
        否则等下一次readable

        while使用 **warnning**
    */
    while (true)
    {
        string type;
        int res = RpcDecoder::typeDecoder(buf, type);
        if (res == -1)
            return;
        int rpcsize = res;
        

        if (type == "VotRes")
        {
            
            //voteRpc Response
            /* 
                优先判断role会造成的问题有: node->rpc未release就转变role
                需要在becomexxxx种重置状态, 好处是直接跳过没有意义的判断
                如果转为follower,nodes_已经析构,match也自然无法进行
                如果转为leader则不会再用到voterpc了
            */
            RequestVoteRes res = RpcDecoder::votResDecode(buf, rpcsize);
            int nodeId = res.getNodeId();
            printf("-%s- Recv VotResp from %d\n",UnixTime::now().toString().c_str(), nodeId);
            string flag = res.getFlag();
            int term = res.getTerm();
            string success = res.getSuccess();
            MutexGuard guard(mutex_);
            if (role_ == candidate)
            {
                //TODO shared_ptr保存RaftNode
                if (nodes_)
                {
                    shared_ptr<RaftNode> node = (*nodes_)[nodeId];
                    //TODO 
                    
                    //.....
                    //判断是否为重复/过期 rpc
                    
                    if (node->getRequestVoteRpc()->match(flag))
                    {
                        if (term > statMachine_->getTerm())
                        {
                            statMachine_->setTerm(term);
                            statMachine_->setVoteFor(-1);
                            //此时已有新leader,但leaderId未知
                            becomeFollowerWithLock();
                            //nodes_全部释放,连接断开,节点析构,无需做多余处理
                            return;
                        }
                        else if (term == statMachine_->getTerm())
                        {
                            if (success == "true")
                            {
                                voteGranted_++;
                                node->getRequestVoteRpc()->releaseRPC();
                                if (voteGranted_ > infos_.size() / 2)
                                {
                                    //buf->retrieve(rpcsize);
                                    /* tips: electdue是一个位于主线程loop的计时器,负责本节点的选举计时,Heartbeat则是位于每个RaftNode所属的loop的计时器,负责RPC计时  */
                                    //becomeLeader马上会重置Heartbeat计时器,无需cancelHeartBeat
                                    becomeLeaderWithLock();
                                    //接下来连接复用,仍需要清除buffer
                                    return;
                                }
                            }
                            else/* success false */
                            {
                                node->getRequestVoteRpc()->releaseRPC();
                                node->cancelHeartBeat();
                                //不再针对该node发送voteReq
                            }
                        }
                    }/* end match */
                }/* end nodes_ */
            }/* end role */
            //buf->retrieve(rpcsize);
        }/* requestVote end*/
        else if (type == "ApdRes")
        {
            AppendEntryRes res = RpcDecoder::apdResDecode(buf, rpcsize);
            int nodeId = res.getNodeId();
            printf("-%s- Recv ApdRes from %d\n", UnixTime::now().toString().c_str(), nodeId);
            string success = res.getSuccess();
            int term = res.getTerm();
            int matchIndex = res.getMatchIndex();
            string flag = res.getFlag();
            //printf("___________Test NodeId %d_____________\n", nodeId);
            //printf("___________Test MatchIndex %d_____________\n", matchIndex);
            //printf("___________Test succ %s_____________\n", success.c_str());
            MutexGuard guard(mutex_);
            if (role_ == leader)
            {
                //releaseRPC need lock?
                //AppendEntriesRPC Response
                //TODO shared_ptr保存RaftNode
                
                
                if (nodes_)
                {
                    shared_ptr<RaftNode> node = (*nodes_)[nodeId];
                    //TODO 
                    //string flag = buf->retrieve(6);
                    if (node->getAppendEntryRpc()->match(flag))
                    {
                        if (success == "true")
                        {
                            //tips: success==true时,term只可能是相等的或者本方term已经更新的更高,但不影响修改log index
                            statMachine_->updateMatchIndex(nodeId, matchIndex);
                            statMachine_->updateNextIndex(nodeId, matchIndex + 1);
                            statMachine_->updateCommitIndex();
                            statMachine_->showNodeInfo();
                        }
                        else/* success false*/
                        {
                            if (term > statMachine_->getTerm())
                            {
                                //Leader落伍 nodeId未知
                                statMachine_->setVoteFor(-1);
                                becomeFollowerWithLock();
                                //RaftNode被置空,Buffer/Conn都会被自动清除
                                return;
                            }
                            else
                            {
                                //!!FIXME 会因为错误判断导致index减到-2
                                statMachine_->retreatIndex(nodeId);
                            }
                        }
                        node->getAppendEntryRpc()->releaseRPC();
                        /* TODO 决定是否立即发送下一轮AppendEntryRPC */
                        //锁?
                        if (statMachine_->getLastLogIndex() >= statMachine_->getNextIndex(nodeId))
                        {
                            statMachine_->showNodeInfo();
                            int preIndex = statMachine_->getPreIndex(nodeId);
                            int preTerm = statMachine_->getPreTerm(nodeId);
                            vector<Log> log;
                            statMachine_->getEntry(preIndex + 1, log);//序列化时会加上logsize
        
                            int commitIndex = statMachine_->getCommitIndex();
                            AppendEntryArg arg(nodeId_, term, preIndex, preTerm, log, commitIndex, flag);
                            AppendEntriesRPC *rpc = node->getAppendEntryRpc();
                            printf("-%s- AppendEntry to nodeid %d\n",UnixTime::now().toString().c_str(), node->getNodeId());

                            // *WARNNING* 绑定node防止call执行过程中RaftNode析构造成crash
                            rpc->call(arg, node);
                            node->resetHeartBeatdue(bind(&RaftServer::appendEntryDueCallback, this, weak_ptr<RaftNode>(node)));
                        }
                    }
                }
            }
            //不管是过期重复的RPC还是正确响应的RPC 都需要将该次RPC清除
            //buf->retrieve(rpcsize);
        }/* AppendEntryRpc end */
        else if (type == "xxxxx")
        {
            //TODO ....
        }
        else
        {
            //TODO 断开连接
            printf("*debug* error in RpcRespRead\n");
        }
        
    }/* end while */
}

void RaftServer::becomeFollower()
{
    /* 此操作会将所有的RaftNode释放 */
    MutexGuard guard(mutex_);
    nodes_.reset(nullptr);
    role_ = follower;
    resetElectionDue();
}

void RaftServer::becomeFollowerWithLock()
{
    printf("-%s- becomeFollower curTerm:%d\n\n\n",UnixTime::now().toString().c_str(), statMachine_->getTerm());
    nodes_.reset(nullptr);
    role_ = follower;
    resetElectionDue();
}

void RaftServer::resetElectionDue()
{
    printf("-%s- ResetElection\n", UnixTime::now().toString().c_str());
    int electms = rand( ) % 100 + 100;/* election due  100ms-200ms */ 
    //electms *= 100;
    double electsec = electms * 0.001;
    serv_->getLoop()->runTimeAfter(electsec, bind(&RaftServer::becomeCandidate, this), "election");
}

void RaftServer::cancelElectTime()
{
    serv_->getLoop()->cancelTime("election");
}

void RaftServer::onRpcReqRead(Buffer *buf, shared_ptr<Connection> conn)
{

    while (true)
    {
        string type;
        int res = RpcDecoder::typeDecoder(buf, type);
        if (res == -1)
            return;
        int rpcsize = res;
        
        if (type == "VotReq")
        {
            /*
                reqRPC中 Term小于选票的Term **则节点需要转变为follower ?**,Term转为新Term, 其他判断照旧
            */
            RequestVoteArg req = RpcDecoder::votReqDecode(buf, rpcsize);
            int term = req.getTerm();
            int nodeId = req.getNodeId(); /* 该nodeId是Candidate的Id */
            printf("-%s- Recv VotReq from %d\n",UnixTime::now().toString().c_str(), nodeId);
            string flag = req.getFlag();
            int lastLogIndex = req.getLastLogIndex();
            int lastLogTerm = req.getLastLogTerm();
            //不管是什么角色,leader term小 则需要让leader更新term
            
            /* statMachine_为共用资源 */
            MutexGuard guard(mutex_);
            if (term < statMachine_->getTerm())
            {
                RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                printf("-%s- Resp ReqVote False\n",UnixTime::now().toString().c_str());
                conn->send(resp.toString());
            }
            else if ( term == statMachine_->getTerm() )
            {
                /* 任期相等的候选者反对票 */
                if (role_ == candidate)
                {
                    RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                    printf("-%s- Resp ReqVote False\n",UnixTime::now().toString().c_str());
                    conn->send(resp.toString());
                }
                else if (role_ == follower) /* 该情况下follower已投票 */
                {
                    /* 如果多个节点同时发起选举,肯定有一方先到修改curTerm,使得后来的被拒 */
                    /* 为了防止为多个人投票
                    if (statMachine_->voteFor() != -1)
                    {
                        RequestVoteRes resp("false", statMachine_->getTerm(), flag);
                        conn->send(resp.toString());
                    }  */
                    /*
                    statMachine_->setTerm(term);
                    statMachine_->setVoteFor(id);
                    RequestVoteRes resp("true", statMachine_->getTerm(), flag);
                    conn->send(resp.toString());
                    resetElectionDue();
                    */
                    RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                    printf("-%s- Resp ReqVote False\n", UnixTime::now().toString().c_str());
                    conn->send(resp.toString());
                }
                else if (role_ == leader)
                {
                    RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                    printf("-%s- Resp ReqVote False\n", UnixTime::now().toString().c_str());
                    conn->send(resp.toString());
                }
            }
            else /* term > statMachine_->getTerm() */
            {
                //竞选者发现自己落伍,投赞成票并更新自己的term
                /* 
                if (role_ == candidate)
                {
                    statMachine_->setTerm(term);
                    statMachine_->setVoteFor(nodeId);
                    RequestVoteRes resp(nodeId_, "true", statMachine_->getTerm(), flag);
                    printf("_____time:%ld______Resp ReqVote True_____________\n",FlagBuilder::getTime());
                    conn->send(resp.toString());
                    becomeFollowerWithLock();
                    //仍需要读走rpcsize
                }

                else*/ 
                if (role_ == follower || role_ == candidate || role_ == leader)
                {
                    if (lastLogIndex >= statMachine_->getLastLogIndex())
                    {
                        //请求的lastLogIndex长,赞成票
                        if (lastLogIndex > statMachine_->getLastLogIndex())
                        {
                            statMachine_->setTerm(term);
                            statMachine_->setVoteFor(nodeId);
                            /* 注意,此处需发回自身的nodeId */
                            RequestVoteRes resp(nodeId_, "true", statMachine_->getTerm(), flag);
                            printf("-%s- Resp ReqVote True\n",UnixTime::now().toString().c_str());
                            conn->send(resp.toString());
                            resetElectionDue();
                        }
                        else if (lastLogIndex == statMachine_->getLastLogIndex() && lastLogTerm >= statMachine_->getLastLogTerm())
                        {
                            //请求的lastLogIndex同长term高,赞成票
                            statMachine_->setTerm(term);
                            statMachine_->setVoteFor(nodeId);
                            RequestVoteRes resp(nodeId_, "true", statMachine_->getTerm(), flag);
                            printf("-%s- Resp ReqVote True\n",UnixTime::now().toString().c_str());
                            conn->send(resp.toString());
                            resetElectionDue();
                        }
                        else
                        {
                            RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                            printf("-%s- Resp ReqVote False\n",UnixTime::now().toString().c_str());
                            conn->send(resp.toString());
                        }
                    }
                    else
                    {
                        RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                        printf("-%s- Resp ReqVote False\n",UnixTime::now().toString().c_str());
                        conn->send(resp.toString());
                    }
                }
                /* 
                else if (role_ == leader)
                {}
                */
            }
            //buf->retrieve(rpcsize);
        }/* end VoteReq */
        else if (type == "ApdReq")
        {
            /* 需要考虑RPC重发的幂等性 */
            /*
                考虑一种情景:由于发送AppendEntryRPC时是可以进行连接重连并重发的,假设在某时刻发送RPC
                但是由于网络波动丢失连接/连接中断,此RPC还是到了peer,在此之间发送方建立新连接发送新RPC?
                这种情况不会发生,连接中断则conn->send不会成功,无法发回复,而发送方被设计成不收到回复就
                一直重发的机制.只会重建连接重发并期望得到回复.而同一条连接数据的发送如果是明显有序(单线程)则是不会乱序的

                如果是同一条RPC重复发送,由于是按index插入的,因此本身就是幂等的
            */
        
            AppendEntryArg req = RpcDecoder::apdReqDecode(buf, rpcsize);
            int nodeId = req.getNodeId();
            printf("-%s- Recv ApdReq from %d\n",UnixTime::now().toString().c_str(), nodeId);
            int term = req.getTerm();
            vector<Log> entry = req.getEntry();
            int logsize = entry.size();
            Log log;
            if (logsize == 1)
            {
                log = entry[0];
            }
            int commitIndex = req.getCommitIndex();
            string flag = req.getFlag();
            int preIndex = req.getPreLogIndex();
            int preTerm = req.getPreLogTerm();
            
            MutexGuard guard(mutex_);

            //FIXME
            if (statMachine_->getCommitIndex() < commitIndex)
            {
                statMachine_->setCommitIndex(commitIndex);
            }
            if ( term >= statMachine_->getTerm() )
            {
                statMachine_->setTerm(term);
                /* 更新term, 使得下方的if判断成立,进入日志添加过程 */
                if (role_ != follower)
                {
                    /* 
                        同term下,作为candidate或者是leader收到apdrpc,意味着有一个leader诞生
                        转变为follower
                    */
                    statMachine_->setVoteFor(nodeId);
                    becomeFollowerWithLock();
                }
                //连接会持续保持,需要保证rpc被读走
            }
            else if ( term < statMachine_->getTerm() )
            {
                AppendEntryRes res(nodeId_, "false", 0, statMachine_->getTerm(), flag);
                printf("-%s- Resp ApdEntry False\n",UnixTime::now().toString().c_str());
                conn->send(res.toString());
            }

            /* 不管是follower还是其他role都需根据log添加/拒绝日志 */
            if (term == statMachine_->getTerm())
            {
                if (role_ != follower)
                {
                    //产生两个leader时,如果接受到对方的ApdReq则都重新选举
                    statMachine_->setVoteFor(nodeId);
                    becomeFollowerWithLock();
                }
                if (statMachine_->preIndexMatch(preIndex, preTerm))
                {
                    //TODO 可修改为多条日志一起复制
                    if (logsize == 1)
                    {
                        statMachine_->insertLog(preIndex + 1, log);
                        printf("-%s- RECV LOGSIZE 1\n", UnixTime::now().toString().c_str());
                        AppendEntryRes res(nodeId_, "true", preIndex + 1, term, flag);
                        printf("-%s- Resp AppendEntry True\n", UnixTime::now().toString().c_str());
                        resetElectionDue();
                        conn->send(res.toString());
                    }
                    else if (logsize == 0)/* no op appendentry */
                    {
                        printf("-%s- RECV LOGSIZE 0\n",UnixTime::now().toString().c_str() );
                        AppendEntryRes res(nodeId_, "true", preIndex, term, flag);
                        printf("-%s- Resp AppendEntry True\n", UnixTime::now().toString().c_str());
                        resetElectionDue();
                        conn->send(res.toString());
                        /* 此日志须与转变为leader后发的第一个日志区分,leader发的空日志需要insert到日志中去,而该日志不需要 */
                    }
                }
                else
                {
                    AppendEntryRes res(nodeId_, "false", 0, term, flag);
                    printf("-%s- Resp AppendEntry False\n",UnixTime::now().toString().c_str());
                    resetElectionDue();
                    conn->send(res.toString());
                }
            }/* end if */
            //buf->retrieve(rpcsize); /* decoder已经进行此步操作 */
        }/* end if ApdReq */
        else if (type == "GLogId")
        {
            string type = buf->retrieveAsString(6);
            MutexGuard guard(mutex_);
            int logId = -1;
            if (role_ == leader)
            {
                logId = curLogId_;
            }
            logId = htonl(logId);
            char buf[8] = {0};
            memcpy(buf, &logId, 4);
            conn->send(string(buf, 4));
        }/* end if GLogId */
        else if (type == "CliReq")
        {
            printf("-%s- Recv CliReq\n", UnixTime::now().toString().c_str());
            ClientReqArg req = ClientReqArg::fromString(buf);
            int nextIndex = statMachine_->getNextIndex(nodeId_);
            string oper = req.getOper();
            string key = req.getKey();
            string value = req.getValue();
            int logId = req.getLogId();
            string flag = req.getFlag();
            int term = statMachine_->getTerm();
            MutexGuard guard(mutex_);

            if (role_ == leader)
            {
                /* FIXME 使用高效的read方式, 考虑实现readIndex Read */
                if (req.getOper() == "set" || req.getOper() == "del" || req.getOper() == "get")
                {
                    printf("-%s- Client log LogId %d\n", UnixTime::now().toString().c_str(), logId);
                    printf("-%s- Leader InsertLog\n", UnixTime::now().toString().c_str());
                    statMachine_->insertLog(nextIndex, Log(
                            term, nextIndex, 
                                logId, oper,
                                    key, value,
                                        bind(&RaftServer::clientCallback, this, weak_ptr<Connection>(conn), logId, flag, oper, placeholders::_1) ) );
                    if (nodes_)
                    {
                        for (auto &node : *nodes_)
                        {   
                            if (node)
                            {
                                int nodeid = node->getNodeId();
                                if (statMachine_->getLastLogIndex() >= statMachine_->getNextIndex(nodeid))
                                {
                                    //各节点在各自线程上使用runInloop()
                                    //Inloop函数
                                    //由于共同访问statMachine_,需要加锁
                                    int term = statMachine_->getTerm();
                                    int preIndex = statMachine_->getPreIndex(nodeid);
                                    int preTerm = statMachine_->getPreTerm(nodeid);
                                    vector<Log> log;
                                    statMachine_->getEntry(preIndex + 1, log);//序列化时会加上logsize
                                    int commitIndex = statMachine_->getCommitIndex();
                                    string flag = FlagBuilder::getFlag();
                                    AppendEntryArg arg( nodeId_, term, preIndex, preTerm, log, commitIndex, flag);
                                    AppendEntriesRPC *rpc = node->getAppendEntryRpc();
                                    printf("-%s- AppendEntry Call to nodeid %d\n", UnixTime::now().toString().c_str(), node->getNodeId());
                                    rpc->call(arg, node);
                                    node->resetHeartBeatdue(bind(&RaftServer::appendEntryDueCallback, this, weak_ptr<RaftNode>(node)));
                                }
                            }
                        }
                    }
                }
            }
            else /* role != leader */ 
            {
                ClientRes res(logId, statMachine_->getVoteFor(), flag, "fail");
                printf("-%s- ClientReq Failed  curLogId:%d  voteFor:%d\n", UnixTime::now().toString().c_str(), logId, statMachine_->getVoteFor());
                conn->send(res.toString());
            }
            
        }/* end CliReq */
    }/* end while */
}/* end onReadReq */

/*  updateCommit时调用 */
void RaftServer::clientCallback(weak_ptr<Connection> conn, int logId, string flag, string oper, string process)
{
    if (process == "commit")
    {
        if (oper == "set" || oper == "del")
        {
            printf("-%s- set log commit\n", UnixTime::now().toString().c_str());
            shared_ptr<Connection> connptr = conn.lock();
            if (connptr)
            {
                ClientRes res(logId, -1, flag, "succ");
                connptr->send(res.toString());
            }
        }
        else if (oper == "get")
        {
            /* apply时才进行回调 */
        }
    }
    else if (string(process, 0, 5) == "apply")
    {
        printf("-%s- get log apply\n", UnixTime::now().toString().c_str());
        string resp = string(process, 5);
        if (oper == "get")
        {
            shared_ptr<Connection> connptr = conn.lock();
            if (connptr)
            {
                ClientRes res(logId, -1, flag, resp);
                connptr->send(res.toString());
            }
        }
    }
}
