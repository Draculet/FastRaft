#include "RpcEncode.h"
#include "AppendEntryArg.h"
#include "AppendEntryRes.h"
#include "RequestVoteArg.h"
#include "RequestVoteRes.h"
#include "../FastNet/include/UnixTime.h"

using namespace std;

string RpcEncoder::apdReqEncode(AppendEntryArg &arg)
{
    int nodeId = arg.getNodeId(); /* 4byte */
    int term = arg.getTerm();   /* 4byte */
    int preLogIndex = arg.getPreLogIndex(); /* 4byte */
    int preLogTerm = arg.getPreLogTerm();    /* 4byte */
    int commitIndex = arg.getCommitIndex();
    string flag = arg.getFlag();

    vector<Log> entry = arg.getEntry();
    //tip: entry的编码放在最后
    //commitIndex 和 flag提前
    int lognum = 0; /* 4byte */
    string logStr = "";
    if (entry.size() == 1)
    {
        lognum = 1;
        logStr = entry[0].toString(); /* logStr.size() byte */
    }
    int total = 0;
    char buf[96 + logStr.size() + flag.size()] = {0};
    string type = "ApdReq";
    memcpy(buf + 4, type.c_str(), 6);
    nodeId = htonl(nodeId);
    memcpy(buf + 10, &nodeId, 4);
    term = htonl(term);
    memcpy(buf + 14, &term, 4);
    preLogIndex = htonl(preLogIndex);
    memcpy(buf + 18, &preLogIndex, 4);
    preLogTerm = htonl(preLogTerm);
    memcpy(buf + 22, &preLogTerm, 4);
    int lognum_ = htonl(lognum);
    memcpy(buf + 26, &lognum_, 4);
    commitIndex = htonl(commitIndex);
    memcpy(buf + 30, &commitIndex, 4);
    int fsize = htonl(flag.size());
    memcpy(buf + 34, &fsize, 4);
    memcpy(buf + 38, flag.c_str(), flag.size());
    total = 34 + flag.size();

    /* 最后将log编码 */
    if (lognum == 1)
    {
        memcpy(buf + 38 + flag.size(), logStr.c_str(), logStr.size());
        total = 34 + flag.size() + logStr.size();
    }
    int total_ = htonl(total);
    memcpy(buf, &total_, 4);
    return string(buf, total + 4);
}


string RpcEncoder::apdResEncode(AppendEntryRes &arg)
{
    int nodeId = arg.getNodeId(); /* 4byte */
    string succ = arg.getSuccess();   /* 4byte */
    int matchIndex = arg.getMatchIndex(); /* 4byte */
    int term = arg.getTerm();    /* 4byte */
    string flag = arg.getFlag();
    int total = 0;
    char buf[48] = {0};
    string type = "ApdRes";
    memcpy(buf + 4, type.c_str(), 6);
    nodeId = htonl(nodeId);
    memcpy(buf + 10, &nodeId, 4);
    memcpy(buf + 14, succ.c_str(), 4);/* 4byte true|fals */
    matchIndex = htonl(matchIndex);
    memcpy(buf + 18, &matchIndex, 4);
    term = htonl(term);
    memcpy(buf + 22, &term, 4);
    int fsize = htonl(flag.size());
    memcpy(buf + 26, &fsize, 4);
    memcpy(buf + 30, flag.c_str(), flag.size());
    total = 26 + flag.size();
    int total_ = htonl(total);
    memcpy(buf, &total_, 4);
    return string(buf, total + 4);
}

string RpcEncoder::votResEncode(RequestVoteRes &arg)
{
    int nodeId = arg.getNodeId(); /* 4byte */
    string succ = arg.getSuccess();   /* 4byte */
    int term = arg.getTerm();    /* 4byte */
    string flag = arg.getFlag();
    int total = 0;
    char buf[48] = {0};
    string type = "VotRes";
    memcpy(buf + 4, type.c_str(), 6);
    nodeId = htonl(nodeId);
    memcpy(buf + 10, &nodeId, 4);
    memcpy(buf + 14, succ.c_str(), 4);/* 4byte true|fals */
    term = htonl(term);
    memcpy(buf + 18, &term, 4);
    int fsize = htonl(flag.size());
    memcpy(buf + 22, &fsize, 4);
    memcpy(buf + 26, flag.c_str(), flag.size());
    total = 22 + flag.size();
    int total_ = htonl(total);
    memcpy(buf, &total_, 4);
    return string(buf, total + 4);
}

string RpcEncoder::votReqEncode(RequestVoteArg &arg)
{
    int nodeId = arg.getNodeId(); /* 4byte */
    int term = arg.getTerm();   /* 4byte */
    int lastLogIndex = arg.getLastLogIndex(); /* 4byte */
    int lastLogTerm = arg.getLastLogTerm();    /* 4byte */
    string flag = arg.getFlag();

    int total = 0;
    char buf[96 + flag.size()] = {0};
    string type = "VotReq";
    memcpy(buf + 4, type.c_str(), 6);
    nodeId = htonl(nodeId);
    memcpy(buf + 10, &nodeId, 4);
    term = htonl(term);
    memcpy(buf + 14, &term, 4);
    lastLogIndex = htonl(lastLogIndex);
    memcpy(buf + 18, &lastLogIndex, 4);
    lastLogTerm = htonl(lastLogTerm);
    memcpy(buf + 22, &lastLogTerm, 4);
    int fsize = htonl(flag.size());
    memcpy(buf + 26, &fsize, 4);
    memcpy(buf + 30, flag.c_str(), flag.size());
    total = 26 + flag.size();

    int total_ = htonl(total);
    memcpy(buf, &total_, 4);
    return string(buf, total + 4);
}