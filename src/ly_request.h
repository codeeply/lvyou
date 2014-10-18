#ifndef LVYOU_REQUEST_H_
#define LVYOU_REQUEST_H_

#include "ly_file.h"

#include <stdint.h>
#include <stdlib.h>

#define LY_REQUEST_MAX_NUM 100

#define LY_REQUEST_RBUFFER_SIZE 8
#define LY_REQUEST_WBUFFER_SIZE 256 * 500

typedef struct ly_request_s ly_request_t;

struct ly_request_s {
    int fd;

    size_t wb_offset;
    size_t wb_size;

    u_char wbuffer[LY_REQUEST_WBUFFER_SIZE];
    u_char rbuffer[LY_REQUEST_RBUFFER_SIZE];

    u_char closed:1;
    u_char in_request:1;
    u_char error:1;
};

int ly_request_init();
ly_request_t *ly_request_new();
void ly_request_free(ly_request_t *r);

#endif
