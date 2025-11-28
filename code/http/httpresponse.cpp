#include "httpresponse.h"

// 静态映射：文件后缀 → MIME 类型
const std::unordered_map<std::string, std::string> HttpResponse::SUFFIX_TYPE = {
    {".html", "text/html"},          {".xml", "text/xml"},          {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},          {".rtf", "application/rtf"},   {".pdf", "application/pdf"},
    {".word", "application/nsword"}, {".png", "image/png"},         {".gif", "image/gif"},
    {".jpg", "image/jpeg"},          {".jpeg", "image/jpeg"},       {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},         {".mpg", "video/mpeg"},        {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},   {".tar", "application/x-tar"}, {".css", "text/css "},
    {".js", "text/javascript "},
};

// 状态码 → 状态名称（如 200 → OK）
const std::unordered_map<int, std::string> HttpResponse::CODE_STATUS = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
};

// 错误码 → 错误页面路径（如 404 → "/404.html"）
const std::unordered_map<int, std::string> HttpResponse::CODE_PATH = {
    {400, "/400.html"},
    {403, "/403.html"},
    {404, "/404.html"},
};

// 构造函数
HttpResponse::HttpResponse() {
    code_ = -1;           // -1 代表还未设置状态码
    path_ = srcDir_ = ""; // 资源路径和根目录初始化为空
    isKeepAlive_ = false; // 默认关闭长连接
    mmFile_ = nullptr;    // 还未映射文件
    mmFileStat_ = {0};    // stat 结构体清空
}

// 析构函数，释放资源
HttpResponse::~HttpResponse() {
    UnmapFile(); // 若存在 mmap 文件映射，释放资源
}

// 初始化响应对象：传入网站根目录、请求路径、是否为长连接 和 状态码
void HttpResponse::Init(const std::string& srcDir, std::string& path, bool isKeepAlive, int code) {
    assert(srcDir != ""); // 根目录不能为空
    if (mmFile_) {        // 如果上次存在未解除的映射
        UnmapFile();      // 先解除映射，避免资源泄露
    }
    code_ = code;               // 设置状态码
    isKeepAlive_ = isKeepAlive; // 设置是否保持连接
    path_ = path;               // 保存请求路径（例如 "/index.html"）
    srcDir_ = srcDir;           // 保存网站根目录（例如 "./resources"）
    mmFile_ = nullptr;          // 重置映射指针
    mmFileStat_ = {0};          // 重置文件信息
}

