#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>

#include "ly_util.h"
#include "ly_file.h"
#include "ly_request.h"

#define MAX_LISTEN_BACKLOG 14096

void set_nodelay(int fd) {
    int opt_val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt_val, sizeof(int)) != 0) {
        ly_perror("setsockopt error"); 
    }
}

void set_nonblock(int fd) {
    int flag;
    if ((flag = fcntl(fd, F_GETFL, 0)) < 0) {
        ly_perror("fcntl get error");
    }
    if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) < 0) {
        ly_perror("fcntl set error");
    }
}

int init_listen_fd(ly_request_t *r, const char *ip, int port) {
    int fd;
    struct sockaddr_in sa;
    struct epoll_event ev;

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        ly_perror_and_return("socket error", -1);
    }

    set_nonblock(fd);

    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(ip);
    sa.sin_port = htons(port);
    bzero(sa.sin_zero, 8);

    if (bind(fd, (struct sockaddr*)&sa, sizeof(struct sockaddr)) == -1) {
        close(fd);
        ly_perror_and_return("bind error", -1);
    }

    if (listen(fd, MAX_LISTEN_BACKLOG) == -1) {
        close(fd);
        ly_perror_and_return("listen error", -1);
    }

    memset(r, 0, sizeof(ly_request_t));
    r->fd = fd;

    return 0;
}

int add_listen_fd(int epoll_fd, ly_request_t *r) {
    struct epoll_event ev;

    ev.data.ptr = r;
    // 这里不能加EPOLLET
    ev.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, r->fd, &ev) == -1) {
        ly_perror_and_return("epoll_ctl add", -1);
    }

    return 0;
}

int del_listen_fd(int epoll_fd, ly_request_t *r) {
    struct epoll_event ev;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, r->fd, &ev) == -1) {
        ly_perror_and_return("epoll_ctl del", -1);
    }

    return 0;
}

int accept_and_add_events(int32_t tid, int listen_fd, int epoll_fd) {
    struct sockaddr_in sa;
    struct epoll_event ev;
    ly_request_t *r;
    socklen_t size = sizeof(struct sockaddr);
    int i, fd, tmin;

    pthread_mutex_lock(&g_accept_mutex);
    fd = accept(listen_fd, (struct sockaddr *)&sa, &size);
    pthread_mutex_unlock(&g_accept_mutex);
    if (fd == -1) {
        ly_perror_and_return("accept error", -1);
    }

    if ((r = ly_request_new()) == NULL) {
        fprintf(stderr, "request_new fail\n");
        close(fd);
        return -1;
    }
    memset(r, 0, sizeof(ly_request_t));
    r->fd = fd;

    set_nonblock(fd);
    set_nodelay(fd);
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = r;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        ly_request_free(r);
        close(fd);
        ly_perror_and_return("epoll_ctl error", -1);
    }

    return 0;
}

void handle_in_event(ly_request_t *r) {
    static int32_t idx = 0;
    ssize_t nb;
    int i;

    while (1) {
        nb = read(r->fd, r->rbuffer, 1);
        if (nb == -1 && errno == EINTR) {
            ly_debug_info("fd = %d EINTR", r->fd);
            continue;
        }
        break;
    }
    if (nb == 0) {
        // 返回0表示对方已经关闭
        r->error = 1;
    } else if (nb == -1) {
        if (errno != EAGAIN) {
            ly_perror("read error");
            r->error = 1;
        } else {
            ly_debug_info("fd = %d EAGAIN", r->fd);
        }
    } else if (r->rbuffer[0] == 1) {
        assert(r->in_request == 0);
        r->in_request = 1;

        r->wb_size = ly_file_get_next_and_fill(r->wbuffer,
                LY_REQUEST_WBUFFER_SIZE);
        r->wb_offset = 0;
    } else {
        r->error = 1;
    }
}

