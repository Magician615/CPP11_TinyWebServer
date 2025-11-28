#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <unordered_map> // 用于定义静态映射，快速查找 MIME 类型、状态码等
#include <string>        // to_string()
#include <fcntl.h>       // open() 文件读写方式
#include <unistd.h>      // close() 文件
#include <sys/stat.h>    // stat() 获取文件属性（大小、类型等）
#include <sys/mman.h>    // mmap(), munmap() 用于将文件映射到内存，提高读取效率

#include "../buffer/buffer.h"
#include "../log/log.h"

class HttpResponse {
public:
    HttpResponse();  // 构造函数
    ~HttpResponse(); // 析构函数，释放资源

    // 初始化响应对象：传入网站根目录、请求路径、是否为长连接 和 状态码
    void Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);

    // 根据当前的 path_、srcDir_ 等生成完整的 HTTP 响应并写入缓冲区 buff
    void MakeResponse(Buffer& buff);

    // 解除之前的文件映射（如果存在）
    void UnmapFile();

    // 返回映射的文件指针（用于之后用 writev 发送）
    char* File();

    // 返回映射文件的长度（Content-Length）
    size_t FileLen() const;

    // 当需要直接返回错误信息（非静态错误页）时，生成简单的 HTML 错误页面并追加到 buff
    void ErrorContent(Buffer& buff, std::string message);

    // 返回 HTTP 状态码
    int Code() const;

private:
    void AddStateLine_(Buffer& buff); // 添加状态行（HTTP/1.1 200 OK）
    void AddHeader_(Buffer& buff);    // 添加响应头
    void AddContent_(Buffer& buff);   // 添加响应体（文件内容）

    void ErrorHtml_();          // 设置错误页面路径
    std::string GetFileType_(); // 根据文件后缀推断 MIME 类型

    int code_;         // HTTP 状态码（如 200、404）
    bool isKeepAlive_; // 是否启用 HTTP 长连接

    std::string path_;   // 请求资源路径（如 "/index.html"）
    std::string srcDir_; // 网站根目录（如 "/var/www/html/"）

    char* mmFile_;           // mmap 映射的文件指针
    struct stat mmFileStat_; // 记录目标文件的信息（大小、类型等）

    // 静态映射：文件后缀 → MIME 类型
    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    // 状态码 → 状态名称（如 200 → OK）
    static const std::unordered_map<int, std::string> CODE_STATUS;
    // 错误码 → 错误页面路径（如 404 → "/404.html"）
    static const std::unordered_map<int, std::string> CODE_PATH;
};

#endif // HTTP_RESPONSE_H