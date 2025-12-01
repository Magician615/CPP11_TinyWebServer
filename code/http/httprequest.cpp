#include "httprequest.h"

// 不带.html 的 URL，如果用户只访问 /index，就自动变成 /index.html
const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML{
    "/index", "/register", "/login", "/welcome", "/video", "/picture",
};

// 判断访问 .html 页面类型
const std::unordered_map<std::string, int> HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html", 0}, // 0 = 注册页面
    {"/login.html", 1},    // 1 = 登录页面
};

// 16进制转化为10进制（%20 表示空格' '）
int HttpRequest::ConverHex(char ch) {
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    return ch;
}

// 初始化请求解析状态（可用于复用 HttpRequest 对象）
void HttpRequest::Init() {
    method_ = path_ = version_ = body_ = ""; // 清空请求方式、URL、版本、请求体
    state_ = REQUEST_LINE;                   // 从解析请求行开始
    header_.clear();                         // 清空头部键值对
    post_.clear();                           // 清空 POST 表单数据
}

// 判断是否为长连接（keep-alive）
bool HttpRequest::IsKeepAlive() const {
    if (header_.count("Connection") == 1) {                                             // 是否存在 Connection 头
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1"; // HTTP1.1 默认支持长连接
    }
    return false;
}

// 解析 HTTP 请求（入口函数）
bool HttpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n"; // HTTP 行结束符

    // 没有数据可读
    if (buff.ReadableBytes() <= 0) {
        return false;
    }

    // 只要有数据可读，并且解析没有完成（FINISH），就一直处理
    while (buff.ReadableBytes() && state_ != FINISH) {
        // 查找 "\r\n" 行结束符
        const char* lineEnd = std::search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
        // 获取这一行字符串（不包含 "\r\n"）
        std::string line(buff.Peek(), lineEnd);

        switch (state_) {
            case REQUEST_LINE:
                if (!ParseRequestLine_(line)) { // 解析请求行
                    return false;
                }
                ParsePath_(); // 处理 URL 文件路径
                break;
            case HEADERS:
                ParseHeader_(line);              // 解析请求头
                if (buff.ReadableBytes() <= 2) { // 如果只剩下 "\r\n"，说明头部结束
                    state_ = FINISH;
                }
                break;
            case BODY:
                ParseBody_(line); // 解析请求体
                break;
            default: break;
        }

        if (lineEnd == buff.BeginWrite()) { // 当前读取完所有数据
            break;
        }
        buff.RetrieveUntil(lineEnd + 2); // 移除本行数据 + "\r\n"
    }

    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

// 获取 URL 路径，例如 "/index.html"
std::string HttpRequest::path() const {
    return path_;
}

// 允许修改 path
std::string& HttpRequest::path() {
    return path_;
}

// 获取请求方式，例如 "GET" / "POST"
std::string HttpRequest::method() const {
    return method_;
}

// 获取 HTTP 版本号，例如 "1.1"
std::string HttpRequest::version() const {
    return version_;
}

