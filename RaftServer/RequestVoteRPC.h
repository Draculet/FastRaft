#ifndef ____REQUESTVOTERPC_H_____
#define ____REQUESTVOTERPC_H_____
#include <memory>
#include <string>

class RaftConnection;
class RequestVoteArg;
class RaftNode;
class RequestVoteRPC
{
    /*
        conn->getConn返回false的话,HeartBeat due更新
        返回true则avail设false,HeartBeat due更新
    */
    public:
    RequestVoteRPC(RaftConnection *conn);
    void callInloop(RequestVoteArg arg, std::shared_ptr<RaftNode> node);
    /* *WARNNING* 绑定RaftNode,防止其析构造成crash */
    void call(RequestVoteArg arg, std::shared_ptr<RaftNode> node);
    void occupyRPC(std::string flag);
    void releaseRPC();
    void setFlag(std::string flag);
    void removeFlag();
    bool match(std::string flag);

    private:
    //锁?
    bool busy_ = false;
    RaftConnection *conn_;
    std::string flag_;
};

#endif