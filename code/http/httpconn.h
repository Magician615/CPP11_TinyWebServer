#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>   // readv / writev 函数，用于分散/聚集IO
#include <arpa/inet.h> // sockaddr_in，inet_ntoa 等网络相关函数
#include <stdlib.h>    // atoi() 字符串转数字
#include <errno.h>     // errno，用于错误码处理

#include "../log/log.h"          // 日志模块
#include "../pool/sqlconnpool.h" // MySQL连接池 RAII 管理
#include "../buffer/buffer.h"    // 自定义缓冲区类
#include "httprequest.h"         // HTTP 请求处理类
#include "httpresponse.h"        // HTTP 响应处理类

class HttpConn {
public:
    // 构造函数 —— 初始化内部变量（fd = -1，连接关闭）
    HttpConn();

    // 析构函数 —— 调用 Close() 关闭连接，释放资源
    ~HttpConn();

    // 初始化 HTTP 连接
    // 参数：客户端 socket fd 和 IP/端口信息
    void init(int sockFd, const sockaddr_in& addr);

    // 读客户端数据（非阻塞读）
    // saveErrno 用于保存错误码
    ssize_t read(int* saveErrno);

    // 向客户端写数据（使用 writev 分段发送响应头和文件内容）
    ssize_t write(int* saveErrno);

    // 主动关闭连接（关闭文件描述符、取消映射、减少用户计数）
    void Close();

    // 获得当前 socket 文件描述符
    int GetFd() const;

    // 获取客户端端口（注意：此端口为网络字节序）
    int GetPort() const;

    // 获取客户端 IP 字符串（格式如 "127.0.0.1"）
    const char* GetIP() const;

    // 获取客户端地址结构体
    sockaddr_in GetAddr() const;

    // 处理HTTP请求 —— 解析请求 + 生成响应
    bool process();

    // 获取剩余待写数据（iov_两个缓冲区加起来）
    int ToWriteBytes() {
        return iov_[0].iov_len + iov_[1].iov_len;
    }

    // 是否开启长连接（keep-alive）
    // 取决于请求报文中 Connection 头字段
    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }

    // static 静态成员 —— 所有连接共享
    static bool isET;                  // 是否为 ET 模式（边缘触发）
    static const char* srcDir;         // 网站访问根目录
    static std::atomic<int> userCount; // 当前在线连接数（原子类型，保证线程安全）

private:
    int fd_;           // 连接套接字（唯一标识客户端）
    sockaddr_in addr_; // 客户端 IP + 端口地址结构

    bool isClose_; // 是否已关闭（true表示连接关闭）

    int iovCnt_;          // writev 使用的 iovec 数量（最多2个）
    struct iovec iov_[2]; // iovec 数组（iov[0] 保存响应头，iov[1] 保存文件内容）

    Buffer readBuff_;  // 读缓冲区（用于接收客户端的请求数据）
    Buffer writeBuff_; // 写缓冲区（用于存放 HTTP 响应头）

    HttpRequest request_;   // HTTP 请求解析对象
    HttpResponse response_; // HTTP 响应构建对象
};

#endif // HTTP_CONN_H
