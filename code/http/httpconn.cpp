#include "httpconn.h"

// 静态成员变量初始化
const char* HttpConn::srcDir;         // 网站资源根目录，例如 ./resources
std::atomic<int> HttpConn::userCount; // 当前连接数（多线程环境下必须 atomic）
bool HttpConn::isET;                  // 是否使用 Epoll ET（边缘触发）模式

HttpConn::HttpConn() {
    fd_ = -1;        // 默认无文件描述符
    addr_ = {0};     // 初始化 IP 地址结构体
    isClose_ = true; // 默认连接关闭状态
}

HttpConn::~HttpConn() {
    Close(); // 析构时关闭连接（防止资源泄露）
}

void HttpConn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0);           // 断言 fd 合法
    userCount++;              // 连接数 +1（atomic，线程安全）
    addr_ = addr;             // 保存客户端地址信息
    fd_ = fd;                 // 保存客户端连接fd
    writeBuff_.RetrieveAll(); // 清空发送缓冲区
    readBuff_.RetrieveAll();  // 清空接收缓冲区
    isClose_ = false;         // 标记连接处于开启状态
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
}

void HttpConn::Close() {
    response_.UnmapFile();   // 解绑文件映射（mmap）
    if (isClose_ == false) { // 若当前连接仍然开启
        isClose_ = true;
        userCount--; // 连接数 -1
        close(fd_);  // 关闭 socket fd
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}

int HttpConn::GetFd() const {
    return fd_; // 返回 socket fd
}

struct sockaddr_in HttpConn::GetAddr() const {
    return addr_; // 返回连接客户端 IP 信息
}

const char* HttpConn::GetIP() const {
    return inet_ntoa(addr_.sin_addr); // 返回客户端 IP 字符串
}

int HttpConn::GetPort() const {
    return addr_.sin_port; // 端口号（注意是网络字节序）
}

ssize_t HttpConn::read(int* saveErrno) {
    ssize_t len = -1;
    do {
        // 使用 Buffer::ReadFd 读取数据（内部使用 read()）
        len = readBuff_.ReadFd(fd_, saveErrno);
        if (len <= 0) {
            break; // 读取失败或结束
        }
    } while (isET); // 如果是 ET 模式，需要循环读取直到无数据
    return len;
}

ssize_t HttpConn::write(int* saveErrno) {
    ssize_t len = -1;
    do {
        // writev 一次发送多个缓冲区（响应头 + 文件内容）
        len = writev(fd_, iov_, iovCnt_);
        if (len <= 0) {         // 发送失败
            *saveErrno = errno; // 保存错误码
            break;
        }

        // 如果响应头和文件都已发送完
        if (iov_[0].iov_len + iov_[1].iov_len == 0) {
            break;
        }
        // 如果发送量超过 iov_[0]（即响应头部分）
        else if (static_cast<size_t>(len) > iov_[0].iov_len) {
            // 调整文件内容指针位置，扣掉已发部分
            iov_[1].iov_base = (uint8_t*)iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);

            // 如果头部还存在数据（理论上不会）
            if (iov_[0].iov_len) {
                writeBuff_.RetrieveAll(); // 清空发送缓冲区
                iov_[0].iov_len = 0;      // 设置头部长度为0
            }
        } else {
            // 如果只发送了头部的一部分
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len; // 移动指针
            iov_[0].iov_len -= len;                              // 更新剩余数据
            writeBuff_.Retrieve(len);                            // 消费缓冲区部分数据
        }
    } while (isET || ToWriteBytes() > 10240); // ET 模式或数据量较大则继续发送

    return len;
}

bool HttpConn::process() {
    request_.Init(); // 初始化请求解析对象

    // 无请求数据直接返回 false
    if (readBuff_.ReadableBytes() <= 0) {
        return false;
    }
    // 解析 HTTP 请求成功
    else if (request_.parse(readBuff_)) {
        LOG_DEBUG("%s", request_.path().c_str());
        // 若解析正常，返回 200 OK
        response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);
    }
    // 解析失败返回 400 错误
    else {
        response_.Init(srcDir, request_.path(), false, 400);
    }

    response_.MakeResponse(writeBuff_); // 生成响应报文（写入 writeBuff_）

    /* 设置 iov[0] —— 响应头 */
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1; // 目前只有 header

    /* 设置 iov[1] —— 文件内容（若存在） */
    if (response_.FileLen() > 0 && response_.File()) {
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2; // 发送两段内容
    }

    LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen(), iovCnt_, ToWriteBytes());
    return true;
}
