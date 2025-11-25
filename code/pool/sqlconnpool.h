#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h> // MySQL C API
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h> // POSIX semaphore
#include <thread>
#include "../log/log.h"

// 数据库连接池（单例），负责维护固定数量的 MYSQL* 连接以复用
class SqlConnPool {
public:
    static SqlConnPool* Instance(); // 单例访问

    MYSQL* GetConn();           // 获取一个连接（从队列中 pop）
    void FreeConn(MYSQL* conn); // 释放连接（push 回队列）
    int GetFreeConnCount();     // 当前可用连接数量

    // 初始化连接池（创建 connSize 个连接）
    void Init(const char* host, int port, const char* user, const char* pwd, const char* dbName, int connSize);
    void ClosePool(); // 关闭池并释放所有连接

private:
    SqlConnPool();
    ~SqlConnPool();

    int MAX_CONN_;  // 池的最大连接数
    int useCount_;  // 当前被使用的连接数（未严格维护）
    int freeCount_; // 当前空闲连接数（未严格维护）

    std::queue<MYSQL*> connQue_; // 存放 MYSQL* 的队列
    std::mutex mtx_;             // 保护 connQue_
    sem_t semId_;                // 信号量用于阻塞等待可用连接
};

// 资源在对象构造初始化,资源在对象析构时释放
class SqlConnRAII {
public:
    // 构造时从连接池获取连接，并把 MYSQL* 赋给外部传入的指针
    SqlConnRAII(MYSQL** sql, SqlConnPool* connpool) {
        assert(connpool);
        *sql = connpool->GetConn(); // 从池中拿一个连接
        sql_ = *sql;
        connpool_ = connpool;
    }

    // 析构时自动释放连接回池
    ~SqlConnRAII() {
        if (sql_) {
            connpool_->FreeConn(sql_);
        }
    }

private:
    MYSQL* sql_;            // 当前持有的 MYSQL 连接
    SqlConnPool* connpool_; // 连接池指针
};

#endif // SQLCONNPOOL_H