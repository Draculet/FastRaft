#ifndef ______CLIENTREQARG_H_____
#define ______CLIENTREQARG_H_____

#include "../FastNet/include/Server.h"
#include <string>

class ClientReqArg
{
    public:
    ClientReqArg(int logId, std::string flag, std::string oper, std::string key, std::string value);
    int getLogId();
    std::string getOper();
    std::string getKey();
    std::string getValue();
    std::string getFlag();
    static ClientReqArg fromString(net::Buffer *buf);

    private:
    int logId_;
    std::string flag_; /* 4byte定长 */
    std::string oper_;
    std::string key_;
    std::string value_;
};

#endif