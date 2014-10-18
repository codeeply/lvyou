#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <error.h>

#include "ly_request.h"
#include "ly_common.h"
#include "ly_file.h"

static u_char *ly_file_data;
static int64_t ly_file_size;

#define LY_FILE_ITEM_BACKLOG (1 << 24)

static ly_file_item_t *ly_file_items;
static int64_t ly_file_nitems;
static u_char cache_line_gap[256];
static int64_t ly_file_item_curr;

static int ly_file_dec_len = 1;
static int64_t ly_file_dec_checkpoint = 10;

int ly_file_init(const char *fn)
{
    struct stat sb;
    int fd;

    /* 打开文件 */
    if ((fd = open(fn, O_RDWR)) == 0) {
        ly_perror("open");
        return -1;
    }

    /* 获取文件的属性 */
    if ((fstat(fd, &sb)) == -1) {
        ly_perror("fstat");
        return -1;
    }

    /* 将文件映射至进程的地址空间 */
    if ((ly_file_data = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE,
        fd, 0)) == (void *)-1)
    {
        ly_perror("mmap");
        return -1;
    }

    /* 映射完后, 关闭文件也可以操纵内存 */
    close(fd);

    ly_file_size = sb.st_size;
    printf("mapping %lu bytes from file %s\n", sb.st_size, fn);

    ly_file_items = malloc(LY_FILE_ITEM_BACKLOG * sizeof(ly_file_item_t));
    if (ly_file_items == NULL) {
        ly_info_and_exit("malloc error");
    }

    return 0;
}

void *ly_file_parse_lines(void *arg)
{
    int64_t i = 0, prev = 0, index = 0, offset = 0, n;
    ly_file_item_t *item;

    for ( ; i < ly_file_size - 1; ++i) {
        if (ly_file_data[i] == '\r' && ly_file_data[i + 1] == '\n') {

            item = &ly_file_items[ly_file_nitems];
            item->data = ly_file_data + prev;
            item->len = i - prev;
            
            /* buf = { len | offset | obj } */
            item->dest_len = item->len - (item->len / 3);
        
            if (index == ly_file_dec_checkpoint) {
                ly_file_dec_len++;
                ly_file_dec_checkpoint *= 10;
            }
    
            item->index = index;
            item->dest_offset = offset;
            offset += ly_file_dec_len + item->dest_len + 2;
            item->dest_len += ly_file_dec_len + 2;
            index++;

            __sync_fetch_and_add(&ly_file_nitems, 1);

            i += 1;
            prev = (i + 1);
        } else {
            continue;
        }
    }

    if (i != ly_file_size) {
        ly_info_and_exit("fail to parse from file");
    }
    n = __sync_fetch_and_add(&ly_file_nitems, 0);
    for (i = 0; i < LY_REQUEST_MAX_NUM; ++i) {
        ly_file_items[n + i].data = NULL;
        ly_file_items[n + i].dest_offset = -1;
    }
    __sync_fetch_and_add(&ly_file_nitems, LY_REQUEST_MAX_NUM);
    ly_debug_info("finish parsing nitems = %lu", ly_file_nitems);
}

static int ly_file_fill(ly_file_item_t *item, u_char *buf, int maxlen)
{
    int i, j, k, len, index;
 
    /* maxlen == 256 // only little endian supported */
    len = item->dest_len;
    index = item->index;

    if (len >= maxlen) {
        ly_info_and_exit("len(=%ld) >= maxlen", len);
    }
 
    k = len - 1;
    buf[k--] = '\n';
    buf[k--] = '\r';

    j = item->len / 3;
    for (i = 0; i < j; ++i) {
        buf[k--] = item->data[i];
    }

    j = item->len - (item->len / 3) * 2;
    for (i = 0; i < j; ++i) {
        buf[k--] = item->data[item->len - j + i];
    }

    if (index == 0) {
        buf[k--] = '0';
    }
    while (index > 0) {
        buf[k--] = index % 10 + '0';
        index /= 10;
    }

    if (k != -1) {
        ly_info_and_exit("k(=%d) != -1", k);
    }

    return len;
}

#define ly_min(x, y) ((x) < (y) ? (x) : (y))

int ly_file_mget_next_and_fill(int n, u_char *buf, int maxlen)
{
    int len = 8 + n, i = 0;
    int64_t nitems, start, end;
    ly_file_item_t *item;

    start = __sync_fetch_and_add(&ly_file_item_curr, n);
    end = start + n;
    while (1) {
        nitems = __sync_fetch_and_add(&ly_file_nitems, 0);
        if (nitems > end) break;
    }
    while (start < end) {
        item = &ly_file_items[start++];
        if (i == 0) {
            *(int64_t *)buf = item->dest_offset;
        }

        if (item->data != NULL) {
            len += ly_file_fill(item, buf + len, maxlen - len);
            buf[8 + (i++)] = item->dest_len;
        } else {
            ly_debug_info("send end");
            buf[8 + (i++)] = 255;
        }
    }

    return len;
}
