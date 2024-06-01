#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
	MYSQL *GetConnection();				 // 获取数据库连接
	bool ReleaseConnection(MYSQL *conn); // 释放连接
	int GetFreeConn();					 // 获取连接
	void DestroyPool();					 // 销毁所有连接

	// 获取数据库连接池的单例
	static connection_pool *GetInstance();

	// 1.初始化m_url、m_Port、m_User、m_PassWord、m_DatabaseName、m_close_log、m_MaxConn
	// 2.初始化 MaxConn 个 MySQL连接，添加到connList中
	// 3.初始化信号量，设置资源数为 MaxConn 个
	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

private:
	// 设置当前已使用的连接数、当前空闲的连接数为0
	connection_pool();
	~connection_pool();

	int m_MaxConn;			// 最大连接数
	int m_CurConn;			// 当前已使用的连接数
	int m_FreeConn;			// 当前空闲的连接数
	list<MYSQL *> connList; // 连接池

	sem reserve; // 信号量封装类
	locker lock; // 保护可修改的成员变量

public:
	string m_url;		   // 主机地址
	string m_Port;		   // 数据库端口号
	string m_User;		   // 登陆数据库用户名
	string m_PassWord;	   // 登陆数据库密码
	string m_DatabaseName; // 使用数据库名
	int m_close_log;	   // 日志开关
};

//-----------------------------------------------------

class connectionRAII
{

public:
	// 从连接池中获取一个连接到SQL
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();

private:
	MYSQL *conRAII;			   // RAII对象 获取的MYSQL指针
	connection_pool *poolRAII; // RAII对象 所在的数据库连接池
};

#endif
