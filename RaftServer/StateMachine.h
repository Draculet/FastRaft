#ifndef ______STATEMACHINE_H______
#define ______STATEMACHINE_H______

#include "Log.h"
#include <vector>
#include "../FastNet/include/Server.h"

class StateMachine
{
    public:
    StateMachine(int nodeId, int nodenum);
    void getEntry(int nextIndex, std::vector<Log> &entry);
    int getTerm();
    void incTerm();
    void setTerm(int term);
    /* 应在插入日志之后init */
    void leaderInit();
    void updateNextIndex(int nodeid, int next);
    void updateMatchIndex(int nodeid, int match);
    int maxMatch();
    void setCommitIndex(int commit);
    void updateCommitIndex();
    void updateTerm(int term);
    void setVoteFor(int vote);
    int getVoteFor();
    /* 两种场景 */
    //1* 尾插日志, lastLogIndex++, 同时需要为log开辟空间
    //2* 日志在index被覆盖, 后面的日志全部截断,无需开辟空间
    void insertLog(int index, Log log);
    int getLastLogIndex();
    int getLastLogTerm();
    bool preIndexMatch(int preIndex, int preTerm);
    int getNextIndex();
    int getNextIndex(int nodeid);
    int getPreIndex(int nodeid);
    int getPreTerm(int nodeid);
    void retreatIndex(int nodeid);
    int getCommitIndex();
    Log &getLog(int index);
    void showNodeInfo();

    private:

    int currentTerm = 0;
    int voteFor = -1;
    std::vector<Log> logs;//第一个index为1
    int commitIndex = 0;
    //是否更改为map?
    std::vector<int> nextIndex; //size为节点数,初始化为 lastLogIndex + 1;
    std::vector<int> matchIndex; //size同上,初始化为0
    int firstLogIndex = 0;/*使用快照则需要此字段*/
    int lastLogIndex = 0;
    int nodeId_;
    int nodenum_;
};

#endif