// 根据当前的 path_、srcDir_ 等生成完整的 HTTP 响应并写入缓冲区 buff
void HttpResponse::MakeResponse(Buffer& buff) {
    /* 判断请求的资源文件 */
    // stat 用来获得文件的属性（大小、权限等）。参数是完整路径字符串。
    // 如果 stat 返回 < 0 表示文件不存在或不可访问，或路径是目录而非文件
    if (stat((srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {
        code_ = 404; // 文件不存在或是目录 → 404
    }
    // 如果文件的其他用户可读标志未设置，则认为没有公开读取权限
    else if (!(mmFileStat_.st_mode & S_IROTH)) {
        code_ = 403; // 没有读取权限 → 403
    }
    // 如果调用 Init 时没有指定 code，则默认 200 OK
    else if (code_ == -1) {
        code_ = 200; // 若之前没设置状态码 → 200
    }
    ErrorHtml_();        // 如果是错误码，可能需要把 path_ 替换为错误页面
    AddStateLine_(buff); // 添加状态行，例如 "HTTP/1.1 200 OK\r\n"
    AddHeader_(buff);    // 添加响应头（Connection, Content-Type 等）
    AddContent_(buff);   // 添加响应体相关（将文件映射并写 Content-length）
}

// 解除之前的文件映射（如果存在）
void HttpResponse::UnmapFile() {
    if (mmFile_) {                            // 如果有映射
        munmap(mmFile_, mmFileStat_.st_size); // 解除映射
        mmFile_ = nullptr;                    // 清空指针，避免重复解除
    }
}

// 返回映射的文件指针（用于之后用 writev 发送）
char* HttpResponse::File() {
    return mmFile_;
}

// 返回映射文件的长度（Content-Length）
size_t HttpResponse::FileLen() const {
    return mmFileStat_.st_size;
}

// 当需要直接返回错误信息（非静态错误页）时，生成简单的 HTML 错误页面并追加到 buff
void HttpResponse::ErrorContent(Buffer& buff, std::string message) {
    std::string body;
    std::string status;
    body += "<html><title>Error</title>";         // 构建 HTML 页面标题
    body += "<body bgcolor=\"ffffff\">";          // 设置背景色
    if (CODE_STATUS.count(code_) == 1) {          // 若 status 存在于映射表中
        status = CODE_STATUS.find(code_)->second; // 取出状态描述
    } else {
        status = "Bad Request"; // 默认状态描述
    }
    body += std::to_string(code_) + " : " + status + "\n"; // 在 body 中添加状态行
    body += "<p>" + message + "</p>";                      // 添加错误信息
    body += "<hr><em>TinyWebServer</em></body></html>";    // 结束 HTML

    // 先写 Content-length 头，然后写两个 CRLF 表示头部结束
    buff.Append("Content-length: " + std::to_string(body.size()) + "\r\n\r\n");
    // 最后把 body 内容写入 buff
    buff.Append(body);
}

// 返回 HTTP 状态码
int HttpResponse::Code() const {
    return code_;
}

// 添加状态行（HTTP/1.1 200 OK）到缓冲区
void HttpResponse::AddStateLine_(Buffer& buff) {
    std::string status; // 用来保存状态短语
    if (CODE_STATUS.count(code_) == 1) {
        // 若 code_ 在 CODE_STATUS 表中有映射
        status = CODE_STATUS.find(code_)->second;
    } else {
        // 否则将 code 视为 400（Bad Request）
        code_ = 400;
        status = CODE_STATUS.find(400)->second;
    }
    // 拼接并追加状态行，末尾以 CRLF 结束
    buff.Append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
}

// 添加响应头（Connection、Content-type 等）
void HttpResponse::AddHeader_(Buffer& buff) {
    buff.Append("Connection: ");                           // 添加 Connection 字段键
    if (isKeepAlive_) {                                    // 如果是长连接
        buff.Append("keep-alive\r\n");                     // 指明 keep-alive
        buff.Append("keep-alive: max=6, timeout=120\r\n"); // 自定义 keep-alive 属性（非标准写法也可）
    } else {
        buff.Append("close\r\n"); // 否则连接关闭
    }
    // 添加 Content-type 头，调用 GetFileType_() 推断 MIME 类型
    buff.Append("Content-type: " + GetFileType_() + "\r\n");
}

// 添加响应体相关信息并把真实文件映射到内存（mmap）
void HttpResponse::AddContent_(Buffer& buff) {
    // 以只读方式打开目标文件
    int srcFd = open((srcDir_ + path_).data(), O_RDONLY);
    if (srcFd < 0) {                          // 打开失败（文件不存在或权限不足等）
        ErrorContent(buff, "File NotFound!"); // 构造简单的错误内容
        return;                               // 返回，不继续后续映射逻辑
    }

    /* 将文件映射到内存提高文件的访问速度
       MAP_PRIVATE 建立一个写入时拷贝的私有映射 */
    LOG_DEBUG("file path %s", (srcDir_ + path_).data()); // 日志输出当前处理的文件路径

    // // mmap 会返回映射的起始地址（void*），这里将其转换为 int* 再检查结果
    // int* mmRet = (int*)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    // if (*mmRet == -1) {                       // （问题点：不正确的错误检查）
    //     ErrorContent(buff, "File NotFound!"); // 若映射失败，则返回错误
    //     return;
    // }
    // mmFile_ = (char*)mmRet; // 将映射地址保存为 char*，便于发送

    // mmap 返回 void*，失败时为 MAP_FAILED
    void* mmRet = mmap(nullptr, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if (mmRet == MAP_FAILED) {
        close(srcFd);
        ErrorContent(buff, "File NotFound!");
        return;
    }
    mmFile_ = static_cast<char*>(mmRet); // 保存映射地址

    close(srcFd); // 关闭文件描述符（映射后可关闭 fd）
    // 添加 Content-length 头并在头部后添加额外的 CRLF 分隔头与 body
    buff.Append("Content-length: " + std::to_string(mmFileStat_.st_size) + "\r\n\r\n");
}

// 如果 code_ 对应有错误页面映射（CODE_PATH 中存在），替换 path_ 为错误页路径并更新 mmFileStat_
void HttpResponse::ErrorHtml_() {
    if (CODE_PATH.count(code_) == 1) {
        // 找到对应的错误页面路径，例如 "/404.html"
        path_ = CODE_PATH.find(code_)->second;
        // 更新 mmFileStat_ 为错误页的 stat 信息
        stat((srcDir_ + path_).data(), &mmFileStat_);
    }
}

// 根据 path_ 的后缀返回对应的 MIME 类型（例如 ".html" -> "text/html"）
std::string HttpResponse::GetFileType_() {
    /* 判断文件类型 */
    std::string::size_type idx = path_.find_last_of('.'); // 找到最后一个 '.' 的位置
    if (idx == std::string::npos) {                       // 如果没找到扩展名
        return "text/plain";                              // 默认返回 text/plain
    }
    std::string suffix = path_.substr(idx); // 取出后缀，例如 ".html"
    if (SUFFIX_TYPE.count(suffix) == 1) {
        return SUFFIX_TYPE.find(suffix)->second; // 如果映射表中有该后缀，返回对应 MIME
    }
    return "text/plain"; // 否则返回 text/plain 作为兜底
}
