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

## 7.WebServer 类详解
### 7.1 初始化
* 创建线程池：线程池的构造函数中会创建线程并且`detach()`
* 初始化Socket的函数`InitSocket_();`：C/S中，服务器套接字的初始化无非就是`socket` - `bind` - `listen` - `accept` - 发送接收数据这几个过程；函数执行到`listen`后，把前面得到的 listenfd 添加到 epoller 模型中，即把 `accept()` 和接收数据的操作交给 epoller 处理了。并且把该监听描述符设置为非阻塞。
* 初始化事件模式函数：`InitEventMode_(trigMode);`，将 `listenEvent_` 和 `connEvent_` 都设置为 `EPOLLET` 模式。
* 初始化数据库连接池：`SqlConnPool::Instance()->Init();`创造单例连接池，执行初始化函数。
* 初始化日志系统：在初始化函数中，创建阻塞队列和写线程，并创建日志。

### 7.2 启动WebServer
接下来启动WebServer，首先需要设定 `epoll_wait()` 等待的时间，这里我们选择调用定时器的 `GetNextTick()` 函数，这个函数的作用是返回最小堆堆顶的连接设定的过期时间与现在时间的差值。这个时间的选择可以保证服务器等待事件的时间不至于太短也不至于太长。接着调用 `epoll_wait()` 函数，返回需要已经就绪事件的数目。这里的就绪事件分为两类：收到新的 http 请求和其他的读写事件。 这里设置两个变量 `fd` 和 `events` 分别用来存储就绪事件的文件描述符和事件类型。  
1.收到新的 HTTP 请求的情况  
在 `fd==listenFd_` 的时候，也就是收到新的 HTTP 请求的时候，调用函数 `DealListen_();` 处理监听，接受客户端连接；  
2.已经建立连接的 HTTP 发来 IO 请求的情况  
在 `events& EPOLLIN` 或 `events& EPOLLOUT` 为真时，需要进行读写的处理。分别调用 `DealRead_(&users_[fd])` 和 `DealWrite_(&users_[fd])` 函数。这里需要说明： `DealListen_()` 函数并没有调用线程池中的线程，而 `DealRead_(&users_[fd])` 和 `DealWrite_(&users_[fd])` 则都交由线程池中的线程进行处理了。  
这就是Reactor，读写事件交给了工作线程处理。

### 7.3 I/O处理的具体流程
`DealRead_(&users_[fd])` 和 `DealWrite_(&users_[fd])` 通过调用
```
threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));     //读
threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));    //写
```
函数来取出线程池中的线程继续进行读写，而主进程这时可以继续监听新来的就绪事件了。  

注意此处用 `std::bind` 将参数绑定，他可以将可调用对象将参数绑定为一个仿函数，绑定后的结果可以使用 `std::function` 保存，而且 `bind` 绑定类成员函数时，第一个参数表示对象的成员函数的指针（所以上面的函数用的是 `&WebServer::OnRead_`），第二个参数表示对象的地址。  

`OnRead_()` 函数首先把数据从缓冲区中读出来(调用 HttpConn 的 `read`, `read` 调用 `ReadFd` 读取到读缓冲区 `Buffer`)，然后交由逻辑函数 `OnProcess()` 处理。注意，`process()` 函数在解析请求报文后随即就生成了响应报文等待 `OnWrite_()` 函数发送。  

这里必须说清楚 `OnRead_()` 和 `OnWrite_()` 函数进行读写的方法，那就是分散读和集中写：  

分散读（scatter read）和集中写（gatherwrite）具体来说是来自读操作的输入数据被分散到多个应用缓冲区中，而来自应用缓冲区的输出数据则被集中提供给单个写操作。 这样做的好处是：它们只需一次系统调用就可以实现在文件和进程的多个缓冲区之间传送数据，免除了多次系统调用或复制数据的开销。  

`OnWrite_()` 函数首先把之前根据请求报文生成的响应报文从缓冲区交给 `fd`，传输完成后修改该 `fd` 的 `events`.  

`OnProcess()` 就是进行业务逻辑处理（解析请求报文、生成响应报文）的函数了。  

一定要记住：“如果没有数据到来，`epoll` 是不会被触发的”。当浏览器向服务器发出 `request` 的时候，`epoll` 会接收到 `EPOLL_IN` 读事件，此时调用 `OnRead()` 去解析，将 `fd`(浏览器) 的 `request` 内容放到读缓冲区，并且把响应报文写到写缓冲区，这个时候调用 `OnProcess()` 是为了把该事件变为 `EPOLL_OUT`，让 `epoll` 下一次检测到写事件，把写缓冲区的内容写到 `fd`。当 `EPOLL_OUT` 写完后，整个流程就结束了，此时需要再次把他置回原来的 `EPOLL_IN` 去检测新的读事件到来。