// 获取 POST 表单中对应 key 的 value
std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");
    if (post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if (post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

// 解析请求行
bool HttpRequest::ParseRequestLine_(const std::string& line) {
    std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$"); // 匹配格式：GET /path HTTP/1.1
    std::smatch subMatch;
    if (regex_match(line, subMatch, patten)) {
        method_ = subMatch[1];  // GET / POST
        path_ = subMatch[2];    // /index.html
        version_ = subMatch[3]; // 1.1
        state_ = HEADERS;       // 下一步解析头部
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

// 解析请求头
void HttpRequest::ParseHeader_(const std::string& line) {
    std::regex patten("^([^:]*): ?(.*)$"); // 例如 Host: 127.0.0.1
    std::smatch subMatch;
    if (regex_match(line, subMatch, patten)) {
        header_[subMatch[1]] = subMatch[2]; // 存储键值对
    } else {
        state_ = BODY; // 如果这行没冒号，说明是空行 → 请求体开始
    }
}

// 解析请求体
void HttpRequest::ParseBody_(const std::string& line) {
    body_ = line; // 保存 body 数据
    ParsePost_(); // 进一步解析 POST 表单
    state_ = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

// 解析 URL 路径
void HttpRequest::ParsePath_() {
    if (path_ == "/") {
        path_ = "/index.html"; // 首页
    } else {
        for (auto& item : DEFAULT_HTML) {
            if (item == path_) {
                path_ += ".html"; // 自动加 .html 后缀
                break;
            }
        }
    }
}

// 解析 POST 请求
void HttpRequest::ParsePost_() {
    if (method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        ParseFromUrlencoded_(); // 解析键值对

        // 处理登录/注册请求
        if (DEFAULT_HTML_TAG.count(path_)) {
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            bool isLogin = (tag == 1);
            if (UserVerify(post_["username"], post_["password"], isLogin)) {
                path_ = "/welcome.html"; // 验证成功跳转
            } else {
                path_ = "/error.html"; // 失败跳转
            }
        }
    }
}

// 解析表单数据格式：key=value&...
void HttpRequest::ParseFromUrlencoded_() {
    // 如果请求体为空，则无需解析
    if (body_.size() == 0)
        return; // 直接返回

    std::string key, value; // 临时用于保存解析出的键和值
    int num = 0;            // 记录当前字符位置
    int n = body_.size();   // 请求体总长度
    int i = 0, j = 0;       // i 和 j 用于记录键值起始位置

    // 遍历请求体中的每个字符
    for (; i < n; i++) {
        char ch = body_[i];
        switch (ch) {
            // 遇到 '='，说明前面的是 key，开始记录 value
            case '=':
                key = body_.substr(j, i - j); // 提取键名
                j = i + 1;                    // 更新起始位置为下一个字符
                break;
            // '+' 在编码中代表空格
            case '+':
                body_[i] = ' '; // 替换为真实空格
                break;
            // 遇到 URL 编码字符，例如：%20 代表空格
            case '%':
                num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]); // 解码计算
                body_[i + 2] = num % 10 + '0';                                // 低位转换为字符
                body_[i + 1] = num / 10 + '0';                                // 高位转换为字符
                i += 2;                                                       // 向后跳过两个字符，因为 %xx 共三个字符
                break;
            // '&' 表示一个键值对结束
            case '&':
                value = body_.substr(j, i - j); // 获取值
                j = i + 1;                      // 更新为下一个键值对起始位置
                post_[key] = value;             // 解析出一组 key=value
                LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
                break;
            default: break;
        }
    }

    // 处理最后一个 key=value
    assert(j <= i);
    if (post_.count(key) == 0 && j < i) {
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}

// 校验用户信息（用于登录注册）
bool HttpRequest::UserVerify(const std::string& name, const std::string& pwd, bool isLogin) {
    // 用户名和密码不能为空
    if (name == "" || pwd == "") {
        return false;
    }

    // 打日志，记录用户名密码（仅用于调试，不建议生产环境打印密码）
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());

    MYSQL* sql;
    SqlConnRAII(&sql, SqlConnPool::Instance()); // 从连接池中获取 MySQL 连接，RAII 自动回收
    assert(sql);                                // 确保数据库连接成功

    bool flag = false;        // 用于判断验证是否成功
    char order[256] = {0};    // SQL 语句缓存区
    MYSQL_RES* res = nullptr; // 查询结果集指针
    // unsigned int j = 0;            // 保存字段个数
    // MYSQL_FIELD* fields = nullptr; // 字段信息

    if (!isLogin) {
        flag = true; // 如果是注册，先假设用户名可用
    }

    // 查询用户及密码
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);

    // 执行查询语句
    if (mysql_query(sql, order)) { // 如果执行失败
        mysql_free_result(res);    // 释放结果集（虽然 res 是空的）
        return false;              // 返回失败
    }

    res = mysql_store_result(sql); // 获取查询结果
    // j = mysql_num_fields(res);        // 获取列数
    // fields = mysql_fetch_fields(res); // 获取列信息

    // 遍历查询结果（如果能取出说明用户名存在）
    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        // 打印数据库的 username 和 password
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        std::string password(row[1]); // 获取数据库中存储的密码

        // 如果是登录操作
        if (isLogin) {
            if (pwd == password) { // 用户输入的密码匹配
                flag = true;
            } else { // 密码错误
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        } else { // 如果是注册但是用户名已存在
            flag = false;
            LOG_DEBUG("user used!"); // 用户名被占用
        }
    }
    mysql_free_result(res); // 释放结果集

    // 注册行为 且 用户名未被使用
    if (!isLogin && flag == true) {
        LOG_DEBUG("regirster!"); // 提示开始注册
        bzero(order, 256);       // 清空 SQL 缓冲区
        snprintf(order, 256, "INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG("%s", order); // 打印 SQL 插入语句，用于调试

        // 执行 SQL 插入语句
        if (mysql_query(sql, order)) {
            LOG_DEBUG("Insert error!"); // 插入失败（可能是数据库错误）
            flag = false;
        }
        flag = true; // 插入成功
    }
    SqlConnPool::Instance()->FreeConn(sql); // 归还数据库连接
    LOG_DEBUG("UserVerify success!!");
    return flag; // 返回最终结果
}
