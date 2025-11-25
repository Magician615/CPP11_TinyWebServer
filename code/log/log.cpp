#include "log.h"

/*
    构造函数初始化成员变量
    构造函数不会创建日志文件，也不会开启线程，
    只是把变量初始化为空或默认值。
*/
Log::Log() {
    lineCount_ = 0;         // 当前日志写入的总行数
    toDay_ = 0;             // 记录日志文件的日期（用于自动按天切换）
    isAsync_ = false;       // 默认为同步模式
    fp_ = nullptr;          // 日志文件指针
    deque_ = nullptr;       // 阻塞队列，用于异步存储日志
    writeThread_ = nullptr; // 异步写日志的线程指针
}

/*
    析构函数（关闭文件和线程）
    这里要确保程序退出前把剩余日志写完
*/
Log::~Log() {
    // 如果是异步模式并且线程还在运行
    if (writeThread_ && writeThread_->joinable()) {
        // 先把队列中的日志写完
        while (!deque_->empty()) {
            deque_->flush(); // 通知 pop() 继续处理
        }
        deque_->Close();      // 关闭队列，避免阻塞
        writeThread_->join(); // 等待线程退出
    }
    // 关闭文件
    if (fp_) {
        std::lock_guard<std::mutex> locker(mtx_);
        flush();     // 刷新缓冲区
        fclose(fp_); // 关闭文件
    }
}

// 获取日志等级
int Log::GetLevel() {
    std::lock_guard<std::mutex> locker(mtx_);
    return level_;
}

// 设置日志等级
void Log::SetLevel(int level) {
    std::lock_guard<std::mutex> locker(mtx_);
    level_ = level;
}

// 判断日志系统是否已经初始化
bool Log::IsOpen() {
    return isOpen_;
}

// 单例模式获取全局唯一日志对象
Log* Log::Instance() {
    static Log log;
    return &log;
}

// 唤醒阻塞队列消费者，开始写日志（强制刷新（同步时直接 fflush，异步时唤醒 pop））
void Log::flush() {
    if (isAsync_) { // 只有异步日志才会用到deque
        deque_->flush();
    }
    fflush(fp_); // 清空输入缓冲区
}

// 线程入口函数（静态）(异步写日志公有方法，调用私有方法AsyncWrite)
void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}

// 异步写日志线程函数（一直从队列中取日志写入文件）
void Log::AsyncWrite_() {
    std::string str = "";
    while (deque_->pop(str)) {
        std::lock_guard<std::mutex> locker(mtx_);
        fputs(str.c_str(), fp_);
    }
}

// 初始化日志系统(日志等级,日志保存路径,文件后缀,阻塞队列容量（>0 说明异步写入）)
void Log::init(int level, const char* path, const char* suffix, int maxQueueCapacity) {
    isOpen_ = true; // 设置日志系统打开
    level_ = level;
    path_ = path;
    suffix_ = suffix;

    // 如果队列大小 > 0，则启用异步日志
    if (maxQueueCapacity > 0) {
        isAsync_ = true;
        if (!deque_) { // 为空则创建一个（只创建一次）
            std::unique_ptr<BlockDeque<std::string>> newQue(new BlockDeque<std::string>);
            // 因为unique_ptr不支持普通的拷贝或赋值操作,所以采用move
            // 将动态申请的内存权给deque_，newDeque被释放
            deque_ = std::move(newQue); // 左值变右值,掏空newDeque

            std::unique_ptr<std::thread> newThread(new std::thread(FlushLogThread));
            writeThread_ = std::move(newThread);
        }
    } else {
        isAsync_ = false;
    }

    lineCount_ = 0;

    // 获取当前时间
    time_t timer = time(nullptr);
    struct tm* systime = localtime(&timer);
    // 日志文件名形如: /path/2025_11_24.log
    char fileName[LOG_NAME_LEN] = {0};
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", path_, systime->tm_year + 1900, systime->tm_mon + 1,
             systime->tm_mday, suffix_);
    toDay_ = systime->tm_mday;

    { // 文件创建操作加锁
        std::lock_guard<std::mutex> locker(mtx_);
        buff_.RetrieveAll(); // 清空 buffer
        if (fp_) {           // 重新打开
            flush();
            fclose(fp_);
        }
        fp_ = fopen(fileName, "a"); // 以追加方式打开文件
        if (fp_ == nullptr) {
            mkdir(fileName, 0777); // 创建目录（最大权限）
            fp_ = fopen(fileName, "a");
        }
        assert(fp_ != nullptr);
    }
}

