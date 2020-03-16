#include "RequestVoteArg.h"
#include "RpcEncode.h"

using namespace std;

/* nodeId即候选者nodeId */
RequestVoteArg::RequestVoteArg(int nodeId, int term, int lastLogIndex, int lastLogTerm, string flag)
    :nodeId_(nodeId),
    term_(term),
    lastLogIndex_(lastLogIndex),
    lastLogTerm_(lastLogTerm),
    flag_(flag)
{}

string RequestVoteArg::toString()
{
    return RpcEncoder::votReqEncode(*this);
}

int RequestVoteArg::getNodeId()
{
    return nodeId_;
}

int RequestVoteArg::getTerm()
{
    return term_;
}

int RequestVoteArg::getLastLogIndex()
{
    return lastLogIndex_;
}

int RequestVoteArg::getLastLogTerm()
{
    return lastLogTerm_;
}

string RequestVoteArg::getFlag()
{
    return flag_;
}
