#ifndef _______RPCENCODE_H______
#define _______RPCENCODE_H______

#include <string>

class AppendEntryArg;
class AppendEntryRes;
class RequestVoteArg;
class RequestVoteRes;

class RpcEncoder
{
    public:
    static std::string apdReqEncode(AppendEntryArg &arg);
    static std::string apdResEncode(AppendEntryRes &arg);
    static std::string votResEncode(RequestVoteRes &arg);
    static std::string votReqEncode(RequestVoteArg &arg);
};

#endif