// 写日志（支持同步/异步）
void Log::write(int level, const char* format, ...) {
    struct timeval now = {0, 0};           // 声明并初始化一个 timeval 结构，用于取得当前时间（秒 + 微秒）
    gettimeofday(&now, nullptr);           // 系统调用，填充 now.tv_sec 和 now.tv_usec
    time_t tSec = now.tv_sec;              // 将秒提取到 time_t（用于 localtime）
    struct tm* sysTime = localtime(&tSec); // 把秒转换为本地时间结构（注意：localtime 不是线程安全）
    struct tm t = *sysTime;                // 复制一份 tm 到局部变量，避免后续被覆盖
    va_list vaList;                        // 可变参数列表对象，用于读取 ... 的参数

    // 如果日期变更，或超过每个文件最大行数（默认 50000） → 重新建文件
    if (toDay_ != t.tm_mday || lineCount_ && (lineCount_ % MAX_LINES == 0)) {
        std::unique_lock<std::mutex> locker(mtx_); // 获取互斥锁（unique_lock 可手动 unlock）
        locker.unlock();                           // 立即释放锁 —— 这里意在不持锁进行耗时操作（但语义上需要注意）

        char newFile[LOG_NAME_LEN]; // 用于新文件名
        char tail[36] = {0};        // 用于日期字符串 YYYY_MM_DD
        // 生成日期后缀字符串
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        // 时间不匹配，则替换为最新的日志文件名
        if (toDay_ != t.tm_mday) {
            // 如果是新的一天，按 yyyy_mm_dd.log 命名
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            toDay_ = t.tm_mday; // 更新当前日志日期
            lineCount_ = 0;     // 新文件行计数重置
        } else {
            // 不是新的一天但达到每文件最大行数，按 yyyy_mm_dd-N.log 命名分割
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lineCount_ / MAX_LINES), suffix_);
        }

        locker.lock();             // 重新加锁保护文件切换操作
        flush();                   // 刷新当前文件缓存（如果异步则唤醒写线程）
        fclose(fp_);               // 关闭旧文件指针
        fp_ = fopen(newFile, "a"); // 打开新文件用于追加写
        assert(fp_ != nullptr);    // 确保文件成功打开（发生异常程序会 abort）
    }

    // 在buffer内生成一条对应的日志信息（正式写日志）
    {
        std::unique_lock<std::mutex> locker(mtx_); // 加锁，保护以下对共享资源的访问（buff_, fp_, lineCount_ 等）
        lineCount_++;                              // 行数计数递增

        // 写入时间戳到缓冲区的可写位置（buff_.BeginWrite() 返回 char*）
        int n = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ", t.tm_year + 1900, t.tm_mon + 1,
                         t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);

        buff_.HasWritten(n);         // 通知 Buffer 已写入 n 字节
        AppendLogLevelTitle_(level); // 在 Buffer 中追加日志级别前缀（如 [info]）

        // 处理可变参数 format、...：把格式化后的字符串写到 Buffer 可写区
        va_start(vaList, format); // 初始化 va_list，开始读取可变参数
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList);
        va_end(vaList);          // 清理 va_list
        buff_.HasWritten(m);     // 更新写指针，说明刚写入了 m 字节
        buff_.Append("\n\0", 2); // 在日志末尾追加换行（包含 '\0' 以确保字符串结束）

        // 如果是异步模式并且队列存在且未满，把整行日志字符串入队（异步写）
        if (isAsync_ && deque_ && !deque_->full()) {
            deque_->push_back(buff_.RetrieveAllToStr()); // 取出缓冲区全部数据作为一个 string 并入队
        }
        // 否则直接同步写入文件
        else {
            fputs(buff_.Peek(), fp_); // 同步写入（直接写文件）
        }
        buff_.RetrieveAll(); // 清空 Buffer（读指针回退，写指针归零）
    }
}

// 写日志等级前缀
void Log::AppendLogLevelTitle_(int level) {
    switch (level) {
        case 0: buff_.Append("[debug]: ", 9); break;
        case 1: buff_.Append("[info] : ", 9); break;
        case 2: buff_.Append("[warn] : ", 9); break;
        case 3: buff_.Append("[error]: ", 9); break;
        default: buff_.Append("[info] : ", 9); break;
    }
}
