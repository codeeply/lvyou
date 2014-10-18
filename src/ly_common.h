#ifndef LVYOU_COMMON_H_
#define LVYOU_COMMON_H_

#include "ly_util.h"

typedef struct ly_ip_addr_s ly_ip_addr_t;

struct ly_ip_addr_s {
    char *ip;
    unsigned short port;
};

int64_t ly_get_ms();
void ly_set_nodelay(int fd);
void ly_set_nonblock(int fd);

int ly_connect_server(ly_ip_addr_t *ip_addr);

#endif
