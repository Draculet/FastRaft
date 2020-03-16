#ifndef _____CLIENTRES_H_____
#define _____CLIENTRES_H_____

#include <string>

class ClientRes
{
    public:
    ClientRes(int logId, int leaderId, std::string flag, std::string resp);
    int getLogId();
    std::string getResp();
    std::string getFlag();
    int getLeaderId();
    std::string toString();

    private:
    int logId_;
    int leaderId_;
    std::string flag_; /* 4byte定长 */
    std::string resp_;/* 不定长 */
};

#endif