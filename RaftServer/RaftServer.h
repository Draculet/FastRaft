#ifndef ___RAFTSERVER_H___
#define ___RAFTSERVER_H___

#include "../FastNet/include/Server.h"
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

#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <set>
#include <map>


class RaftServer
{
    public:
    RaftServer(int nodeId, std::vector<NodeInfo> infos, int port);
    void start();
    void workerCallback();
    void applyLog(Log &log);
    void showKV();
    /* 可能被主线程调用也可能被所属线程调用 */
    void becomeCandidate();
    void requestVoteDueCallback(std::weak_ptr<RaftNode> node);
    void becomeLeaderWithLock();
    void appendEntryDueCallback(std::weak_ptr<RaftNode> node);
    /*  在conn对应的线程调用 */
    void newRaftConnCallback(std::shared_ptr<net::Connection> conn);
    void raftConnClose(std::shared_ptr<net::Connection> conn);
    void raftConnCloseInloop(std::shared_ptr<net::Connection> conn);
    /* 对RPC的response进行的index更新 */
    /* 此函数在RaftNode所属线程被调用,对主线程的statMachine的操作需要加锁/runInloop/主线程runInloop函数 */
    void onRpcRespRead(net::Buffer *buf, std::shared_ptr<net::Connection> conn);
    void becomeFollower();
    void becomeFollowerWithLock();
    void resetElectionDue();
    void cancelElectTime();
    void onRpcReqRead(net::Buffer *buf, std::shared_ptr<net::Connection> conn);
    /*  updateCommit时调用 */
    void clientCallback(std::weak_ptr<net::Connection> conn, int logId, std::string flag, std::string oper, std::string process);

    private:
    std::unique_ptr< net::Server > serv_;
    std::unique_ptr< std::set< std::shared_ptr<net::Connection> > > conns_;//此处的set用于维护连接
    std::unique_ptr< std::vector< std::shared_ptr<RaftNode> > > nodes_;
    std::unique_ptr< StateMachine > statMachine_;
    enum role{follower, leader, candidate} role_;
    int nodeId_;
    int voteGranted_ = 0;
    std::vector<NodeInfo> infos_; /* FIXME */
    base::Mutex mutex_;
    std::unique_ptr< net::ThreadLoop > worker_;
    std::unique_ptr< std::map<std::string, std::string> > mapkv_;
    int curLogId_;
    int lastApplied;
    /* 
    用于get操作实现 readIndex Read
    int curCommitIndex_;
    int respNum_;
    bool isLeader_;
    */
};

#endif