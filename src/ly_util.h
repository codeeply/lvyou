#ifndef LVYOU_UTIL_H_
#define LVYOU_UTIL_H_

#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <ucontext.h>
#include <pthread.h>
#include <unistd.h>
#include <ucontext.h>
#include <poll.h>

void ly_info_and_exit_inner(const char *fmt, ...);

#define ly_info_and_exit(fmt, args...) \
    ly_info_and_exit_inner("%s : "fmt"\n", __FUNCTION__, ##args)

void ly_debug_info_inner(const char *fmt, ...);

#define ly_debug_info(fmt, args...) \
    if (LY_DEBUG) { \
        ly_debug_info_inner("%s : "fmt"\n", __FUNCTION__, ##args); \
    } \

#define ly_perror(str) \
    do { \
        size_t func_len = strlen(__FUNCTION__); \
        size_t str_len = strlen(str); \
        char *buf = (char *)malloc(func_len + str_len + 2); \
        memcpy(buf, __FUNCTION__, func_len); \
        buf[func_len] = ' '; \
        memcpy(buf + func_len + 1, str, str_len); \
        buf[func_len + str_len +  1] = 0; \
        perror(buf); \
        free(buf); \
    } while (0);

#define ly_perror_and_exit(str) \
    do { \
        size_t func_len = strlen(__FUNCTION__); \
        size_t str_len = strlen(str); \
        char *buf = (char *)malloc(func_len + str_len + 2); \
        memcpy(buf, __FUNCTION__, func_len); \
        buf[func_len] = ' '; \
        memcpy(buf + func_len + 1, str, str_len); \
        buf[func_len + str_len +  1] = 0; \
        perror(buf); \
        free(buf); \
        exit(-1); \
    } while (0);

#define ly_perror_and_return(str, ret) \
    do { \
        size_t func_len = strlen(__FUNCTION__); \
        size_t str_len = strlen(str); \
        char *buf = (char *)malloc(func_len + str_len + 2); \
        memcpy(buf, __FUNCTION__, func_len); \
        buf[func_len] = ' '; \
        memcpy(buf + func_len + 1, str, str_len); \
        buf[func_len + str_len +  1] = 0; \
        perror(buf); \
        free(buf); \
        return ret; \
    } while (0);

void ly_pthread_setaffinity(int index);

#endif
