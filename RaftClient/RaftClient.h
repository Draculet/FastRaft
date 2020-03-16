#ifndef _______RAFTCLIENT_________
#define _______RAFTCLIENT_________

#include "../FastNet/include/Server.h"
#include "../FastNet/include/Connection.h"
#include "ConfigReader.h"
#include "NodeInfo.h"
#include "FlagBuilder.h"
#include <algorithm>
#include <set>
#include <map>

using namespace std;
using namespace net;
using namespace base;
struct ReturnArg;
class ClientReqArg;
class ClientRes;

class Client
{
    public:
    Client(Mutex *mutex, Condition *cond, vector<NodeInfo> &infos);
    Eventloop *getLoop();
    void start();
    void onRead(Buffer *buf, shared_ptr<Connection> conn);
    void commitRequest(string oper, string key, string value, int logId, ReturnArg *arg);
    void commitRequestInloop(string oper, string key, string value, int logId, ReturnArg *arg);
    void sendReqDue(string flag);
    void sendReq(int nodeId, int logId, string flag, string oper, string key, string value);
    void onClose();
    shared_ptr<Connection> getConn(int nodeId);
    static int connectPeer(string ip, int port, NetAddr *addr);
    void release();

    private:
    unique_ptr<ThreadLoop> threadloop_;
    Mutex *mutex_;
    Condition *cond_;
    vector<NodeInfo> infos_;
    shared_ptr<Connection> conn_;
    int curNodeId_;
    int logId_;
    string flag_ = "";
    string curOper_ = "";
    string curKey_ = "";
    string curValue_ = "";
    ReturnArg *arg_;
};

struct ReturnArg
{
    ReturnArg()
        :ret(false),
        str()
    {}

    bool ret;
    string str;
};

class ClientReqArg
{
    public:
    ClientReqArg(int logId, string flag, string oper, string key, string value)
        :logId_(logId),
        flag_(flag),
        oper_(oper),
        key_(key),
        value_(value)
    {}

    ClientReqArg()
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

    string getFlag()
    {
        return flag_;
    }

    string toString()
    {
        char buf[64 + key_.size() + value_.size()] = {0};
        string type = "CliReq";
        int logId = htonl(logId_);
        memcpy(buf + 4, type.c_str(), 6);
        memcpy(buf + 10, &logId, 4);
        memcpy(buf + 14, flag_.c_str(), 4);
        memcpy(buf + 18, oper_.c_str(), 3);
        int ksize = htonl(key_.size());
        memcpy(buf + 21, &ksize, 4);
        memcpy(buf + 25, key_.c_str(), key_.size());
        int total = 21 + key_.size();
        if (oper_ == "set")
        {
            int vsize = htonl(value_.size());
            memcpy(buf + 25 + key_.size(), &vsize, 4);
            memcpy(buf + 29 + key_.size(), value_.c_str(), value_.size());
            total = 25 + key_.size() + value_.size();
        }
        int total_ = htonl(total);
        memcpy(buf, &total_, 4);
        return string(buf, total + 4);
    }

    private:
    int logId_;
    string flag_; /* 4byte定长 */
    string oper_;
    string key_;
    string value_;
};

class ClientRes
{
    public:
    ClientRes(int logId, int leaderId, string flag, string resp)
        :logId_(logId),
        leaderId_(leaderId),
        flag_(flag),
        resp_(resp)
    {}

    int getLogId()
    {
        return logId_;
    }
    
    string getResp()
    {
        return resp_;
    }

    string getFlag()
    {
        return flag_;
    }

    int getLeaderId()
    {
        return leaderId_;
    }

    static ClientRes fromString(Buffer *buf)
    {
        buf->retrieve(4);
        int logId;
        string _logId = buf->retrieveAsString(4);
        memcpy(&logId, _logId.c_str(), 4);
        logId = ntohl(logId);
        int leaderId;
        string _leaderId = buf->retrieveAsString(4);
        memcpy(&leaderId, _leaderId.c_str(), 4);
        leaderId = ntohl(leaderId);
        string flag = buf->retrieveAsString(4);
        int respsize;
        string _respsize = buf->retrieveAsString(4);
        memcpy(&respsize, _respsize.c_str(), 4);
        respsize = ntohl(respsize);
        string resp = buf->retrieveAsString(respsize);
        return ClientRes(logId, leaderId, flag, resp);
    }

    private:
    int logId_;
    int leaderId_;
    string flag_; /* 4byte定长 */
    string resp_;/* 不定长 */
};





#endif