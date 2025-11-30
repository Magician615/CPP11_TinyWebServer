#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <vector>        // 小根堆的实现（没有使用 priority_queue）
#include <unordered_map> // 哈希表，映射 timer id → 堆索引
#include <time.h>        // C 语言时间函数（这里没用到）
#include <algorithm>     // std::swap，用于节点交换
#include <functional>    // std::function —— 设置回调函数
#include <assert.h>      // 断言，校验逻辑
#include <chrono>        // 高精度计时器，用于定时器核心功能
#include "../log/log.h"  // 日志模块，调试使用

// 定义“回调函数类型”——到期时执行什么操作
typedef std::function<void()> TimeoutCallBack;

// 使用高精度时钟作为计时工具
typedef std::chrono::high_resolution_clock Clock;

// 时间单位：毫秒
typedef std::chrono::milliseconds MS;

// 时间戳类型
typedef Clock::time_point TimeStamp;

// 定时器节点结构体（用作堆中的元素）
struct TimerNode {
    int id;             // 定时器编号（通常对应客户端 socket fd）
    TimeStamp expires;  // 该定时器的过期时间（absolute time）
    TimeoutCallBack cb; // 超时后执行的回调函数（比如关闭连接）

    // 小顶堆排序：时间越早优先级越高（越靠顶）
    bool operator<(const TimerNode& t) {
        return expires < t.expires;
    }
};

class HeapTimer {
public:
    HeapTimer() {
        // 初始指定堆容量（减少扩容开销）
        heap_.reserve(64);
    }

    ~HeapTimer() {
        clear(); // 析构时清空所有定时器
    }

    void adjust(int id, int newExpires);                      // 调整当前 id 的超时时间
    void add(int id, int timeOut, const TimeoutCallBack& cb); // 添加/更新定时器
    void doWork(int id);                                      // 执行 id 对应回调，并删除定时器
    void clear();                                             // 清空所有定时器
    void tick();                                              // 检查堆顶定时器是否过期，并处理
    void pop();                                               // 删除堆顶定时器
    int GetNextTick();                                        // 计算距离下次超时的时间（给 epoll_wait）

private:
    void del_(size_t i);                    // 删除第 i 个节点
    void siftup_(size_t i);                 // 上浮调整（插入新节点后）
    bool siftdown_(size_t index, size_t n); // 下沉调整（删除/修改节点后）
    void SwapNode_(size_t i, size_t j);     // 交换两个堆节点，并维护 ref_

    std::vector<TimerNode> heap_;         // 小顶堆结构
    std::unordered_map<int, size_t> ref_; // id 对应的在 heap_ 中的下标，方便用heap_的时候查找
};

#endif // HEAP_TIMER_H
