#ifndef ____REQUESTVOTEARG_H____
#define ____REQUESTVOTEARG_H____

#include <string>

class RequestVoteArg
{
    public:
    /* nodeId即候选者nodeId */
    RequestVoteArg(int nodeId, int term, int lastLogIndex, int lastLogTerm, std::string flag);
    std::string toString();
    int getNodeId();
    int getTerm();
    int getLastLogIndex();
    int getLastLogTerm();
    std::string getFlag();

    private:
    int nodeId_;
    int term_;
    int lastLogIndex_;
    int lastLogTerm_;
    std::string flag_;
};

#endif