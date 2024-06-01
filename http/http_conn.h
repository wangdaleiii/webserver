#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, // 解析请求行
        CHECK_STATE_HEADER,          // 解析首部行
        CHECK_STATE_CONTENT          // 解析实体主体
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,       // 资源错误（文件是目录）
        NO_RESOURCE,       // 没有对应的资源
        FORBIDDEN_REQUEST, // 请求被禁止（没有读权限）
        FILE_REQUEST,      // 成功的请求到了文件
        INTERNAL_ERROR,    // 意外错误（无法正确解析HTTP文件）
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0, // 成功解析一行数据
        LINE_BAD,    // 当前行格式不正确
        LINE_OPEN    // 当前行未接受完成
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 根据传入的参数初始化 m_sockfd、m_address、doc_root、m_TRIGMode、m_close_log
    // 初始化 sql_user、sql_user、sql_user
    // 将成员变量设置为空
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);

    // 若 real_close = true, 则关闭 m_sockfd
    // 并从m_epollfd中移除, m_user_count减1
    void close_conn(bool real_close = true);

    // 从接收缓冲区读取数据，解析HTTP
    // 根据m_url将需要显示的文件路径放在 m_real_file 中，并映射到m_file_address处
    // 根据对应的HTTP状态码，组成HTTP数据包
    void process();

    // 读取网络数据，LT模式下只读取一次，ET模式下使用while循环读取
    bool read_once();

    // bytes_to_send为0，则将 m_sockfd 设置为 EPOLLIN ，调用init函数，返回
    // 否则调用 writev 持续发送数据，直到发送完成
    bool write();

    sockaddr_in *get_address()
    {
        return &m_address;
    }

    // 从传入的connection_pool中运行 SELECT username,passwd FROM user
    // 将用户名-密码对放到user中
    void initmysql_result(connection_pool *connPool);
    int timer_flag; // 初始化为0
    int improv;     // 初始化为0

private:
    void init();                       // 初始化各个成员变量
    HTTP_CODE process_read();          // 从接收缓冲区不断读取数据，并调用parse函数解析，do_request()函数处理
    bool process_write(HTTP_CODE ret); // 根据传入的 HTTP_CODE，组成HTTP数据包

    // 解析http请求行
    // 请求方法记录到 m_method 中(只处理 GET 和 POST)，如果出现过POST，cgi = 1
    // URL地址记录到 m_url 中，只保留'/'所在的位置，‘/’则改为 "/judge.html"
    // 版本信息记录到 m_version 中
    // 设置 m_check_state = CHECK_STATE_HEADER
    // 成功返回 NO_REQUEST ，失败返回 BAD_REQUEST
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);      // 解析http请求的一个头部信息，获得是否保持连接、主机名、实体主体长度
    HTTP_CODE parse_content(char *text);      // 若http请求被完整读入，则将实体主体内容放入m_string中
    HTTP_CODE do_request();                   // 根据m_url将需要显示的文件路径放在 m_real_file 中，并映射到m_file_address处

    char *get_line() { return m_read_buf + m_start_line; }; // 返回当前行的首地址
    LINE_STATUS parse_line();                               // 从接收缓冲区中解析出一行数据，并将回车换行字符改为空

    void unmap(); // 取消内存映射操作

    bool add_response(const char *format, ...);          // 格式化输出信息到 m_write_buf 缓冲区中
    bool add_content(const char *content);               // 缓冲区添加实体主体
    bool add_status_line(int status, const char *title); // 缓冲区添加 版本、状态码、短语
    bool add_headers(int content_length);                // 缓冲区添加 Content-Length、Connection 和 回车换行
    bool add_content_type();                             // 缓冲区添加 Content-Type:text/html
    bool add_content_length(int content_length);         // 缓冲区添加 Content-Length 字段
    bool add_linger();                                   // 缓冲区添加 Connection 字段
    bool add_blank_line();                               // 缓冲区添加回车换行

public:
    static int m_epollfd;    // epoll对应的socket
    static int m_user_count; // 连接的用户数
    MYSQL *mysql;            // 在initmysql_result()中在连接池中获取连接
    int m_state;             // 读为0, 写为1，初始化为0

private:
    int m_sockfd;          // 由构造函数初始化，客户端对应的socket
    sockaddr_in m_address; // 由构造函数初始化，类里面没有用到


    char m_read_buf[READ_BUFFER_SIZE]; // 接收数据缓冲区
    long m_read_idx;                   // 接收数据缓冲区指针，下一次需要接收的首地址

    long m_checked_idx;                // 接收区解析数据指针，parse_line()下一次开始解析的地址
    int m_start_line;                  // 读取行的首地址

    int cgi; // 判断POST是否出现，出现为1

    METHOD m_method; // 请求行的措施
    char *m_url;     // 请求行的URL
    char *m_version; // 请求行的版本号
    char *m_string;  // 存储请求头数据

    bool m_linger;         // 链接是否需要释放
    long m_content_length; // 实体主体的长度
    char *m_host;          // 主机名

    CHECK_STATE m_check_state; // HTTP解析状态机的状态位

    char *doc_root;                 // 文档的根目录
    char m_real_file[FILENAME_LEN]; // 相应的HTML文件目录
    struct stat m_file_stat;        // 获取对应URL的文件信息
    char *m_file_address;           // HTML映射区域的起始地址

    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_write_idx;                     // 写缓冲区指针
    
    int bytes_to_send;                   // 服务器需要回复给客户端的字节数
    int bytes_have_send;                 // 服务器已经发送的字节数

    struct iovec m_iv[2]; // 数据发送缓冲区
    int m_iv_count;       // 数据发送缓冲区数量

    map<string, string> m_users; // 类里面没用到
    int m_TRIGMode;              // TRIGMode 为 1 时设置 EPOLLET (使用边缘触发模式)
    int m_close_log;             // 由构造函数初始化，类里面没有用到

    // 由构造函数初始化
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
