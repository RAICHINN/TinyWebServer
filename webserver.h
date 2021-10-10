#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //定时器超时时间的最小单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    //外部main调用的方法
    void thread_pool(); //线程池
    void sql_pool(); //数据库
    void log_write(); //写日志
    void trig_mode(); //触发模式
    void eventListen(); //监听
    void eventLoop(); //运行

    //内部函数调用的方法
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd); //处理读事件函数
    void dealwithwrite(int sockfd); //处理写事件函数

public:
    //基础
    int m_port; //端口号
    char *m_root;
    int m_log_write;
    int m_close_log;
    int m_actormodel; //事件处理模式：1: Reactor 否则 Proactor

    int m_pipefd[2]; //管道 用于信号的传递
    int m_epollfd; 
    http_conn *users;

    //数据库相关
    connection_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool; //线程池
    int m_thread_num; //线程数量

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER]; //epoll事件表

    int m_listenfd; //监听的文件描述符
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode; //监听的触发模式
    int m_CONNTrigmode;

    //定时器相关
    client_data *users_timer; //用户数据类成员，用于关闭非活动链接
    Utils utils; //定时器管理类
};
#endif
