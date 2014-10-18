#ifndef LVYOU_FILE_H_
#define LVYOU_FILE_H_

#include <stdint.h>
#include <stdlib.h>

typedef struct ly_file_item_s ly_file_item_t;
typedef struct ly_file_item_sent_s ly_file_item_sent_t;

struct ly_file_item_s {
    u_char *data;
    int len;
    int index;
    int dest_len;
    int64_t dest_offset;
};

int ly_file_init(const char *fn);
void *ly_file_parse_lines(void *arg);
int ly_file_mget_next_and_fill(int n, u_char *buf, int maxlen);

#endif
