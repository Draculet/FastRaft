#ifndef _____APPENDENTRIESRPC_H____
#define _____APPENDENTRIESRPC_H____

#include <string>
#include <memory>

class RaftConnection;
class RaftNode;
class AppendEntryArg;
class AppendEntriesRPC
{
    /*
        conn->getConn返回false的话,HeartBeat due更新
        返回true则avail设false,HeartBeat due更新
    */
    public:
    AppendEntriesRPC(RaftConnection *conn);
    void call(AppendEntryArg arg, std::shared_ptr<RaftNode> node);
    void callInloop(AppendEntryArg arg, std::shared_ptr<RaftNode> node);
    /* 下列操作都在持有锁状态下进行 */
    void occupyRPC(std::string flag);
    void releaseRPC();
    void setFlag(std::string flag);
    void removeFlag();
    bool match(std::string flag);

    private:
    //锁?
    bool busy_;
    RaftConnection *conn_;
    std::string flag_;
};

#endif