#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map> // 用于存储键值对（header、post 数据）
#include <unordered_set> // 用于快速判断 path 是否需要加 .html
#include <string>        // 字符串类型
#include <regex>         // 用于正则表达式解析 HTTP 请求行和头部
#include <errno.h>       // 错误编号（例如网络异常）
#include <mysql/mysql.h> // MySQL 数据库操作库

#include "../buffer/buffer.h"    // 自己实现的缓冲区类（用于读取 HTTP 内容）
#include "../log/log.h"          // 日志模块
#include "../pool/sqlconnpool.h" // MySQL 连接池

// HTTP请求解析类
class HttpRequest {
public:
    // 解析状态机的状态
    enum PARSE_STATE {
        REQUEST_LINE, // 解析请求行：例如 "GET /index.html HTTP/1.1"
        HEADERS,      // 解析请求头
        BODY,         // 解析请求体（POST内容）
        FINISH,       // 解析结束
    };

    // HTTP状态码（返回给HttpConn，用于设置响应内容）
    enum HTTP_CODE {
        NO_REQUEST = 0,     // 请求不完整
        GET_REQUEST,        // GET 请求完整
        BAD_REQUEST,        // 语法错误
        NO_RESOURCE,        // 资源不存在
        FORBIDDENT_REQUEST, // 权限不足
        FILE_REQUEST,       // 请求静态文件
        INTERNAL_ERROR,     // 服务器内部错误
        CLOSED_CONNECTION,  // 连接已关闭
    };

    // 构造函数：初始化对象
    HttpRequest() {
        Init(); // 调用Init函数，设置初始状态
    }
    ~HttpRequest() = default; // 默认析构函数

    // 初始化请求解析状态（可用于复用 HttpRequest 对象）
    void Init();

    // 解析 HTTP 请求（入口函数）
    bool parse(Buffer& buff);

    // 获取 URL 路径，例如 "/index.html"
    std::string path() const;
    std::string& path(); // 允许修改 path

    // 获取请求方式，例如 "GET" / "POST"
    std::string method() const;

    // 获取 HTTP 版本号，例如 "1.1"
    std::string version() const;

    // 获取 POST 表单中对应 key 的 value
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    // 判断是否为长连接（keep-alive）
    bool IsKeepAlive() const;

private:
    // 以下是请求解析的内部函数（分阶段完成解析）
    bool ParseRequestLine_(const std::string& line); // 解析请求行
    void ParseHeader_(const std::string& line);      // 解析请求头
    void ParseBody_(const std::string& line);        // 解析请求体
    void ParsePath_();                               // 解析 URL 路径
    void ParsePost_();                               // 解析 POST 请求
    void ParseFromUrlencoded_();                     // 解析表单数据格式：key=value&...

    // 校验用户信息（用于登录注册）
    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);

    // 成员变量
    PARSE_STATE state_;                                   // 当前解析状态
    std::string method_, path_, version_, body_;          // 请求方式、路径、版本、请求体
    std::unordered_map<std::string, std::string> header_; // 请求头字段
    std::unordered_map<std::string, std::string> post_;   // POST表单数据

    // 静态常量（所有对象共享）
    static const std::unordered_set<std::string> DEFAULT_HTML;          // 默认网页
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG; // 网页类型（用于区分登录/注册）
    static int ConverHex(char ch);                                      // 16进制转化为10进制（%20 表示空格' '）
};

#endif // HTTP_REQUEST_H