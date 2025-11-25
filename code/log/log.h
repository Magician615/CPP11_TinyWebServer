#ifndef LOG_H
#define LOG_H

#include <mutex>              // 用于 std::mutex 锁，保证线程安全
#include <string>             // 字符串支持
#include <thread>             // std::thread 用于异步写日志线程
#include <sys/time.h>         // 用于获取精确时间（秒+微秒）
#include <string.h>           // C 风格字符串处理函数
#include <stdarg.h>           // 可变参数（例如 printf 形式）
#include <assert.h>           // 断言，用于检查程序错误
#include <sys/stat.h>         // mkdir 创建目录
#include "blockqueue.h"       // 阻塞队列用于异步写日志
#include "../buffer/buffer.h" // 自定义 Buffer 类，用来临时构建日志内容

class Log {
public:
    // 初始化日志系统(日志等级,日志保存路径,文件后缀,阻塞队列容量（>0 说明异步写入）)
    void init(int level = 1, const char* path = "./log", const char* suffix = ".log", int maxQueueCapacity = 1024);
    static Log* Instance();       // 单例设计模式：全局只会有一个 Log 对象
    static void FlushLogThread(); // 异步写日志公有方法，调用私有方法AsyncWrite

    void write(int level, const char* format, ...); // 写日志（支持同步/异步）
    void flush();                                   // 冲刷缓冲区（将内容立刻写入文件）

    int GetLevel();           // 获取日志等级
    void SetLevel(int level); // 设置日志等级
    bool IsOpen();            // 判断日志系统是否已经初始化

private:
    Log();                                // 构造函数（设为 private，防止外部构造，单例模式）
    virtual ~Log();                       // 析构函数（关闭文件和线程）
    void AppendLogLevelTitle_(int level); // 写日志等级前缀
    void AsyncWrite_();                   // 异步写日志方法

private:
    static const int LOG_PATH_LEN = 256; // 日志路径最大长度
    static const int LOG_NAME_LEN = 256; // 日志文件名最大长度
    static const int MAX_LINES = 50000;  // 单个日志文件最多多少行

    const char* path_;   // 路径名
    const char* suffix_; // 后缀名

    int MAX_LINES_; // 实际使用的最大日志行，通常等于 MAX_LINES
    int lineCount_; // 当前已写日志行数
    int toDay_;     // 当前日期，用于跨天分文件

    bool isOpen_;

    Buffer buff_;  // 临时存储日志内容的 Buffer
    int level_;    // 日志等级
    bool isAsync_; // 是否开启异步日志

    FILE* fp_;                                       // 打开log的文件指针
    std::unique_ptr<BlockDeque<std::string>> deque_; // 阻塞队列
    std::unique_ptr<std::thread> writeThread_;       // 写线程的指针
    std::mutex mtx_;                                 // 同步日志必需的互斥量
};

#define LOG_BASE(level, format, ...)                                                                                   \
    do {                                                                                                               \
        Log* log = Log::Instance();                                                                                    \
        if (log->IsOpen() && log->GetLevel() <= level) {                                                               \
            log->write(level, format, ##__VA_ARGS__);                                                                  \
            log->flush();                                                                                              \
        }                                                                                                              \
    } while (0);

// 四个宏定义，主要用于不同类型的日志输出，也是外部使用日志的接口
// ...表示可变参数，__VA_ARGS__就是将...的值复制到这里
// 前面加上##的作用是：当可变参数的个数为0时，这里的##可以把把前面多余的","去掉,否则会编译出错。
#define LOG_DEBUG(format, ...)                                                                                         \
    do {                                                                                                               \
        LOG_BASE(0, format, ##__VA_ARGS__)                                                                             \
    } while (0);
#define LOG_INFO(format, ...)                                                                                          \
    do {                                                                                                               \
        LOG_BASE(1, format, ##__VA_ARGS__)                                                                             \
    } while (0);
#define LOG_WARN(format, ...)                                                                                          \
    do {                                                                                                               \
        LOG_BASE(2, format, ##__VA_ARGS__)                                                                             \
    } while (0);
#define LOG_ERROR(format, ...)                                                                                         \
    do {                                                                                                               \
        LOG_BASE(3, format, ##__VA_ARGS__)                                                                             \
    } while (0);

#endif // LOG_H