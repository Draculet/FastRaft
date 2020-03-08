#include "../../Server.h"
#include "../../Connection.h"

int connectPeer(string ip, int port, NetAddr *addr);
int main(void)
{
    NetAddr addr;
    int sockfd = connectPeer("127.0.0.1", 9981, &addr);
    if (sockfd == 0)
    {
        printf("Success\n");
    }
    Buffer buf;
    while (true)
    {
        int res = buf.readFd(sockfd);
        printf("Recv %d\n", res);
        string data = buf.retrieveAllAsString();
        data += data;
        data += data;
        write(sockfd, data.c_str(), data.size());
    }
}

int connectPeer(string ip, int port, NetAddr *addr)
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