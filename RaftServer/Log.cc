#include "Log.h"

using namespace std;
using namespace net;
using namespace base;

Log::Log(int term, int index,
        int logId, string oper,
            string key, string value,
            function<void(string)> callback)
    :term_(term),
    index_(index),
    logId_(logId),
    oper_(oper), /* oper: set get del*/
    key_(key),
    value_(value), /* oper为get del时, value_为"" */
    callback_(callback)
{}

Log::Log()
    :term_(-1),
    index_(-1),
    logId_(-1),
    oper_(""),
    key_(""),
    value_("")
{}

string Log::toString()
{
    int total = 0;
    int ksize = key_.size();
    int vsize = value_.size();
    int size = ksize + vsize + 3 + 8;
    char buf[size + 32] = {0};
    int term = htonl(term_);
    memcpy(buf, &term, 4);
    int index = htonl(index_);
    memcpy(buf + 4, &index, 4);
    int logId = htonl(logId_);
    memcpy(buf + 8, &logId, 4);



    memcpy(buf + 12, oper_.c_str(), 3);
    ksize = htonl(ksize);
    memcpy(buf + 15, &ksize, 4);
    memcpy(buf + 19, key_.c_str(), key_.size());
    /* get和del只有key参数 */
    total = 19 + key_.size();
    if (oper_ == "set")
    {
        vsize = htonl(vsize);
        memcpy(buf + 19 + key_.size(), &vsize, 4);
        memcpy(buf + 23 + key_.size(), value_.c_str(), value_.size());
        total = 23 + key_.size() + value_.size();
    }
    return string(buf, total);
}

Log Log::fromString(Buffer *buf)
{
    int term;
    string _term = buf->retrieveAsString(4);
    memcpy(&term, _term.c_str(), 4);
    term = ntohl(term);
    int index;
    string _index = buf->retrieveAsString(4);
    memcpy(&index, _index.c_str(), 4);
    index = ntohl(index);
    int logId;
    string _logId = buf->retrieveAsString(4);
    memcpy(&logId, _logId.c_str(), 4);
    logId = ntohl(logId);


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
    return Log(term, index, logId, oper, key, value);
}

int Log::getTerm()
{
    return term_;
}

int Log::getIndex()
{
    return index_;
}
int Log::getLogId()
{
    return logId_;
}

string Log::getOper()
{
    return oper_;
}

string Log::getKey()
{
    return key_;
}

string Log::getValue()
{
    return value_;
}

void Log::callCallback(string str)
{
    if (callback_)
    {
        callback_(str);
    }
}
