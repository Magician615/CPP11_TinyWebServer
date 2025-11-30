# Epoller 和 Webserver

## 1.epoll
`epoll` ：是 Linux 内核提供的高效 I/O 多路复用机制，用于管理大量文件描述符（如 socket）。  
`epoll` 实例：由 `epoll_create()` 创建，返回一个文件描述符（`epollFd`），后用它管理事件集合并调用 `epoll_wait` 等待事件。  
`epoll` 典型流程：`epoll_create` → `epoll_ctl(ADD)` 注册 fd → `epoll_wait` 等待内核返回准备好的事件 → 处理事件（读写等）→ 根据需要 `mod/del`。
相比 `select` / `poll`：
| 特性      | select | poll | epoll           |
| ------- | ------ | ---- | --------------- |
| 最大连接数限制 | 有      | 无    | 无               |
| 扫描效率    | O(n)   | O(n) | **O(1)**        |
| 内核触发    | 轮询     | 轮询   | **事件驱动 (回调触发)** |
| 推荐使用    | ❌      | ❌    | ✅（高性能）          |

## 2.Epoller 类作用是什么？
对 `epoll` 系统调用进行简单封装，便于在 `WebServer` 中使用。

## 3.为什么用 `vector<epoll_event>` 保存事件？
因为 `epoll_wait()` 需要用户提供一个数组来存放触发的事件，所以我们准备一个 `vector`，它既能自动释放内存，也能动态扩容，非常适合这种用途。

## 4.Epoller 类核心方法思路
| 函数             | 作用          | 什么时候调用？       |
| -------------- | ----------- | ------------- |
| `AddFd()`      | 监听新的 socket | 客户端首次连接       |
| `ModFd()`      | 修改监听事件      | 可写时、状态变化      |
| `DelFd()`      | 删除监听        | 客户端断开连接       |
| `Wait()`       | 等待触发事件      | 事件循环（核心）      |
| `GetEventFd()` | 获取事件fd      | epoll_wait后处理 |
| `GetEvents()`  | 获取事件类型      | 判断读/写等        |  
`Epoller` 只是感知 I/O 就绪，不做读写。真正处理数据在 `HttpConn::process()` 中完成

## 5.epoll 主要API和参数
```
/* 创建一个新的 epoll 实例。在内核中创建了一个数据，这个数据中有两个比较重要的数据，
一个是需要检测的文件描述符的信息（红黑树），还有一个是就绪列表，存放检测到数据发送改变的文件描述符信息（双向链表）。*/
int epoll_create(int size);
	- 参数：
		size : 目前没有意义了。随便写一个数，必须大于0
	- 返回值：
		-1 : 失败
		> 0 : 文件描述符，操作epoll实例的

// 对 epoll 实例进行管理：添加文件描述符信息，删除信息，修改信息
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
	- 参数：
		- epfd : epoll实例对应的文件描述符
		- op : 要进行什么操作
			EPOLL_CTL_ADD: 添加
			EPOLL_CTL_MOD: 修改
			EPOLL_CTL_DEL: 删除
		- fd : 目标文件描述符
		- event : 指向 struct epoll_event，用来设置你想监听的事件掩码和关联数据
    - 返回值：
        - 成功返回 0
        - 失败返回 -1，errno 说明原因

// 阻塞/等待并返回就绪的事件列表
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
	- 参数：
		- epfd : epoll实例对应的文件描述符
		- events : 传出参数，保存了发生了变化的文件描述符的信息
		- maxevents : events 数组的大小（最大能返回的事件数）；epoll_wait 最多写入 maxevents 个事件。
		- timeout : 阻塞时间
			- 0 : 立即返回（非阻塞）
			- -1 : 阻塞直到事件到来
			- > 0 : 阻塞的时长（毫秒）
	- 返回值：
		- 成功，返回发送变化的文件描述符的个数 > 0
		- 失败 -1

struct epoll_event {
    uint32_t events;     /* Epoll events (mask) */
    epoll_data_t data;   /* User data variable */
};
union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
};
/*
    events：事件掩码（EPOLLIN|EPOLLOUT|EPOLLERR|...）。这是内核回传给你的就绪类型。你根据它决定要读还是写。
    data：用户附加数据，你可以把任意值塞进去：
    data.fd 常用：直接把 fd 放进去（简单）。
    data.ptr 更通用：可以放对象指针（比如 HttpConn*），对面向对象程序很方便 —— 读取后直接得到连接对象。
    u32/u64：如果需存整数/ID，或64位安全。
*/
```

## 6.重要事件掩码（flags）详解
可读/可写相关
* `EPOLLIN`：对应 fd 可读（对 socket 表示有新数据可读，或对监听 socket 表示有新连接）。
* `EPOLLOUT`：对应 fd 可写（写不会阻塞）。
* `EPOLLRDHUP`：对端半关闭（对端关闭写端），常用于检测对端关闭。比 EPOLLHUP 更早出现。  

异常/错误相关
* `EPOLLERR`：有错误发生（不可通过 `epoll_ctl` 指定，内核会在 `events` 中设置）。
* `EPOLLHUP`：挂起/关闭事件（也会返回 `EPOLLIN | EPOLLHUP` 等组合）。
* 处理建议：在处理事件时，一般先检查 `EPOLLERR|EPOLLHUP` 并关闭连接或读更多错误信息。  

触发模式
* `EPOLLET`：边缘触发（Edge Triggered）。事件只有在状态发生变化时通知（效率高，但需要非阻塞 + 循环读完）。
* 默认不加 `EPOLLET` 时是 水平触发（Level Triggered），只要 fd 处于可读/可写状态会持续通知（简单但在高并发下效率略差）。  

单次通知
* `EPOLLONESHOT`：事件触发后内核自动禁用该 fd 的事件，需要用户再次 `MOD` 来重新启用。常用于多线程+线程池场景，确保同一连接在同一时间只被一个线程处理，防止竞态。  

优先／紧急
* `EPOLLPRI`：带外数据到达（很少用，TCP 带外较罕见）。
