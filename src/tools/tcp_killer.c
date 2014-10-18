#include "ly_common.h"
#include "ly_file.h"
#include "ly_request.h"

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

	if (listen(fd, 1024) == -1) {
        close(fd);
		ly_perror_and_return("listen error", -1);
	}

    return fd;
}

#define N (1 << 24)
char buffer[N];

void do1(int fd) {
    int n, fd1, ret;

    n = read(fd, buffer, N);

    ly_ip_addr_t ia = {"127.0.0.1", 8000};
    fd1 = ly_connect_server(&ia);

    ret = write(fd1, buffer, n);

    if (ret == -1 || ret != n) {
        ly_perror_and_exit("write error");
    }

    n = read(fd1, buffer, N);
    ///sleep(1);
    fprintf(stderr, buffer, n);

    ret = write(fd, buffer, n);
    if (ret == -1 || ret != n) {
        ly_perror_and_exit("write1 error");
    }
}

void test(int fd) {
    socklen_t s;
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 30;
    int z =getsockopt(fd,
            SOL_SOCKET,
            SO_LINGER,
            &so_linger,
            &s);
    if ( z )
        perror("setsockopt(2)");
    printf("%d %d\n", so_linger.l_onoff, so_linger.l_linger);
}
void do2(int fd) {
    int n, fd1, ret;

    n = read(fd, buffer, N);
    n--;

    ly_ip_addr_t ia = {"127.0.0.1", 8000};
    fd1 = ly_connect_server(&ia);

    ret = write(fd1, buffer, n);

    //ret = write(fd1, buffer, 1);

    if (ret == -1 || ret != n) {
        ly_perror_and_exit("write error");
    }

    char *http = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    ret = write(fd, http, strlen(http));
    test(fd);
    sleep(1);
    printf("ret = %d\n", ret);
    close(fd);
}

int main(int argc, char **argv) {
    int fd, listen_fd, cmd;

    if ((listen_fd = ly_server_init_listen_fd("0.0.0.0", 8888)) == -1) {
        ly_info_and_exit("ly_server_init_listen_fd error");
    }

    while (scanf("%d", &cmd) != EOF) {
        switch (cmd) {
        case 0:
            break;
        case 1:
            fd = ly_server_listen_and_accept(listen_fd);
            fprintf(stderr, "fd = %d, cmd = 1", fd);
            do1(fd);
            break;
        case 2:
            fd = ly_server_listen_and_accept(listen_fd);
            fprintf(stderr, "fd = %d, cmd = 2", fd);
            do2(fd);
            break;
        default:
            break;
        }
    }
}
