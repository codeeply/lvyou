#define _GNU_SOURCE
#include "ly_util.h"

#include <pthread.h> // 不加这个头文件setaffinity会crash
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

void ly_info_and_exit_inner(const char *fmt, ...)
{
    va_list ap;
    
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    exit(-1);
}

void ly_debug_info_inner(const char *fmt, ...)
{
    va_list ap;
    
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void ly_pthread_setaffinity(int index)
{
    int ret;
    pthread_t p = pthread_self();

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(index, &mask);

    ret = pthread_setaffinity_np(p, sizeof(mask), &mask);
    if (ret < 0) {
        ly_perror("setaffinity fail");
    }
}
