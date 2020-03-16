#include "AppendEntryRes.h"
#include "RpcEncode.h"

using namespace std;

AppendEntryRes::AppendEntryRes(int nodeId, string success, int matchIndex, int term, string flag)
    :nodeId_(nodeId),
    success_(success),
    matchIndex_(matchIndex),
    term_(term),
    flag_(flag)
{}

int AppendEntryRes::getNodeId()
{
    return nodeId_;    
}

int AppendEntryRes::getTerm()
{
    return term_;
}

string AppendEntryRes::getSuccess()
{
    return success_;
}

int AppendEntryRes::getMatchIndex()
{
    return matchIndex_;
}

string AppendEntryRes::getFlag()
{
    return flag_;
}

string AppendEntryRes::toString()
{
    return RpcEncoder::apdResEncode(*this);
}