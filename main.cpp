#include "config.h"

int main(int argc, char *argv[])
{
    //指定数据库登录信息
    string user = "root";
    string passwd = "";
    string databasename = "yourdb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    //初始化server类
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);

    // 使用 Log::get_instance() 初始化一个单例LOG对象
    // 使用 init() 初始化该LOG对象
    server.log_write();

    // 初始化 m_connPool 数据库连接池
    // 调用 http_conn::initmysql_result 初始化`用户名-密码对`全局变量
    server.sql_pool();

    // 初始化 m_pool 线程池，每个线程创建worker成员函数
    server.thread_pool();

    // 指定触发方式标志位
    server.trig_mode();

    //创建Listen和epoll套接字
    server.eventListen();

    //运行事件循环
    server.eventLoop();

    return 0;
}