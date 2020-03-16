#ifndef _____APPENDENTRYARG_H_____
#define _____APPENDENTRYARG_H_____

#include <string>
#include <vector>
#include "Log.h"

struct AppendEntryArg
{
    public:
    AppendEntryArg();

    AppendEntryArg(int nodeId, int term,  int preLogIndex, int preLogTerm, std::vector<Log> entries, int commitIndex, std::string flag);

    int getNodeId();
    int getTerm();
    
    int getPreLogIndex();
    int getPreLogTerm();
    std::vector<Log> getEntry();
    int getCommitIndex();
    std::string getFlag();
    std::string toString();
    
    private:
    int nodeId_;
    int term_;
    int preLogIndex_;
    int preLogTerm_;
    std::vector<Log> entry_;
    int commitIndex_;
    std::string flag_;
};

#endif