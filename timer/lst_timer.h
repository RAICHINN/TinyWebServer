#ifndef LST_TIMER
#define LST_TIMER

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

#include <time.h>
#include "../log/log.h"

//连接资源结构体成员需要用到的定时器类
//需要前向声明
class util_timer;

//连接资源
struct client_data
{
    //客户端socket地址
    sockaddr_in address;
    //socket文件描述符
    int sockfd;
    //定时器
    util_timer *timer;
};

//定时器链表的结点类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire; //超时时间（系统的绝对时间）
    
    void (* cb_func)(client_data *); //回调函数
    client_data *user_data; //用户的数据
    util_timer *prev; //前向定时器
    util_timer *next; //后继定时器
};

//定时器容器类，项目中的定时器容器是带头尾结点的升序双向链表
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();

private:
    void add_timer(util_timer *timer, util_timer *lst_head); //内部方法，用于调整链表中的内部结点
    //头尾结点
    util_timer *head;
    util_timer *tail;
};

//定时器类最终用这个
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    //初始化函数
    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
