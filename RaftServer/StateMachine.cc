#include "StateMachine.h"
#include "../FastNet/include/UnixTime.h"

using namespace std;
using namespace net;
using namespace base;

StateMachine::StateMachine(int nodeId, int nodenum)
    :logs(0),
    firstLogIndex(0),
    lastLogIndex(0),
    nextIndex(nodenum, lastLogIndex + 1), /*将要同步的日志*/
    matchIndex(nodenum, 0),
    nodeId_(nodeId),
    nodenum_(nodenum)
{
    logs.push_back(Log(0, 0, -1, "", "", ""));
    logs.push_back(Log(-1, -1, -1, "", "", ""));
}

void StateMachine::getEntry(int nextIndex, vector<Log> &entry)
{
    if (lastLogIndex >= nextIndex)
    {
        //每次只取一个日志
        entry.push_back(logs[nextIndex]);
    }
}

int StateMachine::getTerm()
{
    return currentTerm;
}

void StateMachine::incTerm()
{
    currentTerm++;
}

void StateMachine::setTerm(int term)
{
    currentTerm = term;
}

/* 应在插入日志之后init */
void StateMachine::leaderInit()
{
    for (int i = 0; i < nextIndex.size(); i++)
    {
        if (i != nodeId_)
        {
            nextIndex[i] = lastLogIndex + 1;
        }
    }

    /* 
    for (auto &index : nextIndex)
    {
        index = lastLogIndex + 1;
    }
    */
    for (auto &index : matchIndex)
    {
        index = 0;
    }
}

void StateMachine::updateNextIndex(int nodeid, int next)
{
    nextIndex[nodeid] = next;
}

void StateMachine::updateMatchIndex(int nodeid, int match)
{
    matchIndex[nodeid] = match;
}

int StateMachine::maxMatch()
{
    int max = -1;
    for (int i = 0; i < matchIndex.size(); i++)
    {
        if (i != nodeId_ && matchIndex[i] > max)
        {
            max = matchIndex[i];
        }
    }
    while (max > 0)
    {
        int count = 0;
        for (int i = 0; i < matchIndex.size(); i++)
        {
            if (i != nodeId_ && matchIndex[i] >= max)
            {
                count++;
            }
        }
        if (count >= nodenum_ / 2)
        {
            return max;
        }
        else
        {
            max--;
        }
    }

    return max;
}

void StateMachine::setCommitIndex(int commit)
{
    commitIndex = commit;
}

void StateMachine::updateCommitIndex()
{
    //FIXME
    int before = commitIndex;
    bool changed = false;
    int max = maxMatch();
    /*
        根据论文5.4.2需要当前Term的日志被大多数提交才能commit
    */
    if (commitIndex < max && logs[max].getTerm() == currentTerm)
    {
        commitIndex = max;
    }

    if (commitIndex != before)
    {
        for (int i = before + 1; i <= commitIndex; i++)
        {
            logs[i].callCallback("commit");
        }
    }
}

void StateMachine::updateTerm(int term)
{
    currentTerm = term;
}

void StateMachine::setVoteFor(int vote)
{
    voteFor = vote;
}

int StateMachine::getVoteFor()
{
    return voteFor;
}

/* 两种场景 */
//1* 尾插日志, lastLogIndex++, 同时需要为log开辟空间
//2* 日志在index被覆盖, 后面的日志全部截断,无需开辟空间
void StateMachine::insertLog(int index, Log log)
{
    int size = logs.size();
    if (index <= size - 1)
    {
        logs[index] = log;
        lastLogIndex = index;
        nextIndex[nodeId_] = lastLogIndex + 1;
        if (lastLogIndex == size - 1)
        {
            logs.push_back(Log());
        }
        //覆盖日志无需开辟空间
    }
    else
    {
        //报错
    }
    
}

int StateMachine::getLastLogIndex()
{
    return lastLogIndex;
}

int StateMachine::getLastLogTerm()
{
    return logs[lastLogIndex].getTerm();
}

bool StateMachine::preIndexMatch(int preIndex, int preTerm)
{
    if (preIndex <= lastLogIndex)
    {
        int term = logs[preIndex].getTerm();
        return term == preTerm;
    }
    else
    {
        return false;
    }
}

int StateMachine::getNextIndex()
{
    return nextIndex[nodeId_];
}

int StateMachine::getNextIndex(int nodeid)
{
    return nextIndex[nodeid];
}

int StateMachine::getPreIndex(int nodeid)
{
    return nextIndex[nodeid] - 1;
}

int StateMachine::getPreTerm(int nodeid)
{
    return logs[getPreIndex(nodeid)].getTerm();
}

void StateMachine::retreatIndex(int nodeid)
{
    nextIndex[nodeid]--;
}

int StateMachine::getCommitIndex()
{
    return commitIndex;
}

Log &StateMachine::getLog(int index)
{
    return logs[index];
}

void StateMachine::showNodeInfo()
{
    printf("-%s- Show Info\n",UnixTime::now().toString().c_str());
    printf(" -commitIndex: %d\n", commitIndex);
    printf(" -lastLogindex %d\n", lastLogIndex);
    printf(" -logsize %ld\n", logs.size());
    for (auto &index : nextIndex)
    {
        printf("%4d", index);
    }
    printf("\n");
    for (auto &index : matchIndex)
    {
        
        printf("%4d", index);
    }
    printf("\n");
}