#include "buffer.h"

// 初始化 vector 容量为传入大小，读写指针都为 0
Buffer::Buffer(int initBufferSize) : buffer_(initBufferSize), readPos_(0), writePos_(0) {}

// 可读数据大小 = 写下标 - 读下标
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

// 可写数据大小 = buffer总大小 - 写下标
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

// 可预留空间：已经读过的就没用了，等于读下标
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

// 返回当前可读数据开始位置指针
const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}

// 确保可写的长度
void Buffer::EnsureWriteable(size_t len) {
    if (len > WritableBytes()) {
        MakeSpace_(len); // 如果可写大小不够就扩容
    }
    assert(WritableBytes() >= len); // 如果条件为假，则崩溃
}

// 移动写下标，在 Append 中使用
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
}

// 读取 len 长度，移动读下标
void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    readPos_ += len;
}

// 读取到 end 位置
void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end);
    Retrieve(end - Peek()); // end指针 - 读指针 = 读取长度
}

// 取出所有数据，buffer 归零，读写下标归零,在别的函数中会用到
void Buffer::RetrieveAll() {
    // bzero(buffer_.data(), buffer_.size()); // 覆盖原本数据
    std::fill(buffer_.begin(), buffer_.end(), 0); // 现代C++写法，安全且可移植
    readPos_ = 0;
    writePos_ = 0;
}

// 取出剩余可读的 str
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes()); // 复制所有剩余可读数据
    RetrieveAll();                            // 清空缓存
    return str;
}

// 返回写指针位置（只读）
const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}

// 返回写指针位置（可写）
char* Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

// 将字符串 str 加入缓冲区
void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length()); // str.data() 返回字符串的首地址
}

// 将长度为 len 的 C 字符串加入缓冲区
void Buffer::Append(const char* str, size_t len) {
    assert(str);                             // 确保指针非空
    EnsureWriteable(len);                    // 确保有 len 字节可写空间，否则扩容
    std::copy(str, str + len, BeginWrite()); // 将数据从 str 拷贝到缓冲区可写位置
    HasWritten(len);                         // 更新写指针 writePos_
}

// 将长度为 len 的任意类型的数据加入缓冲区
void Buffer::Append(const void* data, size_t len) {
    assert(data); // 确保指针非空
    // 统一逻辑，所有数据最终都走 Append(const char*, size_t)
    Append(static_cast<const char*>(data), len); // 转换指针类型为 const char*，调用真正处理字节的函数
}

// // 将另一个 Buffer 内容追加进本 Buffer
void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

// 将 fd 的内容读到缓冲区，即 writable 的位置
ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    char buff[65535];                        // 临时 buff，在栈上分配（局部变量）
    struct iovec iov[2];                     // readv 分散读结构体
    const size_t writable = WritableBytes(); // 先记录能写多少

    // 分散读， 保证数据全部读完
    iov[0].iov_base = BeginWrite(); // Buffer 写指针位置
    iov[0].iov_len = writable;      // Buffer 可写空间大小
    iov[1].iov_base = buff;         // 临时 buff，buff 退化为 char*，可以自动转换为 void*
    iov[1].iov_len = sizeof(buff);  // 临时 buff 大小

    // 从 fd 读取数据，先填 iov[0]，满了就填 iov[1]
    const ssize_t len = readv(fd, iov, 2); // 分散读，读到 Buffer + 临时 buff

    if (len < 0) {
        *saveErrno = errno; // 出错，把 errno 保存
    }
    // 若 len < writable，说明写区可以容纳 len
    else if (static_cast<size_t>(len) <= writable) {
        writePos_ += len; // 数据全放在 buffer 内
    }
    // 部分写在 Buffer 内，剩余写在临时 buff，通过 Append 扩容存入
    else {
        writePos_ = buffer_.size();                        // Buffer 写满，下标移到最后
        Append(buff, static_cast<size_t>(len) - writable); // 剩余数据追加
    }

    return len; // 返回实际读取字节数
}

// 将 buffer 中可读的区域写入 fd 中
ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes();         // 可读字节数
    ssize_t len = write(fd, Peek(), readSize); // 写入 fd
    if (len < 0) {
        *saveErrno = errno; // 出错保存 errno
        return len;
    }
    Retrieve(len); // 更新读指针
    return len;    // 返回实际写入字节数
}

// 返回缓冲区首地址（写操作用）
char* Buffer::BeginPtr_() {
    return buffer_.data();
}

// 返回缓冲区首地址（只读操作）
const char* Buffer::BeginPtr_() const {
    return buffer_.data();
}

// 扩容（或整理数据）
void Buffer::MakeSpace_(size_t len) {
    if (PrependableBytes() + WritableBytes() < len) {
        buffer_.resize(writePos_ + len + 1); // 空间不够 ➜ 直接扩容
    } else {
        size_t readable = ReadableBytes(); // 当前可读取数据大小

        // std::copy(InputBegin, InputEnd, OutputBegin) 范围左闭右开
        // std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());

        // std::memmove(void* dest, const void* src, std::size_t count)
        std::memmove(BeginPtr_(), BeginPtr_() + readPos_, readable); // 数据迁移

        readPos_ = 0;         // 更新当前读指针位置
        writePos_ = readable; // 更新当前写指针位置
    }
}