#ifndef BUFFER_H // 防止重复包含头文件的宏定义开始（类似#pragma once）
#define BUFFER_H // 如果没有定义，就定义宏

#include <iostream>  // 流输出调试可用
#include <vector>    // vector 容器用于存放缓冲区数据
#include <atomic>    // 原子变量，用于多线程安全
#include <cstring>   // 引入 C 字符串处理函数，如 memmove、memcpy、bzero
#include <algorithm> // 使用std::copy()
#include <unistd.h>  // 使用write，Unix 系统底层接口
#include <sys/uio.h> // 使用iovec（readv分散读结构体），用于高效读取 socket
#include <assert.h>  // 断言，调试用，检查条件是否正确

// Buffer 缓冲区类，核心在于管理数据的“读写区域”
class Buffer {
public:
    Buffer(int initBufferSize = 1024); // 构造函数，默认初始容量为 1024 字节
    ~Buffer() = default;               // 默认析构函数（vector 会自动释放）

    size_t WritableBytes() const;    // 返回可写入空间大小
    size_t ReadableBytes() const;    // 返回可读取数据大小
    size_t PrependableBytes() const; // 返回头部“可以重新利用的空白区域”

    const char* Peek() const;         // 返回当前可读数据开始位置指针
    void EnsureWriteable(size_t len); // 确保有足够的可写空间，不够就扩容
    void HasWritten(size_t len);      // 写入完成后更新写入位置

    void Retrieve(size_t len);           // 从缓冲区读取 len 字节，移动 readPos_
    void RetrieveUntil(const char* end); // 读取直到某指针位置

    void RetrieveAll();             // 读取全部数据（清空缓存）
    std::string RetrieveAllToStr(); // 读取所有数据并返回 string

    const char* BeginWriteConst() const; // 返回写指针位置（只读）
    char* BeginWrite();                  // 返回写指针位置（可写）

    void Append(const std::string& str);       // 将字符串加入缓冲区
    void Append(const char* str, size_t len);  // 将 C 字符串加入缓冲区
    void Append(const void* data, size_t len); // 任意类型的数据加入缓冲区
    void Append(const Buffer& buff);           // 将另一个 Buffer 内容追加进本 Buffer

    ssize_t ReadFd(int fd, int* saveErrno);  // 将 fd 的内容读到缓冲区，即 writable 的位置
    ssize_t WriteFd(int fd, int* saveErrno); // 将 buffer 中可读的区域写入 fd 中

private:
    char* BeginPtr_();             // 返回缓冲区首地址（写操作用）
    const char* BeginPtr_() const; // 同上，只读操作
    void MakeSpace_(size_t len);   // 扩容（或整理数据）

private:
    std::vector<char> buffer_;          // 真正存放数据的容器
    std::atomic<std::size_t> readPos_;  // 当前读取位置
    std::atomic<std::size_t> writePos_; // 当前写入位置
};

#endif // BUFFER_H