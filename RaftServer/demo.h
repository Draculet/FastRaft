
#include "../Server.h"
#include "../Connection.h"
#include <algorithm>
#include <set>
using namespace std;
using namespace net;
using namespace base;
/*  tip:  electdue是一个位于主线程loop的计时器,负责本节点的选举计时,Heartbeat则是位于每个RaftNode所属的loop的计时器,负责RPC计时  */

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

    static int64_t getTime()
    {
        struct timeval tval;
        gettimeofday(&tval, nullptr);
        const int million = 1000000;
        int64_t n = tval.tv_sec * million + tval.tv_usec;
        return n;
    }
};



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

class RaftConnection
{
    /* connect()在loop_所属线程调用 */
    public:
    RaftConnection(NodeInfo info, Eventloop *loop, function<void(shared_ptr<Connection>)> newRaftConnCallback)
        :loop_(loop),
        binded(false),
        info_(info),
        newRaftConnCallback_(newRaftConnCallback)
    {
        connect();
    }

    /* 当节点不再是leader时, 需要将所有RaftNode析构,连接也需要释放 */
    ~RaftConnection()
    {
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
    Eventloop *getLoop()
    {
        return loop_;
    }

    //getConn只会在rpc->call中被调用
    //而call方法只在RaftNode所在线程调用
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

    /* 需要异步 */
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
        int ret = ::connect(clientFd, (struct sockaddr *)(&servaddr), sizeof(servaddr));
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
            binded = false;
            return false;//重连失败
        }
        else
        {
            //new shared_ptr
            shared_ptr<Connection> conn(new Connection(sockfd, addr, loop_));
            newRaftConnCallback_(conn);/*RaftServer传来的保存连接callback*/
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
        :term_(-1),
        index_(-1),
        logId_(-1),
        oper_(""),
        key_(""),
        value_("")
    {}

    string toString()
    {
        int total = 0;
        int ksize = key_.size();
        int vsize = value_.size();
        int size = ksize + vsize + 3 + 8;
        char buf[size + 32] = {0};
        int term = htons(term_);
        memcpy(buf, &term, 4);
        int index = htons(index_);
        memcpy(buf + 4, &index, 4);
        int logId = htons(logId_);
        memcpy(buf + 8, &logId, 4);



        memcpy(buf + 12, oper_.c_str(), 3);
        ksize = htons(ksize);
        memcpy(buf + 15, &ksize, 4);
        memcpy(buf + 19, key_.c_str(), key_.size());
        /* get和del只有key参数 */
        total = 19 + key_.size();
        if (oper_ == "set")
        {
            vsize = htons(vsize);
            memcpy(buf + 19 + key_.size(), &vsize, 4);
            memcpy(buf + 23 + key_.size(), value_.c_str(), value_.size());
            total = 23 + key_.size() + value_.size();
        }
        return string(buf, total);
    }

    static Log fromString(Buffer *buf)
    {
        int term;
        string _term = buf->retrieveAsString(4);
        memcpy(&term, _term.c_str(), 4);
        term = ntohs(term);
        int index;
        string _index = buf->retrieveAsString(4);
        memcpy(&index, _index.c_str(), 4);
        index = ntohs(index);
        int logId;
        string _logId = buf->retrieveAsString(4);
        memcpy(&logId, _logId.c_str(), 4);
        logId = ntohs(logId);


        string oper = buf->retrieveAsString(3);
        int ksize;
        string _ksize = buf->retrieveAsString(4);
        memcpy(&ksize, _ksize.c_str(), 4);
        ksize = ntohs(ksize);
        string key = buf->retrieveAsString(ksize);
        string value = "";
        if (oper == "set")
        {
            int vsize;
            string _vsize = buf->retrieveAsString(4);
            memcpy(&vsize, _vsize.c_str(), 4);
            vsize = ntohs(vsize);
            value = buf->retrieveAsString(vsize);
        }
        return Log(term, index, logId, oper, key, value);
    }

    int getTerm()
    {
        return term_;
    }

    int getIndex()
    {
        return index_;
    }
    int getLogId()
    {
        return logId_;
    }

    string getOper()
    {
        return oper_;
    }

    string getKey()
    {
        return key_;
    }

    string getValue()
    {
        return value_;
    }


    int term_;
    int index_;
    int logId_;//作用是client的幂等性
    string oper_;
    string key_;
    string value_;
};

class StateMachine
{
    public:
    StateMachine(int nodeId, int nodenum)
        :logs(0),
        firstLogIndex(0),
        lastLogIndex(0),
        nextIndex(nodenum, lastLogIndex + 1), /*将要同步的日志*/
        matchIndex(nodenum, 0),
        nodeId_(nodeId)
    {
        logs.push_back(Log(0, 0, -1, "", "", ""));
        logs.push_back(Log(-1, -1, -1, "", "", ""));
    }

