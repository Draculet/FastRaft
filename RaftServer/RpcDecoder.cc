#include "RpcDecoder.h"
#include "../FastNet/include/Server.h"
#include "../FastNet/include/UnixTime.h"
#include "RequestVoteArg.h"
#include "RequestVoteRes.h"
#include "AppendEntryArg.h"
#include "AppendEntryRes.h"

using namespace std;
using namespace net;
int RpcDecoder::typeDecoder(Buffer *buf, string &type)
{
    if (buf->readable() > 4)
    {
        int total = 0;
        string _total = buf->preViewAsString(4);
        memcpy(&total, _total.c_str(), 4);
        total = ntohl(total);
        if (buf->readable() >= 4 + total)
        {
            buf->retrieve(4);
            type = buf->preViewAsString(6);
            printf("-%s- Message Header Type: %s\n", UnixTime::now().toString().c_str(), type.c_str());
            return total;
        }
    }
    return -1;
}

RequestVoteArg RpcDecoder::votReqDecode(Buffer *buf, int total)
{
    string type = buf->retrieveAsString(6);
    if (type == "VotReq")
    {
        int nodeId;
        string _nodeId = buf->retrieveAsString(4);
        memcpy(&nodeId, _nodeId.c_str(), 4);
        nodeId = ntohl(nodeId);
        int term;
        string _term = buf->retrieveAsString(4);
        memcpy(&term, _term.c_str(), 4);
        term = ntohl(term);
        int lastLogIndex;
        string _lastLogIndex = buf->retrieveAsString(4);
        memcpy(&lastLogIndex, _lastLogIndex.c_str(), 4);
        lastLogIndex = ntohl(lastLogIndex);
        int lastLogTerm;
        string _lastLogTerm = buf->retrieveAsString(4);
        memcpy(&lastLogTerm, _lastLogTerm.c_str(), 4);
        lastLogTerm = ntohl(lastLogTerm);
        int flagsize;
        string _flagsize = buf->retrieveAsString(4);
        memcpy(&flagsize, _flagsize.c_str(), 4);
        flagsize = ntohl(flagsize);
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

RequestVoteRes RpcDecoder::votResDecode(Buffer *buf, int total)
{
    string type = buf->retrieveAsString(6);
    if (type == "VotRes")
    {
        int nodeId;
        string _nodeId = buf->retrieveAsString(4);
        memcpy(&nodeId, _nodeId.c_str(), 4);
        nodeId = ntohl(nodeId);
        string succ = buf->retrieveAsString(4);
        if (succ == "fals")
            succ = "false";
        int term;
        string _term = buf->retrieveAsString(4);
        memcpy(&term, _term.c_str(), 4);
        term = ntohl(term);
        int flagsize;
        string _flagsize = buf->retrieveAsString(4);
        memcpy(&flagsize, _flagsize.c_str(), 4);
        flagsize = ntohl(flagsize);
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

AppendEntryArg RpcDecoder::apdReqDecode(Buffer *buf, int total)
{
    string type = buf->retrieveAsString(6);
    if (type == "ApdReq")
    {
        vector<Log> entry;
        int nodeId;
        string _nodeId = buf->retrieveAsString(4);
        memcpy(&nodeId, _nodeId.c_str(), 4);
        nodeId = ntohl(nodeId);
        int term;
        string _term = buf->retrieveAsString(4);
        memcpy(&term, _term.c_str(), 4);
        term = ntohl(term);
        int preLogIndex;
        string _preLogIndex = buf->retrieveAsString(4);
        memcpy(&preLogIndex, _preLogIndex.c_str(), 4);
        preLogIndex = ntohl(preLogIndex);
        int preLogTerm;
        string _preLogTerm = buf->retrieveAsString(4);
        memcpy(&preLogTerm, _preLogTerm.c_str(), 4);
        preLogTerm = ntohl(preLogTerm);
        int lognum;
        string _lognum = buf->retrieveAsString(4);
        memcpy(&lognum, _lognum.c_str(), 4);
        lognum = ntohl(lognum);
        int commitIndex;
        string _commitIndex = buf->retrieveAsString(4);
        memcpy(&commitIndex, _commitIndex.c_str(), 4);
        commitIndex = ntohl(commitIndex);
        int flagsize;
        string _flagsize = buf->retrieveAsString(4);
        memcpy(&flagsize, _flagsize.c_str(), 4);
        flagsize = ntohl(flagsize);
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

AppendEntryRes RpcDecoder::apdResDecode(Buffer *buf, int total)
{
    string type = buf->retrieveAsString(6);
    if (type == "ApdRes")
    {
        int nodeId;
        string _nodeId = buf->retrieveAsString(4);
        memcpy(&nodeId, _nodeId.c_str(), 4);
        nodeId = ntohl(nodeId);
        string succ = buf->retrieveAsString(4);
        if (succ == "fals")
            succ = "false";
        int matchIndex;
        string _matchIndex = buf->retrieveAsString(4);
        memcpy(&matchIndex, _matchIndex.c_str(), 4);
        matchIndex = ntohl(matchIndex);
        int term;
        string _term = buf->retrieveAsString(4);
        memcpy(&term, _term.c_str(), 4);
        term = ntohl(term);
        int flagsize;
        string _flagsize = buf->retrieveAsString(4);
        memcpy(&flagsize, _flagsize.c_str(), 4);
        flagsize = ntohl(flagsize);
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
