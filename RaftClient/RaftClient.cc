#include "RaftClient.h"

using namespace std;
using namespace net; 
using namespace base;

Client::Client(Mutex *mutex, Condition *cond, vector<NodeInfo> &infos)
    :threadloop_(new ThreadLoop()),
    mutex_(mutex),
    cond_(cond),
    curNodeId_(0),
    infos_(infos)
{}

Eventloop *Client::getLoop()
{
    return threadloop_->getLoop();
}

void Client::start()
{
    threadloop_->start();    
}

void Client::onRead(Buffer *buf, shared_ptr<Connection> conn)
{
    ClientRes res = ClientRes::fromString(buf);
    //printf("________Recv %s_______\n\n", res.getResp().c_str());
    string flag = res.getFlag();
    if (flag == flag_)
    {
        //printf("Flag Match\n");
        string resp = res.getResp();
        if (resp == "fail")
        {
            //printf("\n\nrecv false\n\n");
            int nodeId = res.getLeaderId();
            if (nodeId > infos_.size())
            {
                nodeId = curNodeId_ + 1;
            }
            curNodeId_ = nodeId;
            string flag = FlagBuilder::getFlag();
            flag_ = flag;
            conn_.reset();
            sendReq(nodeId, logId_, flag, curOper_, curKey_, curValue_);
            getLoop()->runTimeAfter(1, bind(&Client::sendReqDue, this, flag), "retry");
        }
        else
        {
            getLoop()->cancelTime("retry");
            {
                MutexGuard guard(*mutex_);
                arg_->ret = true;
                arg_->str = resp;
                cond_->notify();
            }
            release();
        }
    }
}

void Client::commitRequest(string oper, string key, string value, int logId, ReturnArg *arg)
{
    getLoop()->runInloop(bind(&Client::commitRequestInloop, this, oper, key, value, logId, arg));      
}

void Client::commitRequestInloop(string oper, string key, string value, int logId, ReturnArg *arg)
{
    curOper_ = oper;
    curKey_ = key;
    curValue_ = value;
    logId_ = logId;
    arg_ = arg;
    string flag  = FlagBuilder::getFlag();
    flag_ = flag;
    sendReq(curNodeId_, logId_, flag, curOper_, curKey_, curValue_);
    getLoop()->runTimeAfter(1, bind(&Client::sendReqDue, this, flag), "retry");
}

void Client::sendReqDue(string flag)
{
    if (flag == flag_)
    {
        curNodeId_ = (curNodeId_ + 1) % infos_.size();
        string flag = FlagBuilder::getFlag();
        flag_ = flag;
        conn_.reset();
        sendReq(curNodeId_, logId_, flag_, curOper_, curKey_, curValue_);
        getLoop()->runTimeAfter(1, bind(&Client::sendReqDue, this, flag), "retry");
    }
}

void Client::sendReq(int nodeId, int logId, string flag, string oper, string key, string value)
{
    shared_ptr<Connection> conn = getConn(nodeId);
    ClientReqArg cliarg(logId, flag, oper, key, value);
    //num = ntohl(num);
    //cout << "argsize: " << num << endl << endl << endl;
    conn->send(cliarg.toString());
}

void Client::onClose()
{
    conn_.reset();
}

shared_ptr<Connection> Client::getConn(int nodeId)
{
    while (!conn_)
    {
        //printf("______nodeId: %d________\n", nodeId);
        string ip = infos_[nodeId].getIp();
        int port = infos_[nodeId].getPort();
        NetAddr addr;
        int fd = connectPeer(ip, port, &addr);
        if (fd == -1)
        {
            //printf("Connect Failed\n\n");
            nodeId = (nodeId + 1) % infos_.size();
        }
        else
        {
            //printf("Connect Success\n\n");
            shared_ptr<Connection> conn(new Connection(fd, addr, getLoop()));
            conn_ = conn;
            conn->setReadCallback( bind(&Client::onRead, this, placeholders::_1, placeholders::_2) );
            //conn->setCloseCallback( bind(&Client::onClose, this, placeholders::_1) );
            conn->handleEstablish();
        }
    }

    return conn_;
}

int Client::connectPeer(string ip, int port, NetAddr *addr)
{
    struct sockaddr_in servaddr;
    int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd < 0)
    {
        return -1;
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr);
    int ret = ::connect(clientFd, (struct sockaddr *)(&servaddr), sizeof(servaddr));
    if (ret != 0)
    {
        close(clientFd);
        return -1;
    }
    *addr = NetAddr(servaddr);
    return clientFd;
}

void Client::release()
{
    flag_ = "";
    curKey_ = "";
    curValue_ = "";
    curOper_ = "";
    logId_ = -1;
    arg_ = nullptr;
}
