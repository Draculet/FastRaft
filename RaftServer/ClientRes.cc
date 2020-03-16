#include "ClientRes.h"
#include <arpa/inet.h>
#include <cstring>
using namespace std;

ClientRes::ClientRes(int logId, int leaderId, string flag, string resp)
    :logId_(logId),
    leaderId_(leaderId),
    flag_(flag),
    resp_(resp)
{}

int ClientRes::getLogId()
{
    return logId_;
}

string ClientRes::getResp()
{
    return resp_;
}

string ClientRes::getFlag()
{
    return flag_;
}

int ClientRes::getLeaderId()
{
    return leaderId_;
}

string ClientRes::toString()
{
    char buf[64 + resp_.size()] = {0};
    int logId = htonl(logId_);
    memcpy(buf + 4, &logId, 4);
    int leaderId = htonl(leaderId_);
    memcpy(buf + 8, &leaderId, 4);
    memcpy(buf + 12, flag_.c_str(), 4);
    int respsize = htonl(resp_.size());
    memcpy(buf + 16, &respsize, 4);
    memcpy(buf + 20, resp_.c_str(), resp_.size());
    int total = 16 + resp_.size();
    int total_ = htonl(total);
    memcpy(buf, &total_, 4);
    return string(buf, total + 4);
}
