#ifndef LVYOU_COROUTINE_H_
#define LVYOU_COROUTINE_H_

#include "ly_common.h"

extern int ly_coroutine_poll_delay;

typedef struct ly_coroutine_s ly_coroutine_t;
typedef struct ly_coroutine_env_s ly_coroutine_env_t;
typedef void (*ly_coroutine_task_t)(ly_coroutine_env_t *, void *);

struct ly_coroutine_s {
    ucontext_t uc;
    ucontext_t exituc;
    int status;
    
    int susfd;
    int events;
};

struct ly_coroutine_env_s {
    int ncos;
    ly_coroutine_t *cos;

    int stack_curr;
    int nstacks;
    void **stacks;

    /* num of suspended coroutines */
    int nsuscos;

    ly_coroutine_t *currco;
    ucontext_t startuc;

    int pollfd_curr;
    int npollfds;
    struct pollfd *pollfds;

    u_char inuse:1;
    u_char left_from_yield:1;
    u_char started:1;

    // 临时成员
    int64_t read_eagain;
    int64_t write_eagain;
};

/* 用法:
 * 注意要在同一个线程内配套调用
 *
 * ly_coroutine_env_t *env;
 * env = calloc(1, sizeof(ly_coroutine_env_t));
 *
 * ly_coroutine_init(env);
 * 
 * ly_coroutine_add(env, task0, arg0);
 * ly_coroutine_add(env, task1, arg1);
 * ......
 *
 * ly_coroutine_run_all(env);
 *
 * ly_coroutine_destroy(env);
 *
 * free(env);
 */
int ly_coroutine_init(ly_coroutine_env_t *, int ncos);
int ly_coroutine_add(ly_coroutine_env_t *env, ly_coroutine_task_t task,
    void *arg);
void ly_coroutine_run_all(ly_coroutine_env_t *);
void ly_coroutine_destroy(ly_coroutine_env_t *);

/* ly_coroutine_read/ly_coroutine_write在调用者看来是阻塞IO.
 * 操作仅在成功或出错时返回, 出错时上层应用应该关闭相应的fd.
 * TODO: 增加超时设计
 */
ssize_t ly_coroutine_read(ly_coroutine_env_t *env, int fd, void *buf,
    size_t count);
ssize_t ly_coroutine_write(ly_coroutine_env_t *env, int fd, const void *buf,
    size_t count);

#endif
