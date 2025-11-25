#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <assert.h>

class ThreadPool {
public:
    ThreadPool() = default;             // 默认构造：不会创建线程（pool_ 默认为空）
    ThreadPool(ThreadPool&&) = default; // 支持移动构造（默认）

    // 尽量用make_shared代替new，如果通过new再传递给shared_ptr，内存是不连续的，会造成内存碎片化
    // make_shared:传递右值，功能是在动态内存中分配一个对象并初始化它，返回指向此对象的shared_ptr
    explicit ThreadPool(size_t threadCount = 8) : pool_(std::make_shared<Pool>()) {
        assert(threadCount > 0); // 确保线程数为正
        for (int i = 0; i < threadCount; ++i) {
            // 创建线程并立即 detach（不保留 thread 对象）
            std::thread([this]() {
                // 每个线程在这里运行一个循环：取任务->执行->等待
                std::unique_lock<std::mutex> locker(pool_->mtx_);
                while (true) {
                    if (!pool_->tasks.empty()) {                     // 如果有任务
                        auto task = std::move(pool_->tasks.front()); // 取出任务（使用移动）
                        pool_->tasks.pop();                          // 弹出队首
                        locker.unlock(); // 解锁允许其它线程访问队列（因为已经把任务取出来了，所以可以提前解锁了）
                        task();          // 执行任务
                        locker.lock();   // 执行完成后重新加锁继续循环
                    }
                    // 如果池已关闭，则退出线程循环
                    else if (pool_->isClosed) {
                        break;
                    }
                    // 没任务且未关闭则等待条件变量唤醒
                    else {
                        pool_->cond_.wait(locker); // 等待,如果任务来了就notify
                    }
                }
            }).detach(); // 将线程分离，线程退出自己清理资源
        }
    }

    // 析构：标记关闭并通知所有工作线程
    ~ThreadPool() {
        if (pool_) {
            std::unique_lock<std::mutex> locker(pool_->mtx_);
            pool_->isClosed = true; // 设置关闭标志
        }
        pool_->cond_.notify_all(); // 唤醒所有等待线程以便它们退出
    }

    // 向线程池添加任务（泛型：可以传入可调用对象）
    template <typename T>
    void AddTask(T&& task) {
        {
            std::unique_lock<std::mutex> locker(pool_->mtx_); // 加锁保护队列
            pool_->tasks.emplace(std::forward<T>(task));      // 将任务放入队列（完美转发）
        }
        pool_->cond_.notify_one(); // 通知一个等待中的线程有新任务
    }

private:
    // 用一个结构体封装起来，方便调用
    struct Pool {
        std::mutex mtx_;                         // 保护 tasks、isClosed 的互斥锁
        std::condition_variable cond_;           // 任务到来或关闭时通知线程
        bool isClosed = false;                   // 标记线程池是否关闭（注意：需要初始化）
        std::queue<std::function<void()>> tasks; // 任务队列，元素为无参返回 void 的可调用对象
    };
    std::shared_ptr<Pool> pool_; // 使用 shared_ptr 使得线程持有共享状态对象
};

#endif // THREADPOOL_H