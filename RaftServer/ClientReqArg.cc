#include "ClientReqArg.h"

using namespace std;
using namespace net;

ClientReqArg::ClientReqArg(int logId, string flag, string oper, string key, string value)
    :logId_(logId),
    flag_(flag),
    oper_(oper),
    key_(key),
    value_(value)
{}

int ClientReqArg::getLogId()
{
    return logId_;
}

string ClientReqArg::getOper()
{
    return oper_;
}

string ClientReqArg::getKey()
{
    return key_;
}

string ClientReqArg::getValue()
{
    return value_;
}

string ClientReqArg::getFlag()
{
    return flag_;
}

ClientReqArg ClientReqArg::fromString(Buffer *buf)
{
    string type = buf->retrieveAsString(6);
    int logId;
    string _logId = buf->retrieveAsString(4);
    memcpy(&logId, _logId.c_str(), 4);
    logId = ntohl(logId);
    string flag = buf->retrieveAsString(4);

    string oper = buf->retrieveAsString(3);
    int ksize;
    string _ksize = buf->retrieveAsString(4);
    memcpy(&ksize, _ksize.c_str(), 4);
    ksize = ntohl(ksize);
    string key = buf->retrieveAsString(ksize);
    string value = "";
    if (oper == "set")
    {
        int vsize;
        string _vsize = buf->retrieveAsString(4);
        memcpy(&vsize, _vsize.c_str(), 4);
        vsize = ntohl(vsize);
        value = buf->retrieveAsString(vsize);
    }

    return ClientReqArg(logId, flag, oper, key, value);
}