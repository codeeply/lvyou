#include "ly_common.h"
#include "ly_file.h"
#include "ly_request.h"

#define MAX_THREAD_NUM 32
#define MAX_CONN_NUM_PER_TREAD 4096
#define MAX_EVENTS_NUM 40960
#define MAX_LISTEN_BACKLOG 40960


static int thread_num;

int ly_server_listen_and_accept(int listen_fd)
{
    int i, fd;
	struct sockaddr_in sa;
    socklen_t size = sizeof(struct sockaddr);

    for ( ; ; ) {
        fd = accept(listen_fd, (struct sockaddr *)&sa, &size);
        if (fd == -1) {
            if (errno == EINTR) {
                ly_debug_info("listen EINTR");
                continue;
            }
            if (errno == EAGAIN) {
                ly_debug_info("listen EAGAIN");
                continue;
            }
            ly_perror_and_exit("listen error");
        }
        return fd;
    }
}

int ly_server_init_listen_fd(const char *ip, int port) {
	int fd;
	struct sockaddr_in sa;
    struct epoll_event ev;

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		ly_perror_and_return("socket error", -1);
	}

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

    return fd;
}

void ly_server_handle_in_event(ly_request_t *r) {
    static int32_t idx = 0;
    ssize_t nb;
    int i, n;

    while (1) {
        nb = read(r->fd, r->rbuffer, 4);
        if (nb == -1 && errno == EINTR) {
            ly_debug_info("fd = %d EINTR", r->fd);
            continue;
        }
        if (nb != 4) {
            close(r->fd);
            r->error = 1;
            return;
        }
        n = *(int32_t *)r->rbuffer;
        break;
    }
    ly_debug_info("fd = %d recv request n = %d", r->fd, n);
    r->in_request = 1;
    r->wb_offset = 0;
    r->wb_size = ly_file_mget_next_and_fill(n, r->wbuffer,
                                            LY_REQUEST_WBUFFER_SIZE);
}

void ly_server_handle_out_event(ly_request_t *r) {
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

void *ly_server_pthread_run(void *arg) {
    ly_request_t *r = calloc(1, sizeof(ly_request_t));

    r->fd = (intptr_t)(arg);
    printf("start run fd = %d\n", r->fd);
    while (1) {
        ly_server_handle_in_event(r);
        if (r->error) break;
        ly_server_handle_out_event(r);
    }
}

int main(int argc, char **argv) {
    int i, n, port, fd, listen_fd;

    if (argc < 4) {
        ly_info_and_exit("./ly_server thread_num file port");
    }
    thread_num = atoi(argv[1]);

    if (thread_num <= 0 || thread_num > MAX_THREAD_NUM) {
        ly_info_and_exit("thread_num = %d, exceeds the range", thread_num);
    }

    signal(SIGPIPE, SIG_IGN);

    if (ly_request_init() == -1) {
        ly_info_and_exit("ly_request_init error");
    }

    if (ly_file_init(argv[2]) == -1) {
        ly_info_and_exit("ly_file_init error");
    }

    port = atoi(argv[3]);
    if ((listen_fd = ly_server_init_listen_fd("0.0.0.0", port)) == -1) {
        ly_info_and_exit("ly_server_init_listen_fd error");
    }

    pthread_t parse_tid;
    pthread_create(&parse_tid, NULL, ly_file_parse_lines, NULL);

	pthread_t *tid = calloc(thread_num, sizeof(pthread_t));
    for (i = 0; i < thread_num; ++i) {
        fd = ly_server_listen_and_accept(listen_fd);
		pthread_create(tid + i, NULL, ly_server_pthread_run,
                       (void *)(intptr_t)fd);
	}

    pthread_join(parse_tid, NULL);
    for (i = 0; i < thread_num; ++i) {
        pthread_join(tid[i], NULL);
    }

    free(tid);
}
