#include "epoller.h"

Epoller::Epoller(int maxEvent)
    : epollFd_(epoll_create(512)),               // 创建epoll实例，参数已被内核忽略
      events_(maxEvent) {                        // 分配事件数组大小
    assert(epollFd_ >= 0 && events_.size() > 0); // 保证创建成功
}

Epoller::~Epoller() {
    close(epollFd_); // 关闭epoll实例，释放资源
}

bool Epoller::AddFd(int fd, uint32_t events) {
    if (fd < 0)
        return false;                                        // fd非法
    epoll_event ev = {0};                                    // 初始化事件结构
    ev.data.fd = fd;                                         // 设置关联的文件描述符
    ev.events = events;                                      // 设置监听事件类型（如 EPOLLIN | EPOLLET）
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev); // 添加监听
}

bool Epoller::ModFd(int fd, uint32_t events) {
    if (fd < 0)
        return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events; // 修改监听事件
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

bool Epoller::DelFd(int fd) {
    if (fd < 0)
        return false;
    epoll_event ev = {0}; // 删除时 ev 可以为 NULL，但某些系统要求传递非空
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev);
}

int Epoller::Wait(int timeoutMs) {
    return epoll_wait(epollFd_, &events_[0],            // 传入事件数组地址
                      static_cast<int>(events_.size()), // 支持的最大事件数
                      timeoutMs);                       // 超时时间（ms）
}

int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0); // 越界检查
    return events_[i].data.fd;            // 返回触发事件的fd
}

uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events; // 返回触发事件类型（EPOLLIN等）
}
