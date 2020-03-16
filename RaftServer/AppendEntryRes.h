#ifndef ____APPENDENTRYRES_H____
#define ____APPENDENTRYRES_H____
#include <string>

struct AppendEntryRes
{
    public:
    AppendEntryRes(int nodeId, std::string success, int matchIndex, int term, std::string flag);
    int getNodeId();
    int getTerm();
    std::string getSuccess();
    int getMatchIndex();
    std::string getFlag();
    std::string toString();

    private:
    int nodeId_;
    std::string success_;
    int matchIndex_;
    int term_;
    std::string flag_;
};

#endif