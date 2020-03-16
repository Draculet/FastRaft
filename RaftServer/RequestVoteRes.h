#ifndef _____REQVOTERES_H____
#define _____REQVOTERES_H____

#include <string>

class RequestVoteRes
{
    public:
    RequestVoteRes(int nodeId, std::string success, int term, std::string flag);
    int getNodeId();
    int getTerm();
    std::string getSuccess();
    std::string getFlag();
    std::string toString();

    private:
    int nodeId_;
    std::string success_;
    int term_;
    std::string flag_;
};

#endif
