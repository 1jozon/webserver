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