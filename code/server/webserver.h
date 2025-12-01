#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map> // 用于存储 fd 到 HttpConn 的映射（用户连接）
#include <fcntl.h>       // fcntl()，设置非阻塞
#include <unistd.h>      // close()
#include <assert.h>      // assert 断言
#include <errno.h>       // errno 系统错误码
#include <sys/socket.h>  // socket(), bind(), listen(), accept()
#include <netinet/in.h>  // sockaddr_in 结构（网络地址）
#include <arpa/inet.h>   // htonl/htons，网络字节序转换

#include "epoller.h"             // epoll 封装类
#include "../log/log.h"          // 日志系统
#include "../timer/heaptimer.h"  // 小根堆定时器（用于连接超时）
#include "../pool/sqlconnpool.h" // MySQL 连接池
#include "../pool/threadpool.h"  // 线程池
#include "../pool/sqlconnpool.h" // RAII 管理数据库连接
#include "../http/httpconn.h"    // HTTP 连接处理类

class WebServer {
public:
    // 构造函数: 设置服务器参数　＋　初始化定时器／线程池／反应堆／连接队列
    WebServer(int port, int trigMode, int timeoutMS, bool OptLinger, int sqlPort, const char* sqlUser,
              const char* sqlPwd, const char* dbName, int connPoolNum, int threadNum, bool openLog, int logLevel,
              int logQueSize);

    ~WebServer(); // 析构函数: 关闭listenFd_，　销毁　连接队列/定时器／线程池／反应堆
    void Start(); // 服务器启动（事件循环）

private:
    bool InitSocket_();                        // 初始化监听 socket
    void InitEventMode_(int trigMode);         // 设置 EPOLL 触发模式（ET/LT）
    void AddClient_(int fd, sockaddr_in addr); // 接收新连接并添加到 epoll

    void DealListen_();         // 处理 listening socket（accept）
    void DealWrite_(HttpConn*); // socket 可写
    void DealRead_(HttpConn*);  // socket 可读

    void SendError_(int fd, const char* info); // 发送错误并关闭
    void ExtentTime_(HttpConn* client);        // 延长连接的超时时间
    void CloseConn_(HttpConn* client);         // 关闭一个连接

    void OnRead_(HttpConn* client);   // 读数据（线程执行）
    void OnWrite_(HttpConn* client);  // 写数据（线程执行）
    void OnProcess(HttpConn* client); // 处理 HTTP 请求，生成响应

    static const int MAX_FD = 65536; // 最大支持客户端连接数

    static int SetFdNonblock(int fd); // 设置非阻塞

    int port_;        // 监听端口
    bool openLinger_; // 是否使用优雅关闭（SO_LINGER）
    int timeoutMS_;   // 超时时间（毫秒）
    bool isClose_;    // 服务器是否关闭
    int listenFd_;    // 监听 socket fd
    char* srcDir_;    // 网站资源目录（./resources）

    uint32_t listenEvent_; // epoll 监听 socket 的事件类型
    uint32_t connEvent_;   // epoll 客户端连接的事件类型

    std::unique_ptr<HeapTimer> timer_;        // 小根堆定时器（管理连接超时）
    std::unique_ptr<ThreadPool> threadpool_;  // 线程池
    std::unique_ptr<Epoller> epoller_;        // epoll 封装
    std::unordered_map<int, HttpConn> users_; // fd -> HttpConn 连接
};

#endif // WEBSERVER_H