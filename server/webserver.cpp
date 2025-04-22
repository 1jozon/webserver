#include "webserver.h"

WebServer::WebServer()
{
    
}
// 当监听触发模式 (m_LISTENTrigmode) 为0时，只接受一个客户端连接。
// 当监听触发模式不为0时，尝试在一个循环中连续接受客户端连接，直到发生错误或达到某种条件为止。
bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode)
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
        timer(connfd, client_address);
    }

    else
    {
        while(1)
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

void WebServer::eventListen()
{
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    if (m_OPT_LINGER == 1)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    // 以监听来自任何网络接口的连接请求
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);
    utils.init(TIMESLOT);

    // 创建epoll内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;
    // 创建管道,用于定时器
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);

    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);
    alarm(TIMESLOT);

    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;

}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
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
        for(int i=0 ; i<ret; i++)
        {
            switch (signals[i])
            {
                case SIGALRM:
                {
                    timeout = true;
                    break;
                }
                case SIGTERM:
                {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;
    while(!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if( number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for(int i = 0 ; i < number ; i++)
        {
            int sockfd = events[i].data.fd;

            if ( sockfd == m_listenfd)
            {
                bool flag = dealclientdata();
                if (flag == false)
                {
                    continue;
                }
            }
            else if ( events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR) )
            {
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (flag == false)
                {
                    LOG_ERROR("%s", "dealclientdata failure");
                }
            }
            else if ( events[i].events & EPOLLIN )
            {
                dealwithread(sockfd);
            }
            else if ( events[i].events & EPOLLOUT )
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}