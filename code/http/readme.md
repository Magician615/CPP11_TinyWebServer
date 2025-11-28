# HTTP

## 1.HttpRequest 的作用
| 阶段           | 任务                          |
| ------------ | --------------------------- |
| Buffer 中有数据时 | 调用 `HttpRequest.parse()`    |
| 分析 HTTP 协议   | 读取请求方式、URL 等                |
| 用户输入（登录/注册）  | 使用 `UserVerify()` 连接数据库验证   |
| 解析完成后        | 返回给 `HttpConn`，用于构造 HTTP 响应 |


## 2.`std::map` 的两种访问方式
* `find(key)->second`：
```
auto it = header_.find("Connection");
if (it != header_.end()) {
    return it->second == "keep-alive";
}
```
✔ 安全 —— 因为在调用 find() 之前先判断键是否存在  
✔ 不会修改 `map`  
✔ 推荐用于查询
* `operator[](key)`（即 `map[key]`）：
```
return header_["Connection"] == "keep-alive";
```
⚠ 风险：如果 `"Connection"` 不存在，会 自动插入一个空字符串作为新元素！  
⚠ 会修改容器内容  
⚠ 不适合用于只检查是否存在 / 不想改 `map` 的情况

## 3.为什么用 `const char CRLF[]` 而不是 `std::string`？
| 写法                               | 类型       | 适用于                        |
| -------------------------------- | -------- | -------------------------- |
| `const char CRLF[] = "\r\n";`    | C 风格字符数组 | 最适合配合 `std::search` 使用 |
| `const std::string CRLF="\r\n";` | C++ 字符串  | 不适合用在 C 字符串指针比较            |

## 4.`std::search()` 怎么用？
```
Iter search(Iter1 first1, Iter1 last1, Iter2 first2, Iter2 last2);
```
查找 `[first2, last2)` 这个“子序列”出现在 `[first1, last1)` 里的位置。  
| 参数                       | 说明                   |
| ------------------------ | -------------------- |
| `buff.Peek()`            | 当前可读数据开头指针           |
| `buff.BeginWriteConst()` | 当前可读数据结尾（写入位置）       |
| `CRLF`                   | `"\r\n"` 的起始地址       |
| `CRLF + 2`               | `"\r\n"` 的结束位置（长度 2） |
`"CRLF + 2"` 是因为长度为 2：`'\r'` 和 `'\n'`

## 5.获取当前行（不含 `\r\n`）
```
std::string line(buff.Peek(), lineEnd);
```
用 `[Peek(), lineEnd)` 构造字符串，刚好去掉了 `\r\n`

## 6.解析请求行中的 `std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");`
`std::regex` 表示一个正则表达式对象（pattern）。  
字符串 `^([^ ]*) ([^ ]*) HTTP/([^ ]*)$` 的含义：
* `^` / `$`：行首和行尾锚点 → 整行必须完全匹配（很重要）。
* `([^ ]*)`：第1个捕获组，匹配 0 个或多个不是空格的字符（即 method，如 `GET`）。
* `' '`：一个字面空格（请求行字段间是空格分隔）。
* 第二个 `([^ ]*)`：第2组，匹配 path（不含空格）。
* `HTTP/`：字面文本 `HTTP/`。
* `([^ ]*)`：第3组，匹配版本号（例如 `1.1`）。  
注意：默认的正则语法是 ECMAScript（`std::regex_constants::ECMAScript`）。

## 7.`std::smatch subMatch`
* `std::smatch` 是 `std::match_results<std::string::const_iterator>` 的别名，专门用于对 `std::string` 的匹配结果。
* 它像一个数组：`subMatch[0]` 是整个匹配（整个请求行），`subMatch[1]`、`subMatch[2]`、`subMatch[3]` 分别是三个捕获组的内容。
* `subMatch[i]` 的类型可以隐式转换为 `std::string`，因此 `method_ = subMatch[1];` 是合法的。

## 8.`if (regex_match(line, subMatch, patten)) { ... }`
`std::regex_match` 的作用：尝试用正则模式匹配整个字符串 `line`。
* 返回值是 `bool`：匹配则返回 `true`，不匹配返回 `false`。
* 当 `true` 时，`subMatch` 被填充：`subMatch[0]`...`subMatch[n]` 可读取。  
注意 `regex_match` 与 `regex_search` 的区别：
* `regex_match` 要求整个序列被模式匹配（等同于模式前后加 `^` 和 `$`）。
* `regex_search` 查找子串是否能匹配模式（适合在长文本中检索）。

