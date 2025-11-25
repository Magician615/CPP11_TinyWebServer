#ifndef BLOCKQUEUE_H // 防止头文件重复包含
#define BLOCKQUEUE_H

#include <mutex>              // std::mutex 互斥锁
#include <deque>              // std::deque 双端队列容器（比 vector 更适合频繁头尾操作）
#include <condition_variable> // 条件变量，用于线程同步
#include <sys/time.h>         // 获取时间，用于超时等待

template <class T>
class BlockDeque {
public:
    explicit BlockDeque(size_t MaxCapacity = 1000); // 构造函数，设置最大容量
    ~BlockDeque();                                  // 析构函数

    void clear();      // 清空队列
    bool empty();      // 判断是否为空
    bool full();       // 判断是否满了
    void Close();      // 关闭队列，通知所有等待线程退出
    size_t size();     // 获取当前队列大小
    size_t capacity(); // 返回最大容量

    T front(); // 获取头部元素（但不删除）
    T back();  // 获取尾部元素（但不删除）

    void push_back(const T& item);  // 向队尾添加元素（生产者）
    void push_front(const T& item); // 向队首添加元素（不常用）

    bool pop(T& item);              // 取出数据（消费者），如果为空会阻塞等待
    bool pop(T& item, int timeout); // 带超时版本的 pop（超时返回 false）

    void flush(); // 唤醒一个消费者线程（用于立即写入日志）

private:
    std::deque<T> deq_; // 实际存储数据的双端队列
    size_t capacity_;   // 队列最大容量
    std::mutex mtx_;    // 互斥锁，保证线程安全
    bool isClose_;      // 队列是否关闭

    std::condition_variable condConsumer_; // 消费者条件变量
    std::condition_variable condProducer_; // 生产者条件变量
};

template <typename T>
BlockDeque<T>::BlockDeque(size_t MaxCapacity) : capacity_(MaxCapacity) {
    assert(MaxCapacity > 0);
    isClose_ = false; // 队列默认开启
}

template <typename T>
BlockDeque<T>::~BlockDeque() {
    Close(); // 程序结束时关闭队列并通知线程退出
}

template <typename T>
void BlockDeque<T>::clear() {
    std::lock_guard<std::mutex> locker(mtx_);
    deq_.clear();
}

template <typename T>
bool BlockDeque<T>::empty() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.empty();
}

template <typename T>
bool BlockDeque<T>::full() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size() >= capacity_;
}

// 用于服务器退出时，确保日志线程正确退出
template <typename T>
void BlockDeque<T>::Close() {
    {
        std::lock_guard<std::mutex> locker(mtx_); // 加锁
        deq_.clear();                             // 清空队列
        isClose_ = true;                          // 标记已关闭
    }
    condProducer_.notify_all(); // 通知所有等待生产
    condConsumer_.notify_all(); // 通知所有等待消费
}

template <typename T>
size_t BlockDeque<T>::size() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size();
}

template <typename T>
size_t BlockDeque<T>::capacity() {
    std::lock_guard<std::mutex> locker(mtx_);
    return capacity_;
}

template <typename T>
T BlockDeque<T>::front() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.front();
}

template <typename T>
T BlockDeque<T>::back() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.back();
}

template <typename T>
void BlockDeque<T>::push_back(const T& item) {
    // 注意，条件变量需要搭配unique_lock
    std::unique_lock<std::mutex> locker(mtx_);
    while (deq_.size() >= capacity_) { // 如果满了，生产者阻塞等待消费者消费
        condProducer_.wait(locker);    // 暂停生产，等待消费者唤醒生产条件变量
    }
    deq_.push_back(item);       // 正常加入队列尾部
    condConsumer_.notify_one(); // 通知一个消费者来消费
}

template <typename T>
void BlockDeque<T>::push_front(const T& item) {
    // 注意，条件变量需要搭配unique_lock
    std::unique_lock<std::mutex> locker(mtx_);
    while (deq_.size() >= capacity_) { // 如果满了，生产者阻塞等待消费者消费
        condProducer_.wait(locker);    // 暂停生产，等待消费者唤醒生产条件变量
    }
    deq_.push_front(item);      // 正常加入队列首部
    condConsumer_.notify_one(); // 通知一个消费者来消费
}

template <typename T>
bool BlockDeque<T>::pop(T& item) {
    std::unique_lock<std::mutex> locker(mtx_);
    while (deq_.empty()) {          // 如果队列为空，等待生产者写入
        condConsumer_.wait(locker); // 阻塞
        if (isClose_) {
            return false; // 如果队列关闭，退出
        }
    }
    item = deq_.front();        // 取出元素
    deq_.pop_front();           // 删除
    condProducer_.notify_one(); // 唤醒生产者
    return true;
}

template <typename T>
bool BlockDeque<T>::pop(T& item, int timeout) {
    std::unique_lock<std::mutex> locker(mtx_);
    while (deq_.empty()) {
        if (condConsumer_.wait_for(locker, std::chrono::seconds(timeout)) == std::cv_status::timeout) {
            return false; // 超时了
        }
        if (isClose_) {
            return false;
        }
        item = deq_.front();
        deq_.pop_front();
        condProducer_.notify_one();
        return true;
    }
}

template <typename T>
void BlockDeque<T>::flush() {
    condConsumer_.notify_one();
}

#endif // BLOCKQUEUE_H