void handle_out_event(ly_request_t *r) {
    if (r->error || !r->in_request) {
        return;
    }

    ssize_t nb;
    size_t rm = r->wb_size - r->wb_offset;

    while (1) {
        nb = write(r->fd, r->wbuffer + r->wb_offset, rm);
        if (nb == -1 && errno == EINTR) {
            ly_debug_info("write EINTR");
            continue;
        }
        break;
    }
    if (nb == -1) {
        if (errno != EAGAIN) {
            ly_perror("write error");
            r->error = 1;
        } else {
            ly_debug_info("fd = %d EAGAIN", r->fd);
        }
    } else {
        r->wb_offset += nb;
        if (rm == nb) {
            r->in_request = 0;
        }
    }
}

void *pthread_run_loop(void *arg) {
    ly_request_t *lr = arg, *r;
    int epoll_fd, listen_fd;
    struct epoll_event *events;
    int i, n, listen_fd_deleted;
    int32_t thread_index = __sync_fetch_and_add(&g_thread_index, 1);
    ly_pthread_setaffinity(thread_index);

    events = calloc(MAX_EVENTS_NUM, sizeof(struct epoll_event));
    if (events == NULL) {
        ly_info_and_exit("calloc error");
    }

    epoll_fd = epoll_create(MAX_EVENTS_NUM);
    if (epoll_fd == -1) {
        ly_perror_and_exit("epoll_create error");
    }

    listen_fd = lr->fd;
    if (add_listen_fd(epoll_fd, lr) == -1) {
        ly_info_and_exit("add_listen_fd error");
    }
    listen_fd_deleted = 0;

    for ( ; ; ) {
        if (listen_fd_deleted &&
                g_conn_cnt[thread_index] < g_conn_num_per_thread)
        {
            listen_fd_deleted = 0;
            add_listen_fd(epoll_fd, lr);
        }
        n = epoll_wait(epoll_fd, events, MAX_EVENTS_NUM, -1);

        for (i = 0; i < n; ++i) {
            r = events[i].data.ptr;
            if (events[i].events & EPOLLIN) {
                if(r->fd == listen_fd) {
                    // 一次返回有可能多个listen_fd或读写处理中有fd关闭.
                    if (g_conn_cnt[thread_index] == g_conn_num_per_thread) {
                        if (!listen_fd_deleted) {
                            del_listen_fd(epoll_fd, lr);
                            listen_fd_deleted = 1;
                        }
                        continue;
                    }
                    if (accept_and_add_events(thread_index,
                                listen_fd,
                                epoll_fd)
                            == -1)
                    {
                        // 在单核虚拟机上可能由于线程调度问题,
                        // 有一个线程会在这里这里转好几圈
                        ly_debug_info("idx
                                =
                                %d
                                %accept_and_add_events
                                %fail",
                                thread_index);
                    }
                    else
                    {
                        g_conn_cnt[thread_index]++;
                        if
                            (g_conn_cnt[thread_index]
                             ==
                             g_conn_num_per_thread)
                            {
                                del_listen_fd(epoll_fd,
                                        lr);
                                listen_fd_deleted
                                    =
                                    1;
                            }
                    }
                    continue;
                }
                handle_in_event(r);
            }
            if
                (events[i].events
                 &
                 EPOLLOUT)
                {
                    handle_out_event(r);
                }
            else
            {
                ly_debug_info("fd
                        =
                        %d
                        %unwritable",
                        %r->fd);
            }
            if
                (r->error)
                {
                    // 如果读缓冲区里还有数据时关闭则发送reset包
                    ly_debug_info("close
                            fd
                            =
                            %d",
                            %r->fd);
                    close(r->fd);
                    ly_request_free(r);
                    g_conn_cnt[thread_index]--;
                }
        }
    }

    free(events);
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    if (init_listen_fd(r, "0.0.0.0", 60530) == -1) {
        ly_info_and_exit("init_listen_fd error");
    }

    return 0;
}
