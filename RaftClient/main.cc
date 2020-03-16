#include "RaftClient.h"
using namespace std;
using namespace net;

int CorrectLogId(vector<NodeInfo> &infos);

int main(void)
{
    vector<NodeInfo> infos;
    getNodeInfo(infos);   
    int logId = CorrectLogId(infos);
    //printf("Current LogId %d\n", logId);
    Mutex mutex;
    Condition cond(mutex);
    Client client(&mutex, &cond, infos);
    client.start();
    while (true)
    {
        printf(">\n");
        string oper = "";
        string key = "";
        string value = "";
        cin >> oper;
        cin >> key;
        if (oper == "set")
            cin >> value;
        if (oper != "get" && oper != "del" && oper != "set" )
        {
            printf("Unkown Command.\n");
            cin.sync();
            continue;
        }
        logId++;
        //printf("Task LogId %d\n", logId);
        ReturnArg arg;
        client.commitRequest(oper, key, value, logId, &arg);
        {
            MutexGuard guard(mutex);
            while (!arg.ret)
            {
                cond.wait();
            }
            if (arg.str == "")
            {
                arg.str = "Key not exist";
            }
            printf("\nResult:\n%s\n\n", arg.str.c_str());
        }
    }
}

int CorrectLogId(vector<NodeInfo> &infos)
{
    while (true)
    {
        for (auto info : infos)
        {
            NetAddr addr;
            int fd = Client::connectPeer(info.getIp(), info.getPort(), &addr);
            if (fd == -1)
            {
                continue;
            }
            else
            {
                char buf[16] = {0};
                int total = 6;
                string type = "GLogId";
                total = htonl(total);
                memcpy(buf, &total, 4);
                memcpy(buf + 4, type.c_str(), 6);
                int ret = write(fd, buf, 10);
                if (ret == -1)
                {
                    perror("write");
                    exit(-1);
                }
                int logId;
                ret = read(fd, &logId, 4);
                logId = ntohl(logId);
                close(fd);
                if (logId == -1)
                {
                    continue;
                }
                else if (logId >= 0)
                {
                    return logId;
                }
            }
        }
    }
}