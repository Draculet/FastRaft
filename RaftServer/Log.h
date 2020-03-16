#ifndef _____LOG_H_______
#define _____LOG_H_______

#include <functional>
#include <string>
#include "../FastNet/include/Server.h"

class Log
{
    public:
    Log(int term, int index,
            int logId, std::string oper,
                std::string key, std::string value,
                    std::function<void(std::string)> callback = std::function<void(std::string)>());
    Log();
    std::string toString();
    static Log fromString(net::Buffer *buf);
    int getTerm();
    int getIndex();
    int getLogId();
    std::string getOper();
    std::string getKey();
    std::string getValue();
    void callCallback(std::string str);

    int term_;
    int index_;
    int logId_;//作用是client的幂等性
    std::string oper_;
    std::string key_;
    std::string value_;
    std::function<void(std::string)> callback_;
};

#endif