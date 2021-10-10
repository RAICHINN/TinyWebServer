#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}
//将目标定时器添加到链表中，使得链表为升序
void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    //若当前链表中只有头尾节点，直接插入
    if (!head)
    {
        head = tail = timer;
        return;
    }
    //如果新的定时器超时时间小于当前头部结点，在链表头部添加定时器
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    //调用私有成员，调整内部结点？
    add_timer(timer, head);
}

//调整定时器，任务发生变化时，调整定时器在链表中的位置
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    util_timer *tmp = timer->next;
    //被调整的定时器在链表尾部，定时器值仍小于下一个定时器值，不调整
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    //被调整定时器是头结点，将定时器取出，重新插入
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    //被调整定时器在内部，将定时器取出，重新插入
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

//删除定时器
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    //链表中只有一个定时器时
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    //要删除的定时器为头结点
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    //要删除的定时器为尾结点
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    //要删除的定时器在链表内部
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}
//定时任务处理函数
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    //获取当前时间
    time_t cur = time(NULL);
    util_timer *tmp = head;
    //遍历定时器链表
    while (tmp)
    {
        //链表容器为升序排列
        //当前时间小于定时器的超时时间，后面的定时器也没有到期，不操作
        if (cur < tmp->expire)
        {
            break;
        }
        
        //当前定时器到期，则调用回调函数，执行定时事件
        tmp->cb_func(tmp->user_data);
        //将处理后的定时器从链表容器中删除，并重置头结点
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head; //？？？这行代码是否必要
    }
}

//私有成员，被公有成员add_timer和adjust_time调用
//主要用于调整链表内部结点
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    while (tmp) //prev不是尾结点，用双指针寻找合适位置
    {
        //timer插到prev和tmp的中间
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        //两指针后移
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp) //prev是尾结点，直接在其后面插入
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

//设置超时时间
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* 
将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
epollfd: epoll内核事件表的fd
fd: 监听的fd
one_shot: 是否注册EPOLLONESHOT事件
TRIGMode: 设置触发模式 1为设置边缘触发模式
*/
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; //EPOLLIN: 有数据来临时； EPOLLET：边缘触发模式；EPOLLRDHUP: 读关闭时
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event); //EPOLL_CTL_ADD: 往事件表中注册fd上的事件
    setnonblocking(fd); //设置fd为非阻塞
}

//信号处理函数，仅仅通过管道发送信号值，不处理信号对应的逻辑，缩短yi
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    //将信号值从管道写端写入，传输字符类型，而非整形
    send(u_pipefd[1], (char *)&msg, 1, 0); //！！没有对非阻塞返回值进行处理，如果阻塞是不是意味着这一次定时器事件失效了
    //将原来的errno赋值为当前的errno
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    //创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT); //设置定时器，时间到时触发SIGALRM信号
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
//定时器回调函数
void cb_func(client_data *user_data)
{
    //删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    //关闭文件描述符
    close(user_data->sockfd);
    //用户连接数减1
    http_conn::m_user_count--;
}
