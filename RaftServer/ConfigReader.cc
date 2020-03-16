#include <string>
#include <fstream>
#include <iostream>
#include "NodeInfo.h"
#include "ConfigReader.h"

using namespace std;
void getNodeInfo(vector<NodeInfo> &nodeinfo)
{
    ifstream ifile;
    ifile.open("config.txt");
    string info = "";
    nodeinfo.clear();
    int nodeid = 0;
    while(getline(ifile, info))
    {
        int pos = info.find(" ");
        string ip = string(info, 0, pos);
        string _port = string(info, pos + 1);
        int port = atoi(_port.c_str());
        nodeinfo.push_back(NodeInfo(nodeid, ip, port));
        nodeid++;
    }
    
    return;
}
