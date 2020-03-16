#include "AppendEntryArg.h"
#include "RpcEncode.h"
using namespace std;


AppendEntryArg::AppendEntryArg()
{}

AppendEntryArg::AppendEntryArg(int nodeId, int term,  int preLogIndex, int preLogTerm, vector<Log> entries, int commitIndex, string flag)
    :nodeId_(nodeId),
    term_(term),
    preLogIndex_(preLogIndex),
    preLogTerm_(preLogTerm),
    entry_(entries),
    commitIndex_(commitIndex),
    flag_(flag)
{}

int AppendEntryArg::getNodeId()
{
    return nodeId_;
}

int AppendEntryArg::getTerm()
{
    return term_;
}

int AppendEntryArg::getPreLogIndex()
{
    return preLogIndex_;
}

int AppendEntryArg::getPreLogTerm()
{
    return preLogTerm_;
}

vector<Log> AppendEntryArg::getEntry()
{
    return entry_;
}

int AppendEntryArg::getCommitIndex()
{
    return commitIndex_;
}

string AppendEntryArg::getFlag()
{
    return flag_;
}

string AppendEntryArg::toString()
{
    return RpcEncoder::apdReqEncode(*this);
}

