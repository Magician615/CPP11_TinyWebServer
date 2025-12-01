#include "webserver.h"

// 构造函数：初始化服务器配置与各个子模块（定时器、线程池、epoller、MySQL连接池等）
WebServer::WebServer(int port, int trigMode, int timeoutMS, bool OptLinger, int sqlPort, const char* sqlUser,
                     const char* sqlPwd, const char* dbName, int connPoolNum, int threadNum, bool openLog, int logLevel,
                     int logQueSize)
    : port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false), timer_(new HeapTimer()),
      threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller()) {
    // 获取当前工作目录（返回动态分配内存，需要 later free）
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    // 将资源目录拼接到当前工作目录后面（确保目录尾部空间够用）
    strncat(srcDir_, "/resources/", 16);

    // 初始化 HttpConn 的静态成员：在线用户数和资源目录
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;

    // 初始化 MySQL 连接池（连接到本地主机，端口 sqlPort）
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    // 根据传入的 trigMode 设置 epoll 触发模式（ET/LT）等
    InitEventMode_(trigMode);

    // 创建监听 socket 并注册到 epoll，若失败则标记 isClose_
    if (!InitSocket_()) {
        isClose_ = true;
    }

    // 初始化日志系统（如果需要）
    if (openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if (isClose_) {
            LOG_ERROR("========== Server init error!==========");
        } else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger ? "true" : "false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s", (listenEvent_ & EPOLLET ? "ET" : "LT"),
                     (connEvent_ & EPOLLET ? "ET" : "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

// 析构：关闭监听 fd，标记关闭，释放 srcDir 内存，并关闭数据库连接池
WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

// 根据传入的 trigMode 设置 listenEvent_ 与 connEvent_
// listenEvent_ 用于监听 socket（accept），connEvent_ 用于客户端连接
void WebServer::InitEventMode_(int trigMode) {
    // 默认关注对端关闭事件
    listenEvent_ = EPOLLRDHUP;
    // 客户端事件默认使用 ONESHOT（配合线程池避免并发处理）并关注对端关闭
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode) {
        case 0: // 默认 LT 模式
            break;
        case 1: // conn ET
            connEvent_ |= EPOLLET;
            break;
        case 2: // listen ET
            listenEvent_ |= EPOLLET;
            break;
        case 3:
            listenEvent_ |= EPOLLET; // listen ET
            connEvent_ |= EPOLLET;   // conn ET
            break;
        default:
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
    }
    // 告诉 HttpConn 全局是否使用 ET 模式（给 HttpConn::read/write 行为做判断）
    HttpConn::isET = (connEvent_ & EPOLLET);
}

// 主循环：等待 epoll 事件并分发处理
void WebServer::Start() {
    int timeMS = -1; /* epoll wait timeout == -1 无事件将阻塞 */
    if (!isClose_) {
        LOG_INFO("========== Server start ==========");
    }
    while (!isClose_) {
        // 若启用超时检测，则获取下一次最近的定时器触发时间，传给 epoll_wait 作为超时
        if (timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick();
        }
        // 等待事件发生，最多等待 timeMS 毫秒（-1 表示阻塞）
        int eventCnt = epoller_->Wait(timeMS);
        for (int i = 0; i < eventCnt; i++) {
            /* 处理每个就绪事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);

            // 如果是监听 socket，就处理新的连接
            if (fd == listenFd_) {
                DealListen_();
            }
            // 处理异常 / 对端关闭 / 错误 等情况
            else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            }
            // 可读事件
            else if (events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            // 可写事件
            else if (events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

// 发送错误信息并关闭 fd（用于拒绝连接等）
void WebServer::SendError_(int fd, const char* info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

// 关闭并清理连接：从 epoll 删除并调用 HttpConn::Close()
void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}

// 新连接加入：初始化 users_ 中的 HttpConn（placement by fd），加入定时器并注册 epoll
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    users_[fd].init(fd, addr); // 初始化 HttpConn 对象（构造在 unordered_map 中）
    if (timeoutMS_ > 0) {
        // 为该 fd 添加超时定时器，回调为 CloseConn_（用于超时断开）
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    // 注册到 epoll，初始只监听读事件 + connEvent_（例如 ONESHOT, EPOLLET）
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    SetFdNonblock(fd); // 将客户端 socket 设为非阻塞
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

// 监听 socket 可读（即有新连接）时调用；如果是 ET 模式需要循环 accept
void WebServer::DealListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr*)&addr, &len);
        if (fd <= 0) {
            // accept 失败：可能没有新的连接（在非阻塞/ET下会返回 -1, errno==EAGAIN）
            return;
        } else if (HttpConn::userCount >= MAX_FD) {
            // 超过最大并发，发送错误并关闭
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        // 将新连接注册并初始化
        AddClient_(fd, addr);
    } while (listenEvent_ & EPOLLET); // 如果监听 socket 为 ET，需要循环 accept 直到返回 EAGAIN
}

// 读事件分发：延长定时器并把读取任务交给线程池
void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);                                                // 延长该连接的超时时间
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client)); // 交给线程池处理
}

// 写事件分发：同样延长定时器并交给线程池
void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

// 调整/延长指定客户端的超时时间（在有活动时调用）
void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    if (timeoutMS_ > 0) {
        timer_->adjust(client->GetFd(), timeoutMS_);
    }
}

// 线程池中实际执行的读取逻辑：从 HttpConn 读取数据，若错误则关闭连接，否则继续处理请求
void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);        // 调用 HttpConn::read，内部会把数据读到其 Buffer
    if (ret <= 0 && readErrno != EAGAIN) { // 出错且不是 EAGAIN（表示暂时无数据）
        CloseConn_(client);
        return;
    }
    // 读取成功或 EAGAIN（对于 ET 需要注意），进入请求处理流程
    OnProcess(client);
}

// 处理请求：解析并准备响应；根据是否有响应数据设置下次 epoll 监听为写或继续读
void WebServer::OnProcess(HttpConn* client) {
    if (client->process()) { // process() 解析请求并构造响应，返回 true 表示已准备好响应
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT); // 监听可写以发送响应
    } else {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN); // 继续监听读
    }
}

// 线程池中实际执行的写逻辑：调用 HttpConn::write 将 iov 中数据写出
void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno); // 调用写，返回写出字节数或错误码
    if (client->ToWriteBytes() == 0) {
        /* 如果剩余待写为 0，说明本次传输已完成 */
        if (client->IsKeepAlive()) {
            // 若是长连接，则继续处理新的请求（保持连接）
            OnProcess(client);
            return;
        }
    } else if (ret < 0) {
        if (writeErrno == EAGAIN) {
            /* 若写缓冲已满，等待下一次可写事件继续发送 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    // 非长连接或写出失败，关闭连接
    CloseConn_(client);
}

/* Create listenFd 并绑定监听，同时把 listenFd 加入 epoll */
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    // 检查端口号
    if (port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!", port_);
        return false;
    }
    // 填充地址结构（监听所有网卡）
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    // 如果启用优雅关闭，设置 linger 选项，使得 close() 时可尽量发送完剩余数据
    struct linger optLinger = {0};
    if (openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    // 创建 socket
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    // 设置 SO_LINGER（无论 openLinger_ 是否设置，都调用 setsockopt；如果 openLinger_ == false，optLinger 是 {0,0}）
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用，避免 TIME_WAIT 阻塞重启 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if (ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    // 绑定端口
    ret = bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 监听
    ret = listen(listenFd_, 6);
    if (ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }
    // 将监听 fd 加入 epoll，监听 listenEvent_（如有 EPOLLET）以及读事件 EPOLLIN
    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if (ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    // 将监听 socket 设置为非阻塞
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

// 设置文件描述符为非阻塞（返回 fcntl 的结果）
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    // 注意：此处使用 F_GETFD 可能是笔误，通常应使用 F_GETFL 获取 flags
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}
