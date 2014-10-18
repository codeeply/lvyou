#include "ly_common.h"

int64_t ly_get_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t t0 = tv.tv_sec;
    int64_t t1 = tv.tv_usec;
    return t0 * 1000 + t1 / 1000;
}

void ly_set_nodelay(int fd) {
    int opt_val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt_val, sizeof(int)) != 0) {
        ly_perror("setsockopt error"); 
    }
}

void ly_set_nonblock(int fd) {
    int flag;
    if ((flag = fcntl(fd, F_GETFL, 0)) < 0) {
        ly_perror("fcntl get error");
    }
    if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) < 0) {
        ly_perror("fcntl set error");
    }
}

int ly_connect_server(ly_ip_addr_t *ip_addr)
{
    int fd;
    struct sockaddr_in sa;

    sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(ip_addr->ip);
	sa.sin_port = htons(ip_addr->port);
	bzero(sa.sin_zero, 8);
        
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        ly_perror_and_exit("socket error");
    }
 
    //ly_set_nodelay(fd);
    //ly_set_nonblock(fd);

    if (connect(fd, (struct sockaddr *)&sa, sizeof(struct sockaddr)) == -1
        && errno != EINPROGRESS)
    {
        ly_perror_and_exit("connect error");
    }

    return fd;
}