## 9.`ParseFromUrlencoded_()` 逻辑
| 字符    | 作用                 |
| ----- | ------------------ |
| `=`   | 分割 key 和 value     |
| `&`   | 一组参数结束             |
| `+`   | 转换为空格              |
| `%xx` | URL 编码转换           |
| 遍历完成后 | 把最后一组 key-value 保存 |

## 10.`UserVerify()` 逻辑
| 处理类型   | 用户存在？ | 密码是否正确 | 结果        |
| ------ | ----- | ------ | --------- |
| **登录** | ❌ 不存在 | ❌      | ❌         |
|        | ✔️ 存在 | ❌ 错    | ❌（密码错）    |
|        | ✔️ 存在 | ✔️ 对   | ✔️（登录成功）  |
| **注册** | ❌ 不存在 | -      | ✔️（允许注册）  |
|        | ✔️ 存在 | -      | ❌（用户名被占用） |

## 11.正则表达式
* `used?`  
`?`代表前面的一个字符可以出现0次或1次，说简单点就是`d`可有可无。
* `ab*c`  
`*`代表前面的一个字符可以出现0次或多次
* `ab+c`  
`+`会匹配出现1次以上的字符
* `ab{6}c`  
指定出现的次数，比如这里就是`abbbbbbc`，同理还有`ab{2,6}c`：限定出现次数为2~6次；`ab{2,}c`为出现2次以上
* `(ab)+`  
这里的括号会将ab一起括起来当成整体来匹配
* `a (cat|dog)`  
或运算：括号必不可少，可以匹配`a cat`和`a dog`
* `[abc]+`  
或运算：要求匹配的字符只能是`[]`里面的内容；`[a-z]`：匹配小写字符；`[a-zA-Z]`：匹配所有字母；`a-zA-Z0-9`：匹配所有字母和数字
* `.*`  
`.`表示匹配除换行符 `\n` 之外的任何单字符，`*`表示0次或多次。所以`.*`在一起就表示任意字符出现0次或多次。
* `^`限定开头；取反：`[^a]` 表示“匹配除了a的任意字符”，只要不是在`[]`里面都是限定开头
* `$`匹配行尾。如`^a`只会匹配行首的`a`，`a$`只会匹配行尾的`a`
* `?`表示将贪婪匹配->懒惰匹配。就是匹配尽可能少的字符。意味着匹配任意数量的重复，但是在能使整个匹配成功的前提下使用最少的重复。

## 12.为什么静态变量用 `static const unordered_map`
* 所有对象共享
* 只读，编译期初始化
* 查询时间 O(1)，效率高

## 13.HttpResponse 的作用
* 和 `HttpRequest` 对应，一个负责“解析请求”，一个负责“构造响应”。
* 读取服务器上的资源文件（比如 `/index.html`）
* 判断请求文件是否合法（是否存在、权限是否足够）
* 组装 HTTP 响应报文（状态行、响应头、响应体）
* 用 `mmap` 映射文件，提高响应效率
* 支持错误页面（400、403、404）

## 14.HttpResponse 的核心知识点
| 步骤            | 完成工作                |
| ------------- | ------------------- |
| `Init`          | 初始化响应对象             |
| `stat`          | 判断目标资源是否存在          |
| `ErrorHtml_`    | 若错误，切换到错误页面         |
| `AddStateLine_` | 写 `HTTP` 状态行          |
| `AddHeader_`    | 写响应头（连接方式、内容类型）     |
| `AddContent_`   | `mmap` 文件 → 写入 `buffer` |
| 最终发送          | 由 `epoll` + `writev` 发送 |

## 15.mmap
* `mmap` 会直接返回文件的“内存映射地址”，不需要复制文件内容，提高效率
* `read`/`write` IO 耗时比内存映射慢很多
* web 服务器高并发时用 `mmap` 更快

## 16.HttpConn 的核心知识点
| 知识点         | 解释                     |
| ----------- | ---------------------- |
| `writev`    | 一次发送多个内存块，提高效率         |
| `iovec`     | 描述每块内存地址和长度            |
| `ET`模式        | 边缘触发，必须循环读写，直到返回EAGAIN |
| `mmap`        | 把文件映射到内存，无需读入缓冲区       |
| `atomic<int>` | 无锁实现多线程共享计数            |
| `Retrieve`    | Buffer读指针前移            |
| `RetrieveAll` | Buffer清空               |

## 17.writev + iovec
| 名称       | 含义                                   |
| -------- | ------------------------------------ |
| `write`  | 一次只能写一个缓冲区                           |
| `writev` | 可以同时发送多个缓冲区（散布-聚集IO）                 |
| `iovec`  | 一个结构体，代表一段连续内存 `{iov_base, iov_len}` |
iovec[0] → HTTP响应头  
iovec[1] → 文件内容（mmap映射）
