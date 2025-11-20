# Buffer 类详解

## 1.Buffer 是干什么的？
它是你在处理 HTTP 数据时的“暂存区”：  
* 网络数据  → 存入 Buffer → 解析 HTTP 请求  
* 服务器响应 → 写入 Buffer → 发送到 socket

## 2.为什么不直接用 char[] 而要用这个类？
* 动态扩容
* 支持高效网络 IO（使用 `readv`，减少拷贝）
* 线程安全（使用 `atomic`）
* 易管理读写位置
* 灵活添加、读取、清理数据

## 3.为什么要用 atomic？
多线程同时读写（每个连接一个线程/事件），指针要保证同步

## 4.为什么用 readv 而不是 read？
`readv` 能将数据“分散”写入多个 buffer，减少一次 IO 读不完的问题

## 5.PrependableBytes() 有什么用？
读完的数据区域可以重新利用，避免频繁扩容

## 6.为什么不直接清空 buffer？
每次清空开销大，应该复用空间

## 7.size_t 和 ssize_t
| 类型    | 含义                               | 是否有符号 | 常用在哪                   |
|---------|------------------------------------|------------|----------------------------|
| `size_t`  | 大小（memory大小、容器长度）       | 无符号     | `vector.size()` / `sizeof`     |
| `ssize_t` | 数据读写长度（可表示失败）         | 有符号     | `read()` / `write()` 函数返回值 |

## 8.MakeSpace_() 中的 copy() 与 memmove()
| 使用方法                                       | 头文件      |
|------------------------------------------------|-------------|
| `std::copy(InputBegin, InputEnd, OutputBegin)` | `<algorithm>` |
| `std::memmove(void* dest, const void* src, std::size_t count)` | `<cstring>` |
* `copy()` 会将 `[InputBegin, InputEnd)` 范围的数据复制到从 `OutputBegin` 开始的区域。
* 但 `copy()` 不能可靠处理重叠区域。当复制区域重叠时，`std::copy` 行为未定义（UB）。
* 建议使用 `memmove()`（专门处理内存重叠情况）。

## 9.&buffer_[0] 和 &*buffer_.begin() 和 buffer_.data()（std::vector::data()）
都是获取buffer_首地址
* &buffer_[0]：当 buffer_ 为空（size() == 0）时，访问 buffer_[0] 是 未定义行为（UB）
* &*buffer_.begin()：vector 非空时可用，功能等价于 &buffer_[0]。为空时，解引用 begin() 是 UB
* buffer_.data()：C++11 标准正式提供，即使 buffer_ 为空，也返回一个合法的指针（可以安全地做指针算术，但不能解引用）

## 10.assert(condition)
`assert` 是一个 断言（调试工具）  
它不会返回值，只在条件为假时触发程序异常终止（通常是崩溃）  
用来检查程序员预期的条件是否成立  
`assert` 这个宏只在 Debug 模式生效，在 Release 模式默认会被编译器移除掉（优化掉），因此不会影响效率

## 11.ReadFd 与 WriteFd
| ReadFd 设计思想                               | WriteFd 设计思想                     |
|------------------------------------------------|--------------------------------------|
| 避免一次读不完就丢数据                         | 简单高效：每次只写可读区域           |
| 减少内存拷贝                                   | 支持 partial write（写不完也能继续写） |
| 支持大请求（HTTP POST 上传大文件）             | 与 ReadFd 形成完整的 socket 缓冲读写机制 |

## 12.readv
`ssize_t readv(int fd, const struct iovec *iov, int iovcnt)`  
作用：从 fd 读取数据，并把数据依次写入 iov 数组指定的多个 buffer  
参数：
* `fd`：文件描述符（socket/file）
* `iov`：struct iovec 数组，每个元素是一个 buffer
* `iovcnt`：数组长度  
返回值：实际读取的字节数（可能小于总容量）  
优势：一次系统调用，可以把数据分散写入多个 buffer，减少内存拷贝和循环读取

## 13.write
`ssize_t write(int fd, const void* buf, size_t count)`  
作用：将内存 buf 中的 count 字节写入文件描述符 fd（可以是 socket、文件等）  
返回值：实际写入的字节数（可能小于 count）  
注意：
* 在 socket 或非阻塞 IO 下，可能只写一部分，需要循环写
* 出错返回 -1，并设置 errno

## 14.struct iovec
```
struct iovec {
    void  *iov_base;    // buffer 的首地址
    size_t iov_len;     // buffer 的长度（字节数）
};
```
用于分散/聚合IO：
* `readv`  → 分散读（scatter）
* `writev` → 聚合写（gather）  
好处：
* 不用自己在循环里分段拷贝
* 可以把不同数据结构直接映射到不同 buffer
* 高性能网络服务器常用
* 避免多次系统调用、避免多次内存拷贝、避免内存溢出风险

## 15.本Buffer类设计潜在问题
在大多数普通 HTTP 请求场景下是 OK 的，但对于大文件传输、高并发持续流量就可能导致内存无限增长。  

常见做法：
| 方法                         |           优缺点                      |
|-----------------------------|---------------------------------------|
| 设置最大容量                   |       简单，但需要处理上限溢出           |
| 缩容 / 回收                   |       保持灵活性，但会增加拷贝开销        |
| 环形缓冲区                     |      高性能内存稳定，但实现复杂          |

* 方法 A：设置最大容量  
限制 Buffer 最大容量，超过则返回错误或丢弃数据  
优点：内存不会无限增长  
缺点：需要处理超过上限的情况（丢包/返回错误）
* 方法 B：定期缩容 / 回收  
当 Buffer 使用量远小于容量时，主动缩小容量，可在每次 RetrieveAll 后调用这样可以释放大块内存，避免长期占用
* 方法 C：循环缓冲区（Ring Buffer）  
使用环形缓冲区，不断复用已经读取的空间。当写到末尾时，回到头部继续写  
优点：减少搬移和 resize、内存占用稳定  
实现复杂一些，需要处理读写指针 wrap-around