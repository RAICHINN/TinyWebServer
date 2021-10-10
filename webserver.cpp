#include "webserver.h"

WebServer::WebServer()
{
    //用户类：http_conn类对象，一次性创建MAX_FD个 （太浪费空间，能否优化？）
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200); //获取当前工作目录的绝对路径，复制到server_path所指的内存空间中
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //创建用户资料数据内存空间，用于定时器关闭非活动连接 （能否优化？？）
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}
/* 设置触发模式
ET = 1;
LT = 0;
 */
void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

//日志记录
void WebServer::log_write()
{
    if (0 == m_close_log) //是否关闭日志标志
    {
        //初始化日志
        if (1 == m_log_write) //m_log_write 设为1， 异步
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    //创建数据库连接池，是个单例模式
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0); //创建监听fd，PF_INET:ip4; SOCK_STREAM:流服务
    assert(m_listenfd >= 0);

    //优雅关闭连接？？？
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address; //创建一个ip4 socket地址
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY); //htonl: host to network long；INADDR_ANY表示不关心IP，即服务器可被任意网访问
    address.sin_port = htons(m_port); //htons: host to network short

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)); //设置m_listenfd为SO_REUSEADDR:允许重用本地地址和端口
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address)); //命名socket，将m_listenfd与address绑定
    assert(ret >= 0);
    ret = listen(m_listenfd, 5); //监听m_listenfd
    assert(ret >= 0);
    
    utils.init(TIMESLOT); //设置定时时间

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5); //返回的m_epollfd将用作其他所有epoll系统调用的第一个参数，以指定要访问的事件表
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode); //往事件表注册监听fd，为什么走的是定时器类的方法
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd); //创建一对管道m_pipefd，用于信号通信
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]); //设置管道的m_pipefd[1]为非阻塞，为什么？
    utils.addfd(m_epollfd, m_pipefd[0], false, 0); //注册管道的读事件

    utils.addsig(SIGPIPE, SIG_IGN); //？？？ 设置SIGPIPE的信号处理函数为SIG_IGN ？？？
    utils.addsig(SIGALRM, utils.sig_handler, false); //设置信号SIGALRM的信号函数
    utils.addsig(SIGTERM, utils.sig_handler, false); //设置信号SIGTERM的信号函数
    //每隔TIMESLOT时间触发SIGALRM信号
    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;   // 实际用管道u_pipefd
    Utils::u_epollfd = m_epollfd; //用于定时器超时删除
}

/* 主线程中的定时器设置函数timer
参数：
connfd: 客户连接的文件描述符
client_address: 客户的socket地址结构体
 */
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    //初始化http_conn类
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer; //创建定时器
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func; //设置定时器回调函数
    time_t cur = time(NULL); //获取系统的绝对时间
    timer->expire = cur + 3 * TIMESLOT; //设置绝对超时时间
    users_timer[connfd].timer = timer; //把这个定时器信息保存到用户资料，到时检测超时时会用到
    utils.m_timer_lst.add_timer(timer); //把定时器加到定时器链表中
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

//客户关闭连接，移除计时器
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

//该函数负责接收监听的连接，并为用户设置超时非活动的定时器
bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode) //监听fd触发模式：水平模式
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        //连接成功，设置定时器
        timer(connfd, client_address);
    }

    else //监听fd触发模式：边缘触发模式
    {
        while (1) //阻塞，
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}
/* 主线程中的接收信号后的处理函数，运行结果保持到参数中
timeout: 是否超时
stop_server: 是否停止服务器
*/
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    
    //从管道读端读取信号，成功时返回字节数，失败时返回-1
    //正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0); 
    if (ret == -1) 
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        //处理信号值对应的逻辑
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM: //SIGALRM: 定时器超时
            {
                timeout = true;
                break;
            }
            case SIGTERM: //SIGTERM: 程序结束信号
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}
/* 处理读事件函数
sockfd: 要处理的fd
 */
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer; //取到该客户的定时器

    //reactor模式
    if (1 == m_actormodel)
    {
        if (timer) //给定时器加时间
        {
            adjust_timer(timer);
        }

        //将该读事件加入请求队列
        m_pool->append(users + sockfd, 0); //users + sockfd ？？？

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor模式
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}
/* 处理写事件函数
sockfd: 
 */
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //Reactor模式
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //Proactor 模式
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}
//epoll事件循环
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1); //等待事件发生，从m_pollfd复制到events中
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        //轮询文件描述符
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            //处理异常事件
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //管道接收到信号，调用信号处理函数
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) 
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN) //是读事件
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT) //是写事件
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout) //定时输出日志
        {
            utils.timer_handler(); //？？？

            LOG_INFO("%s", "timer tick"); //？？？未完善

            timeout = false;
        }
    }
}