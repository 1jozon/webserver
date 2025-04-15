#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/epoll.h>

#define BUF_SIZE 1024
#define SMALL_BUF 100
#define EPOLL_SIZE 50
#define MAX_THREAD 100

void *request_handler(void *arg);
void send_data(FILE *fp, char *ct, char *file_name);
char *content_type(char *file);
void send_error(FILE *fp);
void error_handling(char *message);
void *thread_worker(void* arg);
int epfd, event_cnt;
struct epoll_event *ep_events;
char buf[BUF_SIZE];
int main(int argc, char *argv[]) {
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_adr, clnt_adr;
    int clnt_adr_size;
    pthread_t* m_threads = malloc(sizeof(pthread_t) * MAX_THREAD);
    
    struct epoll_event event;
    // int epfd, event_cnt;

    if (argc != 2) {
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(atoi(argv[1]));
    
    if (bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("bind() error");
    if (listen(serv_sock, 20) == -1)
        error_handling("listen() error");

    epfd = epoll_create(EPOLL_SIZE);
    ep_events = malloc(sizeof(struct epoll_event) * EPOLL_SIZE);

    event.events = EPOLLIN;
    event.data.fd = serv_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, serv_sock, &event);

    while (1) {
        event_cnt = epoll_wait(epfd, ep_events, EPOLL_SIZE, -1);
        if (event_cnt == -1) {
            puts("epoll_wait() error");
            break;
        }
        for (int i = 0; i < event_cnt; i++) {
            printf("event_cnt: %d\n", ep_events[i].data.fd);
            if (ep_events[i].data.fd == serv_sock) {
                clnt_adr_size = sizeof(clnt_adr);
                clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, &clnt_adr_size);
                printf("Connection Request: %s:%d\n", inet_ntoa(clnt_adr.sin_addr), ntohs(clnt_adr.sin_port));

                event.events = EPOLLIN;
                event.data.fd = clnt_sock;
                epoll_ctl(epfd, EPOLL_CTL_ADD, clnt_sock, &event);
            } else{
                
                int str_len = read(ep_events[i].data.fd, buf, BUF_SIZE);
                if(str_len == 0) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, ep_events[i].data.fd, NULL);
                    close(ep_events[i].data.fd);
                    printf("closed client : %d \n", ep_events[i].data.fd);
                } else {
                    int *new_clnt_sock = malloc(sizeof(int));
                    *new_clnt_sock = ep_events[i].data.fd;
                    printf("Send message clnt_sock: %d\n", ep_events[i].data.fd);
                    FILE *clnt_write;
                    clnt_write = fdopen(clnt_sock, "w");
                    // printf("Send message clnt_sock: %d\n", ep_events[i].data.fd);
                    // pthread_create(m_threads + (ep_events[i].data.fd), NULL, request_handler, &ep_events[i].data.fd);
                    // pthread_detach(m_threads[ep_events[i].data.fd]);
                    send_data(clnt_write, "text/html", "index.html");
                }
            }
            // else {
            //     // // 分配一个新的整数指针用于存储客户端套接字描述符
               
            //     // // 创建线程处理请求
            //     // pthread_create(m_threads + (*new_clnt_sock), NULL, thread_worker, new_clnt_sock);
            //     // pthread_detach(m_threads[*new_clnt_sock]);
                 
            //     int* new_clnt_sock = malloc(sizeof(int));
            //     *new_clnt_sock = ep_events[i].data.fd;
            //     request_handler(new_clnt_sock,i);
            // }
        }
    }
    close(serv_sock);
    free(ep_events);
    free(m_threads);
    return 0;
}

// 其他函数保持不变...
void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
void *request_handler(void *arg)
{
    int clnt_sock = *((int*)arg);
    printf("request_handler1");
    char req_line[SMALL_BUF];
    FILE *clnt_read;
    FILE *clnt_write;
    char method[10];
    char ct[15];
    char file_name[30];
    clnt_read = fdopen(clnt_sock, "r");
    clnt_write = fdopen(dup(clnt_sock), "w");
    fgets(req_line, SMALL_BUF, clnt_read);
    printf("request_handler2");
    if(strstr(req_line, "HTTP/") == NULL)
    {
        send_error(clnt_write);
        fclose(clnt_read);
        fclose(clnt_write);
        return NULL;
    }
    strcpy(method, strtok(req_line, " /"));
    strcpy(file_name, strtok(NULL, " /"));
    strcpy(ct, content_type(file_name));
    if(strcmp(method, "GET") != 0)
    {
        send_error(clnt_write);
        fclose(clnt_read);
        fclose(clnt_write);
        return NULL;
    }
    fclose(clnt_read);
    send_data(clnt_write, ct, file_name);
}

void send_data(FILE *fp, char *ct, char *file_name)
{
    char protocol[] = "HTTP/1.0 200 OK\r\n";
    char server[] = "Server:Linux Web Server \r\n";
    char cnt_len[] = "Content-length:2048\r\n";
    char cnt_type[SMALL_BUF];
    char buf[BUF_SIZE];
    FILE *send_file;
    sprintf(cnt_type, "Content-type:%s\r\n\r\n", ct);
    send_file = fopen(file_name, "r");
    if(send_file == NULL)
    {
        send_error(fp);
        return;
    }
    fputs(protocol, fp);
    fputs(server, fp);
    fputs(cnt_len, fp);
    fputs(cnt_type, fp);
    while(fgets(buf, BUF_SIZE, send_file) != NULL)
    {
        fputs(buf, fp);
        fflush(fp);
    }
    fflush(fp);
    fclose(fp);
}
char *content_type(char *file)
{
    char extension[SMALL_BUF];
    char file_name[SMALL_BUF];
    strcpy(file_name, file);
    strtok(file_name, ".");
    strcpy(extension, strtok(NULL, "."));

    if (!strcmp(extension, "html") || !strcmp(extension, "htm"))
        return "text/html";
    else
        return "text/plain";
}
void send_error(FILE *fp)
{
    char protocol[] = "HTTP/1.0 400 Bad Request\r\n";
    char server[] = "Server:Linux Web Server \r\n";
    char cnt_len[] = "Content-length:2048\r\n";
    char cnt_type[] = "Content-type:text/html\r\n\r\n";
    char content[] = "<html><head><title>NETWORK</title></head>"
                     "<body><font size=+5><br>发生错误！ 查看请求文件名和请求方式!"
                     "</font></body></html>";
    fputs(protocol, fp);
    fputs(server, fp);
    fputs(cnt_len, fp);
    fputs(cnt_type, fp);
    fflush(fp);
}
// // 线程池中的线程函数
// void* thread_worker(void* arg) {
//     int clnt_sock = *(int*)arg;
//     free(arg); // 释放分配给每个连接的内存
//     request_handler(&clnt_sock);
//     return NULL;
// }
