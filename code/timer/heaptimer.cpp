#include "heaptimer.h"

// 新加入/调整节点时，从下往上调整保持小顶堆结构
void HeapTimer::siftup_(size_t i) {
    assert(i >= 0 && i < heap_.size());
    size_t j = (i - 1) / 2; // j 为父节点下标
    while (j >= 0) {
        if (heap_[j] < heap_[i]) { // 若父节点比子节点小，满足小顶堆，停止
            break;
        }
        SwapNode_(i, j); // 否则交换
        i = j;
        j = (i - 1) / 2; // 继续向上比较
    }
}

// 交换堆中两个节点，同时更新 id → index 的映射
void HeapTimer::SwapNode_(size_t i, size_t j) {
    assert(i < heap_.size());
    assert(j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i; // 更新 hash 映射
    ref_[heap_[j].id] = j;
}

// 从上往下维护小顶堆结构（删除/更新时间后使用）
// false：不需要下滑  true：下滑成功  n:共几个结点
bool HeapTimer::siftdown_(size_t index, size_t n) {
    assert(index < heap_.size());
    size_t parent = index;
    size_t child = parent * 2 + 1; // 左子节点
    while (child < n) {
        // 若右子节点存在且更小，选右子节点
        if (child + 1 < n && heap_[child + 1] < heap_[child])
            child++;
        // 若父节点比最小子节点小，停止
        if (heap_[parent] < heap_[child])
            break;
        SwapNode_(parent, child);
        parent = child; // 继续下沉
        child = parent * 2 + 1;
    }
    return parent > index;
}

// 添加或更新定时器
void HeapTimer::add(int id, int timeout, const TimeoutCallBack& cb) {
    assert(id >= 0);
    size_t i;
    if (ref_.count(id) == 0) {
        // 新节点：插入堆尾并上浮
        i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id, Clock::now() + MS(timeout), cb});
        siftup_(i);
    } else {
        // 已有节点：更新时间，然后决定上浮 or 下沉
        i = ref_[id];
        heap_[i].expires = Clock::now() + MS(timeout);
        heap_[i].cb = cb;
        if (!siftdown_(i, heap_.size())) {
            siftup_(i);
        }
    }
}

// 执行 id 对应的回调，并删除节点
void HeapTimer::doWork(int id) {
    if (heap_.empty() || ref_.count(id) == 0)
        return;
    size_t i = ref_[id];
    TimerNode node = heap_[i];
    node.cb(); // 回调：通常关闭连接
    del_(i);
}

// 删除堆中指定位置节点
void HeapTimer::del_(size_t index) {
    assert(!heap_.empty() && index < heap_.size());
    size_t n = heap_.size() - 1;
    if (index < n) {
        SwapNode_(index, n); // 把目标节点和尾节点交换
        if (!siftdown_(index, n))
            siftup_(index);
    }
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

// 调整指定 id 的时间
void HeapTimer::adjust(int id, int timeout) {
    assert(!heap_.empty() && ref_.count(id) > 0);
    heap_[ref_[id]].expires = Clock::now() + MS(timeout);
    siftdown_(ref_[id], heap_.size());
}

// 处理所有过期定时器
void HeapTimer::tick() {
    if (heap_.empty())
        return;
    while (!heap_.empty()) {
        TimerNode node = heap_.front();
        if (std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0)
            break;
        node.cb(); // 执行回调
        pop();     // 删除堆顶
    }
}

// 弹出堆顶（即最早时间）
void HeapTimer::pop() {
    assert(!heap_.empty());
    del_(0);
}

// 清空定时器
void HeapTimer::clear() {
    ref_.clear();
    heap_.clear();
}

// 给 epoll_wait 返回等待时间（毫秒）
int HeapTimer::GetNextTick() {
    tick(); // 处理到期的
    size_t res = -1;
    if (!heap_.empty()) {
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
        if (res < 0)
            res = 0;
    }
    return res;
}