    void getEntry(int nextIndex, vector<Log> &entry)
    {
        if (lastLogIndex >= nextIndex)
        {
            //每次只取一个日志
            entry.push_back(logs[nextIndex]);
        }
    }

    int getTerm()
    {
        return currentTerm;
    }

    void incTerm()
    {
        currentTerm++;
    }

    void setTerm(int term)
    {
        currentTerm = term;
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

    void updateNextIndex(int nodeid, int next)
    {
        nextIndex[nodeid] = next;
    }

    void updateMatchIndex(int nodeid, int match)
    {
        matchIndex[nodeid] = match;
    }

    void updateCommitIndex()
    {
        //FIXME
        int min = *min_element(matchIndex.begin(), matchIndex.end());
        /*
            根据论文5.4.2需要当前Term的日志被大多数提交才能commit
        */
        if (commitIndex < min && logs[min].getTerm() == currentTerm)
        {
            commitIndex = min;
        }
    }

    void updateTerm(int term)
    {
        currentTerm = term;
    }

    void setVoteFor(int vote)
    {
        voteFor = vote;
    }

    /* 两种场景 */
    //1* 尾插日志, lastLogIndex++, 同时需要为log开辟空间
    //2* 日志在index被覆盖, 后面的日志全部截断,无需开辟空间
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
                logs.push_back(Log());
            }
            //覆盖日志无需开辟空间
        }
        else
        {
            //报错
        }
        
    }

    int getLastLogIndex()
    {
        return lastLogIndex;
    }

    int getLastLogTerm()
    {
        return logs[lastLogIndex].getTerm();
    }

    bool preIndexMatch(int preIndex, int preTerm)
    {
        if (preIndex <= lastLogIndex)
        {
            int term = logs[preIndex].getTerm();
            return term == preTerm;
        }
        else
        {
            return false;
        }
    }

    int getNextIndex()
    {
        return nextIndex[nodeId_];
    }
    
    int getNextIndex(int nodeid)
    {
        return nextIndex[nodeid];
    }

    int getPreIndex(int nodeid)
    {
        return nextIndex[nodeid] - 1;
    }

    int getPreTerm(int nodeid)
    {
        return logs[getPreIndex(nodeid)].getTerm();
    }

    void retreatIndex(int nodeid)
    {
        nextIndex[nodeid]--;
    }

    int getCommitIndex()
    {
        return commitIndex;
    }

    void showVector()
    {
        printf("____time:%ld__________SHow____________\n",FlagBuilder::getTime());
        printf("lastLogindex %d\n", lastLogIndex);
        printf("logsize %d\n", logs.size());
        for (auto &index : nextIndex)
        {
            printf("%d ", index);
        }
        printf("\n");
        for (auto &index : matchIndex)
        {
            
            printf("%d ", index);
        }
        printf("\n");
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

class AppendEntryArg;
class AppendEntryRes;
class RequestVoteArg;
class RequestVoteRes;
class RpcEncoder
{
    public:
    static string apdReqEncode(AppendEntryArg &arg);
    static string apdResEncode(AppendEntryRes &arg);
    static string votResEncode(RequestVoteRes &arg);
    static string votReqEncode(RequestVoteArg &arg);
};

class RequestVoteRes
{
    public:
    RequestVoteRes(int nodeId, string success, int term, string flag)
        :nodeId_(nodeId),
        success_(success),
        term_(term),
        flag_(flag)
    {}

    int getNodeId()
    {
        return nodeId_;
    }

    int getTerm()
    {
        return term_;
    }

    string getSuccess()
    {
        return success_;
    }

    string getFlag()
    {
        return flag_;
    }

    string toString()
    {
        return RpcEncoder::votResEncode(*this);
    }

    private:
    int nodeId_;
    string success_;
    int term_;
    string flag_;
};

class RequestVoteArg
{
    public:
    /* nodeId即候选者nodeId */
    RequestVoteArg(int nodeId, int term, int lastLogIndex, int lastLogTerm, string flag)
        :nodeId_(nodeId),
        term_(term),
        lastLogIndex_(lastLogIndex),
        lastLogTerm_(lastLogTerm),
        flag_(flag)
    {}
    
    string toString()
    {
        return RpcEncoder::votReqEncode(*this);
    }

    int getNodeId()
    {
        return nodeId_;
    }

    int getTerm()
    {
        return term_;
    }

    int getLastLogIndex()
    {
        return lastLogIndex_;
    }

    int getLastLogTerm()
    {
        return lastLogTerm_;
    }

    string getFlag()
    {
        return flag_;
    }


    private:
    int nodeId_;
    int term_;
    int lastLogIndex_;
    int lastLogTerm_;
    string flag_;
};

struct AppendEntryArg
{
    public:
    AppendEntryArg()
    {}

    AppendEntryArg(int nodeId, int term,  int preLogIndex, int preLogTerm, vector<Log> entries, int commitIndex, string flag)
        :nodeId_(nodeId),
        term_(term),
        preLogIndex_(preLogIndex),
        preLogTerm_(preLogTerm),
        entry_(entries),
        flag_(flag)
    {}

    int getNodeId()
    {
        return nodeId_;
    }

    int getTerm()
    {
        return term_;
    }
    
    int getPreLogIndex()
    {
        return preLogIndex_;
    }

    int getPreLogTerm()
    {
        return preLogTerm_;
    }

    vector<Log> getEntry()
    {
        return entry_;
    }

    int getCommitIndex()
    {
        return commitIndex_;
    }

    string getFlag()
    {
        return flag_;
    }

    string toString()
    {
        return RpcEncoder::apdReqEncode(*this);
    }

    
    private:
    int nodeId_;
    int term_;
    int preLogIndex_;
    int preLogTerm_;
    vector<Log> entry_;
    int commitIndex_;
    string flag_;
};

struct AppendEntryRes
{
    public:
    AppendEntryRes(int nodeId, string success, int matchIndex, int term, string flag)
        :nodeId_(nodeId),
        success_(success),
        matchIndex_(matchIndex),
        term_(term),
        flag_(flag)
    {}

    int getNodeId()
    {
        return nodeId_;    
    }

    int getTerm()
    {
        return term_;
    }

    string getSuccess()
    {
        return success_;
    }
    
    int getMatchIndex()
    {
        return matchIndex_;
    }

    string getFlag()
    {
        return flag_;
    }

    string toString()
    {
        return RpcEncoder::apdResEncode(*this);
    }

    private:
    int nodeId_;
    string success_;
    int matchIndex_;
    int term_;
    string flag_;
};

class ClientReqArg
{
    public:
    ClientReqArg(int logId, string oper, string key, string value)
        :logId_(logId),
        oper_(oper),
        key_(key),
        value_(value)
    {}

    int getLogId()
    {
        return logId_;
    }
    
    string getOper()
    {
        return oper_;
    }

    string getKey()
    {
        return key_;
    }

    string getValue()
    {
        return value_;
    }

    static ClientReqArg fromString(Buffer *buf)
    {
        int logId;
        string _logId = buf->retrieveAsString(4);
        memcpy(&logId, _logId.c_str(), 4);
        logId = ntohs(logId);


        string oper = buf->retrieveAsString(3);
        int ksize;
        string _ksize = buf->retrieveAsString(4);
        memcpy(&ksize, _ksize.c_str(), 4);
        ksize = ntohs(ksize);
        string key = buf->retrieveAsString(ksize);
        string value = "";
        if (oper == "set")
        {
            int vsize;
            string _vsize = buf->retrieveAsString(4);
            memcpy(&vsize, _vsize.c_str(), 4);
            vsize = ntohs(vsize);
            value = buf->retrieveAsString(vsize);
        }

        return ClientReqArg(logId, oper, key, value);
    }

    private:
    int logId_;
    string oper_;
    string key_;
    string value_;
};


class RpcDecoder
{
    public:
    //设计为static 
    //需要处理的部分以while执行
    /* 
        typeDecoder会修改type为解析出的类型标识, 返回rpc的大小
        返回-1表示Buffer不完整
        type为定长6字节的字符串
    */
    static int typeDecoder(Buffer *buf, string &type)
    {
        printf("___________type Decoder_____________\n");
        if (buf->readable() > 4)
        {
            int total = 0;
            string _total = buf->preViewAsString(4);
            memcpy(&total, _total.c_str(), 4);
            total = ntohs(total);
            if (buf->readable() >= 4 + total)
            {
                buf->retrieve(4);
                type = buf->preViewAsString(6);
                printf("___________test %s_____________\n", type.c_str());
                return total;
            }
        }
        return -1;
    }

    static RequestVoteArg votReqDecode(Buffer *buf, int total)
    {
        string type = buf->retrieveAsString(6);
        if (type == "VotReq")
        {
            int nodeId;
            string _nodeId = buf->retrieveAsString(4);
            memcpy(&nodeId, _nodeId.c_str(), 4);
            nodeId = ntohs(nodeId);
            int term;
            string _term = buf->retrieveAsString(4);
            memcpy(&term, _term.c_str(), 4);
            term = ntohs(term);
            int lastLogIndex;
            string _lastLogIndex = buf->retrieveAsString(4);
            memcpy(&lastLogIndex, _lastLogIndex.c_str(), 4);
            lastLogIndex = ntohs(lastLogIndex);
            int lastLogTerm;
            string _lastLogTerm = buf->retrieveAsString(4);
            memcpy(&lastLogTerm, _lastLogTerm.c_str(), 4);
            lastLogTerm = ntohs(lastLogTerm);
            int flagsize;
            string _flagsize = buf->retrieveAsString(4);
            memcpy(&flagsize, _flagsize.c_str(), 4);
            flagsize = ntohs(flagsize);
            string flag = buf->retrieveAsString(flagsize);


            //buf->retrieve(total);
            return RequestVoteArg(nodeId, term, lastLogIndex, lastLogTerm, flag);
        }
        else
        {
            //TODO 报错,断连接
            return RequestVoteArg(-1, -1, -1, -1, "");
        }
    }

    static RequestVoteRes votResDecode(Buffer *buf, int total)
    {
        string type = buf->retrieveAsString(6);
        if (type == "VotRes")
        {
            int nodeId;
            string _nodeId = buf->retrieveAsString(4);
            memcpy(&nodeId, _nodeId.c_str(), 4);
            nodeId = ntohs(nodeId);
            string succ = buf->retrieveAsString(4);
            if (succ == "fals")
                succ = "false";
            int term;
            string _term = buf->retrieveAsString(4);
            memcpy(&term, _term.c_str(), 4);
            term = ntohs(term);
            int flagsize;
            string _flagsize = buf->retrieveAsString(4);
            memcpy(&flagsize, _flagsize.c_str(), 4);
            flagsize = ntohs(flagsize);
            string flag = buf->retrieveAsString(flagsize);

            //buf->retrieve(total);
            return RequestVoteRes(nodeId, succ, term, flag);
        }
        else
        {
            //TODO 报错,断连接
            return RequestVoteRes(-1, "", -1, "");
        }
    }

    static AppendEntryArg apdReqDecode(Buffer *buf, int total)
    {
        string type = buf->retrieveAsString(6);
        if (type == "ApdReq")
        {
            vector<Log> entry;
            int nodeId;
            string _nodeId = buf->retrieveAsString(4);
            memcpy(&nodeId, _nodeId.c_str(), 4);
            nodeId = ntohs(nodeId);
            int term;
            string _term = buf->retrieveAsString(4);
            memcpy(&term, _term.c_str(), 4);
            term = ntohs(term);
            int preLogIndex;
            string _preLogIndex = buf->retrieveAsString(4);
            memcpy(&preLogIndex, _preLogIndex.c_str(), 4);
            preLogIndex = ntohs(preLogIndex);
            int preLogTerm;
            string _preLogTerm = buf->retrieveAsString(4);
            memcpy(&preLogTerm, _preLogTerm.c_str(), 4);
            preLogTerm = ntohs(preLogTerm);
            int lognum;
            string _lognum = buf->retrieveAsString(4);
            memcpy(&lognum, _lognum.c_str(), 4);
            lognum = ntohs(lognum);
            int commitIndex;
            string _commitIndex = buf->retrieveAsString(4);
            memcpy(&commitIndex, _commitIndex.c_str(), 4);
            commitIndex = ntohs(commitIndex);
            int flagsize;
            string _flagsize = buf->retrieveAsString(4);
            memcpy(&flagsize, _flagsize.c_str(), 4);
            flagsize = ntohs(flagsize);
            string flag = buf->retrieveAsString(flagsize);
            if (lognum == 1)
            {
                Log log = Log::fromString(buf);
                entry.push_back(log);
            }


            //buf->retrieve(total);
            return AppendEntryArg(nodeId, term, preLogIndex, preLogTerm, entry, commitIndex, flag);
        }
        else
        {
            //TODO 报错,断连接
            return AppendEntryArg(-1, -1, -1, -1, vector<Log>(), -1, "");
        }
    }

    static AppendEntryRes apdResDecode(Buffer *buf, int total)
    {
        string type = buf->retrieveAsString(6);
        if (type == "ApdRes")
        {
            int nodeId;
            string _nodeId = buf->retrieveAsString(4);
            memcpy(&nodeId, _nodeId.c_str(), 4);
            nodeId = ntohs(nodeId);
            string succ = buf->retrieveAsString(4);
            if (succ == "fals")
                succ = "false";
            int matchIndex;
            string _matchIndex = buf->retrieveAsString(4);
            memcpy(&matchIndex, _matchIndex.c_str(), 4);
            matchIndex = ntohs(matchIndex);
            int term;
            string _term = buf->retrieveAsString(4);
            memcpy(&term, _term.c_str(), 4);
            term = ntohs(term);
            int flagsize;
            string _flagsize = buf->retrieveAsString(4);
            memcpy(&flagsize, _flagsize.c_str(), 4);
            flagsize = ntohs(flagsize);
            string flag = buf->retrieveAsString(flagsize);

            //buf->retrieve(total);
            return AppendEntryRes(nodeId, succ, matchIndex, term, flag);
        }
        else
        {
            //TODO 报错,断连接
            return AppendEntryRes(-1, "", -1, -1, "");
        }
    }

    static ClientReqArg cliReqDecode(Buffer * buf, int total);
    
    
};






class AppendEntriesRPC
{
    /*
        conn->getConn返回false的话,HeartBeat due更新
        返回true则avail设false,HeartBeat due更新
    */
    public:
    AppendEntriesRPC(RaftConnection *conn)
        :busy_(false),
        conn_(conn),
        flag_("")
    {}

    void call(AppendEntryArg arg)
    {
        conn_->getLoop()->insertQueue(bind(bind(&AppendEntriesRPC::callInloop, this, arg)));
    }


    void callInloop(AppendEntryArg arg)
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
    void occupyRPC(string flag)
    {
        //锁?
        flag_ = flag;
        busy_ = true;
    }

    void releaseRPC()
    {
        //锁?
        busy_ = false;
        removeFlag();
    }
    
    void setFlag(string flag)
    {
        flag_ = flag;
    }

    void removeFlag()
    {
        flag_ = "";
    }
    
    bool match(string flag)
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
    private:
    //锁?
    bool busy_;
    RaftConnection *conn_;
    string flag_;
};

class RequestVoteRPC
{
    /*
        conn->getConn返回false的话,HeartBeat due更新
        返回true则avail设false,HeartBeat due更新
    */
    public:
    RequestVoteRPC(RaftConnection *conn)
        :conn_(conn)
    {}

    void callInloop(RequestVoteArg arg)
    {
        printf("___time:%ld________ReqVote Call_____________\n", FlagBuilder::getTime());
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

    void call(RequestVoteArg arg)
    {   
        conn_->getLoop()->insertQueue(bind(&RequestVoteRPC::callInloop, this, arg));
    }


    void occupyRPC(string flag)
    {
        //锁?
        flag_ = flag;
        busy_ = true;
    }

    void releaseRPC()
    {
        //锁?
        busy_ = false;
        removeFlag();
    }
    
    void setFlag(string flag)
    {
        flag_ = flag;
    }

    void removeFlag()
    {
        flag_ = "";
    }
    
    bool match(string flag)
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

    private:
    //锁?
    bool busy_ = false;
    RaftConnection *conn_;
    string flag_;
};
/* 每个RaftNode属于一个Eventloop */
//RaftNode是对其余节点的抽象
/* RaftNode 需要用shared_ptr保存 */
class RaftNode
{
    public:
    RaftNode(NodeInfo info/*包括peer ip port等*/,  string name, Eventloop *loop, function<void(shared_ptr<Connection>)> cb)
        :conn_(new RaftConnection(info, loop, cb)),
        name_(info.getName()),
        requestVoteRpc(new RequestVoteRPC(&*conn_)),
        appendEntryRpc(new AppendEntriesRPC(&*conn_)),
        loop_(loop),
        nodeId_(info.getNodeId())
    {}


    AppendEntriesRPC *getAppendEntryRpc()
    {
        return &*appendEntryRpc;
    }

    RequestVoteRPC *getRequestVoteRpc()
    {
        return &*requestVoteRpc;
    }

    int getNodeId()
    {
        return nodeId_;
    }

    void resetHeartBeatdue(function<void()> callback)
    {
        printf("____time:%ld______ResetHeartBeat_noid: %d__name: %s__________\n",FlagBuilder::getTime(), nodeId_, name_.c_str());
        int ms = 50;/* election due */
        //ms *= 10;
        double sec = ms * 0.001;
        loop_->runTimeAfter(sec, callback, name_);
    }

    void cancelHeartBeat()
    {
        loop_->cancelTime(name_);
    }

    private:
    unique_ptr<RaftConnection> conn_;
    string name_;
    unique_ptr<RequestVoteRPC> requestVoteRpc;
    unique_ptr<AppendEntriesRPC> appendEntryRpc;
    Eventloop *loop_;
    int nodeId_;
};


class RaftServer
{
    public:
    RaftServer(int nodeId, vector<NodeInfo> infos, int port)
        :serv_(new Server(4, port)),
        conns_(new set< shared_ptr<Connection> >()),
        nodes_(nullptr),
        statMachine_(new StateMachine( nodeId, infos.size() )),
        role_(follower),
        nodeId_(nodeId),
        voteGranted_(0),
        infos_(infos),
        mutex_()
    {
        serv_->setReadCallback(bind(&RaftServer::onRpcReqRead, this, placeholders::_1, placeholders::_2));
    }

    void start()
    {
        struct timeval tval;
        gettimeofday(&tval, nullptr);
        const int million = 1000000;
        int64_t n = tval.tv_sec * million + tval.tv_usec;
        srand(n);
        int electms = rand( ) % 100 + 100;/* election due  100ms-200ms */ 
        //electms *= 10;
        double electsec = electms * 0.001;
        /* 此处回调函数被回调的位置是主线程 */
        serv_->getLoop()->runTimeAfter(electsec, bind(&RaftServer::becomeCandidate, this), "election");
        serv_->start();
    }
    
    /* 可能被主线程调用也可能被所属线程调用 */
    void becomeCandidate()
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
        statMachine_->incTerm();
        printf("\n\n__time:%ld_________becomeCandidate__term: %d___________\n\n\n",FlagBuilder::getTime(), statMachine_->getTerm());
        if (role_ == follower)
        {
            nodes_.reset(nullptr);
            if (!nodes_)
            {
                //FIXME
                nodes_.reset(new vector< shared_ptr<RaftNode> >());
                for (int i = 0; i < infos_.size(); i++)
                {
                    nodes_->push_back(nullptr);
                }
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
                        printf("____time:%ld_______ReqestVote Call to nodeid %d_____________\n",FlagBuilder::getTime(), node->getNodeId());
                        rpc->call(arg);
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
    
    void requestVoteDueCallback(weak_ptr<RaftNode> node)
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
                printf("___time:%ld________ReqestVote Call to nodeid %d_____________\n",FlagBuilder::getTime(), ptr->getNodeId());
                rpc->call(arg);
                ptr->resetHeartBeatdue(bind(&RaftServer::requestVoteDueCallback, this, weak_ptr<RaftNode>(node)));
            }
        }
    }

    void becomeLeaderWithLock()
    {
        printf("\n\n_____time:%ld______becomeLeader_term:%d____________\n\n\n",FlagBuilder::getTime(), statMachine_->getTerm());
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
                        printf("__time:%ld_________AppendEntry Call to nodeid %d_____________\n",FlagBuilder::getTime(), node->getNodeId());
                        rpc->call(arg);
                        node->resetHeartBeatdue(bind(&RaftServer::appendEntryDueCallback, this, weak_ptr<RaftNode>(node)));
                    }
                }
            }
        }
        cancelElectTime();
    }

    void appendEntryDueCallback(weak_ptr<RaftNode> node)
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
                printf("___time:%ld________AppendEntry Call to nodeid %d_____________\n",FlagBuilder::getTime(), ptr->getNodeId());
                //tip: 2020/3/8 FIXME HeartBeat计时器回调callback是在其所属线程回调, 即可同步回调callInloop
                //注意该函数已上锁,在newRaftCallback函数也进行上锁,call调用runInloop实际上是同步调用
                //因此会造成死锁
                //已修复

                rpc->call(arg);
                /*  call的runInloop是对同一个线程是同步调用 */
                ptr->resetHeartBeatdue(bind(&RaftServer::appendEntryDueCallback, this, weak_ptr<RaftNode>(node)));
            }
        }
    }

    /*  在conn对应的线程调用 */
    void newRaftConnCallback(shared_ptr<Connection> conn)
    {
        printf("New Raft Conn Call Back Before\n");
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
        printf("New Raft Conn Call Back After\n");
    }

    void raftConnClose(shared_ptr<Connection> conn)
    {
        serv_->getLoop()->runInloop(bind(&RaftServer::raftConnCloseInloop, this, conn));
        // 需要runInloop内层函数
        // 从set中移除计数指针指向的连接
        // 在RaftConnection析构时,析构函数调用: loop->runInloop(bind(handleClose)
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
    /* 此函数在RaftNode所属线程被调用,对主线程的statMachine的操作需要加锁/runInloop/主线程runInloop函数 */
    void onRpcRespRead(Buffer *buf, shared_ptr<Connection> conn)
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
                printf("___time:%ld_______Recv VotResp from %d_____________\n",FlagBuilder::getTime(), nodeId);
                string flag = res.getFlag();
                int term = res.getTerm();
                string success = res.getSuccess();
                MutexGuard guard(mutex_);
                if (role_ == candidate)
                {
                    //TODO shared_ptr保存RaftNode
                    //**FIXME** 锁?
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
                printf("__time:%ld_________Recv ApdRes from %d_____________\n",FlagBuilder::getTime(), nodeId);
                string success = res.getSuccess();
                int term = res.getTerm();
                int matchIndex = res.getMatchIndex();
                string flag = res.getFlag();
                printf("___________Test NodeId %d_____________\n", nodeId);
                printf("___________Test MatchIndex %d_____________\n", matchIndex);
                printf("___________Test succ %s_____________\n", success.c_str());
                MutexGuard guard(mutex_);
                if (role_ == leader)
                {
                    //releaseRPC need lock?
                    //AppendEntriesRPC Response
                    /* TODO 考虑仍使用index来表示 */
                    //TODO shared_ptr保存RaftNode
                    
                    
                    //**FIXME** 锁?
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
                                statMachine_->showVector();
                            }
                            else/* success false*/
                            {
                                if (term > statMachine_->getTerm())
                                {
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
                                statMachine_->showVector();
                                int preIndex = statMachine_->getPreIndex(nodeId);
                                int preTerm = statMachine_->getPreTerm(nodeId);
                                vector<Log> log;
                                statMachine_->getEntry(preIndex + 1, log);//序列化时会加上logsize
            
                                int commitIndex = statMachine_->getCommitIndex();
                                AppendEntryArg arg(nodeId_, term, preIndex, preTerm, log, commitIndex, flag);
                                AppendEntriesRPC *rpc = node->getAppendEntryRpc();
                                printf("___time:%ld________AppendEntry to nodeid %d_____________\n",FlagBuilder::getTime(), node->getNodeId());
                                rpc->call(arg);
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
                printf("*debug* error in RpcRespRead\n");
            }
            
        }/* end while */
    }

    void becomeFollower()
    {
        /* 此操作会将所有的RaftNode释放 */
        MutexGuard guard(mutex_);
        nodes_.reset(nullptr);
        role_ = follower;
        resetElectionDue();
    }

    void becomeFollowerWithLock()
    {
        printf("\n\n____time:%ld_______becomeFollower_term:%d____________\n\n\n",FlagBuilder::getTime(), statMachine_->getTerm());
        nodes_.reset(nullptr);
        role_ = follower;
        resetElectionDue();
    }

    void resetElectionDue()
    {
        printf("__________ResetElection__________\n");
        int electms = rand( ) % 100 + 100;/* election due  100ms-200ms */ 
        //electms *= 10;
        double electsec = electms * 0.001;
        serv_->getLoop()->runTimeAfter(electsec, bind(&RaftServer::becomeCandidate, this), "election");
    }

    void cancelElectTime()
    {
        serv_->getLoop()->cancelTime("election");
    }

    void onRpcReqRead(Buffer *buf, shared_ptr<Connection> conn)
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
                printf("____time:%ld_______Recv VotReq from %d_____________\n",FlagBuilder::getTime(), nodeId);
                string flag = req.getFlag();
                int lastLogIndex = req.getLastLogIndex();
                int lastLogTerm = req.getLastLogTerm();
                //不管是什么角色,leader term小 则需要让leader更新term
                
                /* statMachine_为共用资源 */
                MutexGuard guard(mutex_);
                if (term < statMachine_->getTerm())
                {
                    RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                    printf("____time:%ld_______Resp ReqVote False_____________\n",FlagBuilder::getTime());
                    conn->send(resp.toString());
                }
                else if ( term == statMachine_->getTerm() )
                {
                    /* 任期相等的候选者反对票 */
                    if (role_ == candidate)
                    {
                        RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                        printf("____time:%ld_______Resp ReqVote False_____________\n",FlagBuilder::getTime());
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
                        printf("____time:%ld_______Resp ReqVote False_____________\n", FlagBuilder::getTime());
                        conn->send(resp.toString());
                    }
                    else if (role_ == leader)
                    {
                        RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                        printf("____time:%ld_______Resp ReqVote False_____________\n", FlagBuilder::getTime());
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
                                printf("____time:%ld_______Resp ReqVote True_____________\n",FlagBuilder::getTime());
                                conn->send(resp.toString());
                                resetElectionDue();
                            }
                            else if (lastLogIndex == statMachine_->getLastLogIndex() && lastLogTerm >= statMachine_->getLastLogTerm())
                            {
                                //请求的lastLogIndex同长term高,赞成票
                                statMachine_->setTerm(term);
                                statMachine_->setVoteFor(nodeId);
                                RequestVoteRes resp(nodeId_, "true", statMachine_->getTerm(), flag);
                                printf("______time:%ld_____Resp ReqVote True_____________\n",FlagBuilder::getTime());
                                conn->send(resp.toString());
                                resetElectionDue();
                            }
                            else
                            {
                                RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                                printf("_____time:%ld______Resp ReqVote False___lastlog >= mine__________\n",FlagBuilder::getTime());
                                conn->send(resp.toString());
                            }
                        }
                        else
                        {
                            RequestVoteRes resp(nodeId_, "false", statMachine_->getTerm(), flag);
                            printf("_____time:%ld______Resp ReqVote False___lastlog < mine__________\n",FlagBuilder::getTime());
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
                printf("___time:%ld________Recv ApdReq from %d_____________\n",FlagBuilder::getTime(), nodeId);
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
                        becomeFollowerWithLock();
                    }
                    //连接会持续保持,需要保证rpc被读走
                }
                else if (term < statMachine_->getTerm() )
                {
                    AppendEntryRes res(nodeId_, "false", 0, statMachine_->getTerm(), flag);
                    printf("_____time:%ld______Resp ApdEntry False_____________\n",FlagBuilder::getTime());
                    conn->send(res.toString());
                }

                /* 不管是follower还是其他role都需根据log添加/拒绝日志 */
                if (term == statMachine_->getTerm())
                {
                    if (role_ != follower)
                    {
                        //产生两个leader时,如果接受到对方的ApdReq则都重新选举
                        becomeFollowerWithLock();
                    }
                    if (statMachine_->preIndexMatch(preIndex, preTerm))
                    {
                        //TODO 可修改为多条日志一起复制
                        if (logsize == 1)
                        {
                            statMachine_->insertLog(preIndex + 1, log);
                            printf("_______LOGSIZE__1______\n");
                            AppendEntryRes res(nodeId_, "true", preIndex + 1, term, flag);
                            printf("__time:%ld_________Resp AppendEntry True_____________\n", FlagBuilder::getTime());
                            resetElectionDue();
                            conn->send(res.toString());
                        }
                        else if (logsize == 0)/* no op appendentry */
                        {
                            printf("_______LOGSIZE__0______\n");
                            AppendEntryRes res(nodeId_, "true", preIndex, term, flag);
                            printf("___time:%ld________Resp AppendEntry True_____________\n", FlagBuilder::getTime());
                            resetElectionDue();
                            conn->send(res.toString());
                            /* 此日志须与转变为leader后发的第一个日志区分,leader发的空日志需要insert到日志中去,而该日志不需要 */
                        }
                    }
                    else
                    {
                        AppendEntryRes res(nodeId_, "false", 0, term, flag);
                        printf("____time:%ld_______Resp AppendEntry False_____________\n",FlagBuilder::getTime());
                        resetElectionDue();
                        conn->send(res.toString());
                    }
                }/* end if */
                //buf->retrieve(rpcsize); /* decoder已经进行此步操作 */
            }/* end if ApdReq */
            else if (type == "CliReq")
            {
                printf("___________Recv CliReq_____________\n");
                ClientReqArg req = ClientReqArg::fromString(buf);
                MutexGuard guard(mutex_);

                if (role_ == leader)
                {
                    int nextIndex = statMachine_->getNextIndex(nodeId_);
                    string oper = req.getOper();
                    string key = req.getKey();
                    string value = req.getValue();
                    int logId = req.getLogId();
                    int term = statMachine_->getTerm();
                    statMachine_->insertLog(nextIndex, Log(term, nextIndex, logId, oper, key, value));
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
                                    printf("___________AppendEntry Call to nodeid %d_____________\n", node->getNodeId());
                                    rpc->call(arg);
                                    node->resetHeartBeatdue(bind(&RaftServer::appendEntryDueCallback, this, weak_ptr<RaftNode>(node)));
                                }
                            }
                        }
                    }
                }
                else
                {
                    char buf[8] = {0};
                    int size = 4;
                    memcpy(buf, &size, sizeof(int));
                    string resp = string(buf, 4) + string("fail");
                    conn->send(resp);
                }
                
            }/* end CliReq */
        }/* end while */
    }/* end onReadReq */

    private:
    unique_ptr< Server > serv_;
    unique_ptr< set< shared_ptr<Connection> > > conns_;//此处的set用于维护连接
    unique_ptr< vector< shared_ptr<RaftNode> > > nodes_;
    unique_ptr< StateMachine > statMachine_;
    enum role{follower, leader, candidate} role_;
    int nodeId_;
    int voteGranted_ = 0;
    vector<NodeInfo> infos_; /* FIXME */
    Mutex mutex_;
};

/* 方便起见每次只同步一条日志 */
