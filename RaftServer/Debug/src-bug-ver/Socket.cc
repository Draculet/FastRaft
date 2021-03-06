#include "../include/Socket.h"
using namespace std;
using namespace net;
using namespace base;

Socket::Socket(int fd, NetAddr addr):
    fd_(fd),
    addr_(addr)
{

}

Socket::Socket(Socket &&tmp)
{
    fd_ = tmp.fd_;
    addr_ = tmp.addr_;
}

Socket::Socket(int port)
{
    createNonBlockAndBind(port);
}

Socket::Socket(string ip, int port)
{
    createNonBlockAndBind(ip, port);
}

Socket::~Socket()
{
    ::close(fd_);
    //printf("*debug* socket close\n");
}

void Socket::setNonBlock()
{
    int flags = ::fcntl(fd_, F_GETFL, 0);
    flags |= O_NONBLOCK;
    int ret = ::fcntl(fd_, F_SETFL, flags);
    if (ret < 0)
    {
        perror("setNonBlock");
    }
    //TODO 判设置错误,日志输出
}

void Socket::setCloseExec()
{
    int flags = ::fcntl(fd_, F_GETFD, 0);
    flags |= FD_CLOEXEC;
    int ret = ::fcntl(fd_, F_SETFD, flags);
    if (ret < 0)
    {
        perror("setCloseExec");
    }
    //TODO 判设置错误,日志输出
}

void Socket::createNonBlockAndBind(int port)
{
    fd_ = ::socket(PF_INET, SOCK_STREAM, 0);
    setReuseAddr(true);
    setReusePort(true);
    setNonBlock();
    if (fd_ < 0)
    {
        perror("createNonBlockAndBind");
    }
    sockaddr_in servaddr;
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    int ret = ::bind(fd_, (sockaddr *)&servaddr, sizeof(servaddr));
    if (ret < 0)
    {
        perror("bind");
        exit(-1);
    }
    addr_ = servaddr;
    //TODO 判创建失败错,日志输出
}

void Socket::createNonBlockAndBind(string ip, int port)
{
    fd_ = ::socket(PF_INET, SOCK_STREAM, 0);
    setReuseAddr(true);
    setReusePort(true);
    setNonBlock();
    if (fd_ < 0)
    {
        perror("createNonBlockAndBind");
    }
    sockaddr_in servaddr;
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    int ret = inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr);
    if (ret < 0)
    {
        perror("inet_pton");
    }   
    ret = ::bind(fd_, (sockaddr *)&servaddr, sizeof(servaddr));
    addr_ = servaddr;
    if (ret < 0)
    {
        perror("bind");
        exit(-1);
    }
    //TODO 判创建失败错,日志输出
}

int Socket::accept(NetAddr *addr)
{
    sockaddr_in cli_addr;
    socklen_t len = sizeof(cli_addr);
    int connfd = ::accept4(fd_, (sockaddr *)&cli_addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd < 0)
    {
        //非阻塞accept,只输出错误信息
        perror("accept4");
        //TODO 判创建失败错,日志输出
        return -1;
    }
    addr->setAddr(cli_addr);
    return connfd;
}

void Socket::listen(int lsize)
{
    int ret = ::listen(fd_, lsize);
    if (ret < 0)
    {
        perror("listen");
    }
    //TODO 判监听失败,日志输出
}

void Socket::shutdownWR()
{
    //shutdown默认只关闭写端,防止有信息未接受
    int ret = ::shutdown(fd_, SHUT_WR);
    if (ret < 0)
    {
        perror("shutdown");
    }
    //TODO 日志输出
}

void Socket::setNoDelay(bool on)
{
    int optval = on ? 1 : 0;
    int ret = ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
    if (ret < 0)
    {
        perror("setNodelay");
    }
}

void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    int ret = ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (ret < 0)
    {
        perror("setReuseAddr");
    }
}

void Socket::setReusePort(bool on)
{
    int optval = on ? 1 : 0;
    int ret = ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    if (ret < 0)
    {
        perror("reuseport");
    }
}

void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
    int ret = ::setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    if (ret < 0)
    {
        perror("keepalive");
    }
}

NetAddr *Socket::getAddr()
{
    return &addr_;
}

int Socket::getFd()
{
    return fd_;
}