#include "sqlconnpool.h"

// 构造：初始化使用计数
SqlConnPool::SqlConnPool() {
    useCount_ = 0;
    freeCount_ = 0;
}

// 单例实现：函数内静态对象保证线程安全（C++11 起）
SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool pool;
    return &pool;
}

// 初始化连接池：创建 connSize 个 MYSQL* 连接并放入队列
void SqlConnPool::Init(const char* host, int port, const char* user, const char* pwd, const char* dbName,
                       int connSize) {
    assert(connSize > 0);
    for (int i = 0; i < connSize; ++i) {
        MYSQL* conn = nullptr;
        conn = mysql_init(conn); // 初始化 MYSQL 对象
        if (!conn) {
            LOG_WARN("MySql init error!");
            assert(conn);
        }
        // 连接到数据库
        conn = mysql_real_connect(conn, host, user, pwd, dbName, port, nullptr, 0);
        if (!conn) {
            LOG_ERROR("MySql Connect error!");
        }
        connQue_.push(conn); // 将连接放入队列（无加锁：Init 预计只在单线程环境调用）
    }
    MAX_CONN_ = connSize;
    sem_init(&semId_, 0, MAX_CONN_);
}

// 从连接池中获取一个连接（如果没有则阻塞），返回 MYSQL*
MYSQL* SqlConnPool::GetConn() {
    MYSQL* conn = nullptr;
    // 如果队列空则直接返回 nullptr（非阻塞设计）
    if (connQue_.empty()) {
        LOG_WARN("SqlConnPool busy");
        return nullptr;
    }
    sem_wait(&semId_); // P 操作：等待信号量（如果为 0 阻塞）
    {
        std::lock_guard<std::mutex> locker(mtx_);
        conn = connQue_.front(); // 取队首
        connQue_.pop();          // 弹出
    }
    return conn;
}

// 释放连接回池
void SqlConnPool::FreeConn(MYSQL* conn) {
    assert(conn);
    std::lock_guard<std::mutex> locker(mtx_);
    connQue_.push(conn); // 放回队列
    sem_post(&semId_);   // V 操作：释放一个信号量，唤醒等待者
}

// 返回当前空闲连接数量（线程安全）
int SqlConnPool::GetFreeConnCount() {
    std::lock_guard<std::mutex> locker(mtx_);
    return connQue_.size();
}

// 关闭所有连接并清理 MySQL 客户端库
void SqlConnPool::ClosePool() {
    std::lock_guard<std::mutex> locker(mtx_);
    while (!connQue_.empty()) {
        auto conn = connQue_.front();
        connQue_.pop();
        mysql_close(conn); // 关闭每个 MYSQL* 连接
    }
    mysql_library_end(); // 结束 MySQL 库（可选）
}

// 析构函数：调用 ClosePool 清理资源
SqlConnPool::~SqlConnPool() {
    ClosePool();
}