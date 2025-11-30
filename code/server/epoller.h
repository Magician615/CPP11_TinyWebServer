#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h> // epoll_ctl()、epoll_wait()、epoll_event
#include <fcntl.h>     // fcntl()，用于设置文件描述符为非阻塞
#include <unistd.h>    // close() 关闭文件描述符
#include <assert.h>    // assert() 断言检查程序合法性
#include <vector>      // vector容器，用于存储事件数组
#include <errno.h>     // errno 错误码

class Epoller {
public:
    explicit Epoller(int maxEvent = 1024); // 构造函数，预设最大事件数量

    ~Epoller(); // 析构函数，释放epollFd_资源

    bool AddFd(int fd, uint32_t events); // 向epoll添加文件描述符
    bool ModFd(int fd, uint32_t events); // 修改fd监听事件类型
    bool DelFd(int fd);                  // 从epoll中删除fd

    int Wait(int timeoutMs = -1); // 等待内核触发事件，默认-1为阻塞

    int GetEventFd(size_t i) const;     // 获取第i个事件对应的fd
    uint32_t GetEvents(size_t i) const; // 获取第i个事件的触发类型

private:
    int epollFd_;                            // epoll实例的文件描述符
    std::vector<struct epoll_event> events_; // 存放触发的事件数组
};

#endif // EPOLLER_H
