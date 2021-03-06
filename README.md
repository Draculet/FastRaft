# FastRaft

FastRaft是基于FastNet网络库,根据Raft论文实现的简陋分布式KV,暂时只支持get/set/del操作, 目前未实现快照,未考虑集群成员变化和KV的持久化存储.

本仓库包含：

1. 维护节点一致性的服务端程序 RaftServer
2. 提交set/get/del操作的客户端程序 RaftClient
3. 依赖的网络库 FastNet

## 导航

- [介绍](#介绍)
- [安装](#安装)
- [使用](#使用)

## 介绍

本项目是个人在阅读完Raft论文和相关参考资料后，参照论文实现的一个较完整Demo。借助FastNet的事件驱动模型，RPC的调用和RPC返回值的获取都是异步的过程，且由于使用的是多线程事件驱动模型，相比同步调用，效率和吞吐量要提高很多。项目在C++11标准下构建，大量使用智能指针管理对象生命周期，经过大量测试，已排除了绝大部分的内存泄漏和对象不正常析构问题。项目目前处于非常简陋和粗糙的初期构建阶段，未来可能会继续完善或者抽取复用成为独立的库，非常欢迎你阅读并留下意见，更欢迎你的加入


## 安装

服务端安装
```sh
$ cd RaftServer
$ cmake .
$ make
```

客户端安装
```sh
$ cd RaftClient
$ cmake .
$ make
```

## 使用

为了便于本地测试，使用 config.txt 文本文件配置节点ip和端口，使用者需要在运行服务端时指明自身节点的节点号，节点号按照 config.txt 中的配置从0开始编号，另外使用者需要自行保证节点号不重复
```sh
$ ./main [nodeid]
```

客户端使用的config.txt需要与服务端保持一致，安装完成后即可直接运行
```sh
$ ./main
```


## 待完善
* 编写FastNet的日志库，取代目前低效的日志打印方式
* 完善Raft算法的实现,加入快照支持
* 多客户端日志提交存在问题
* 在保证线程安全的情况下解决目前临界区较大的问题
* ...