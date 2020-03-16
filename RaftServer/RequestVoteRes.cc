#include "RequestVoteRes.h"
#include "RpcEncode.h"
using namespace std;

RequestVoteRes::RequestVoteRes(int nodeId, string success, int term, string flag)
    :nodeId_(nodeId),
    success_(success),
    term_(term),
    flag_(flag)
{}

int RequestVoteRes::getNodeId()
{
    return nodeId_;
}

int RequestVoteRes::getTerm()
{
    return term_;
}

string RequestVoteRes::getSuccess()
{
    return success_;
}

string RequestVoteRes::getFlag()
{
    return flag_;
}

string RequestVoteRes::toString()
{
    return RpcEncoder::votResEncode(*this);
}