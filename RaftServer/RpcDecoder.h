#ifndef ____RPCDECODER_H_____
#define ____RPCDECODER_H_____

#include "../FastNet/include/Server.h"
#include <string>

class RequestVoteArg;
class RequestVoteRes;
class AppendEntryArg;
class AppendEntryRes;
class RpcDecoder
{
    public:
    //设计为static 
    //需要处理的部分以while执行
    /* 
        typeDecoder会修改type为解析出的类型标识, 返回rpc的大小
        返回-1表示Buffer不完整
        type为定长6字节的字符串
    */
    static int typeDecoder(net::Buffer *buf, std::string &type);
    static RequestVoteArg votReqDecode(net::Buffer *buf, int total);
    static RequestVoteRes votResDecode(net::Buffer *buf, int total);
    static AppendEntryArg apdReqDecode(net::Buffer *buf, int total);
    static AppendEntryRes apdResDecode(net::Buffer *buf, int total);
    
};

#endif