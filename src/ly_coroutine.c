#include "ly_coroutine.h"
#include "ly_common.h"

// 一个线程内开启的协程数不能超过这个数
#define LY_COROUTINE_MAX_NUM (1 << 14)
// 这个设太小, 在debug的时候进入vsprintf调用栈会导致溢出
#define LY_COROUTINE_STACK_SIZE (1 << 14)

int ly_coroutine_poll_delay;

enum {
    LY_COROUTINE_NORMAL = 0,
    LY_COROUTINE_SUSPENDED,
    LY_COROUTINE_RUNNING
} ly_coroutine_status;

static int ly_coroutine_init_uc(ly_coroutine_env_t *env, ucontext_t *uc,
    void *func, void *arg, ucontext_t *exituc);
static void ly_coroutine_resume(ly_coroutine_env_t *env, ly_coroutine_t * co);
static void ly_coroutine_exit(ly_coroutine_env_t *env, void *arg);
static void ly_coroutine_yield(ly_coroutine_env_t *env);

static int ly_coroutine_init_uc(ly_coroutine_env_t *env, ucontext_t *uc,
    void *func, void *arg, ucontext_t *exituc)
{
    getcontext(uc);

    if (env->stack_curr >= env->nstacks) {
        ly_info_and_exit("env->stack_curr >= env->nstacks");
    }

    uc->uc_stack.ss_sp = mmap(0, LY_COROUTINE_STACK_SIZE,
            PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANON, -1, 0);
    if (uc->uc_stack.ss_sp == MAP_FAILED) {
        ly_perror_and_return("mmap error", -1);
    }

    uc->uc_stack.ss_size = LY_COROUTINE_STACK_SIZE;
    uc->uc_stack.ss_flags = 0;
    uc->uc_link = exituc;

    env->stacks[env->stack_curr++] = uc->uc_stack.ss_sp;

    typedef void (*func_type_for_mc)();
    makecontext(uc, (func_type_for_mc)func, 2, env, arg);

    return 0;
}

static void ly_coroutine_resume(ly_coroutine_env_t *env, ly_coroutine_t *co)
{
    if (co->status != LY_COROUTINE_SUSPENDED) {
        ly_info_and_exit("co->status != LY_COROUTINE_SUSPENDED");
    }

    co->susfd = -1;
    co->events = 0;
    co->status = LY_COROUTINE_RUNNING;
    env->currco = co;
    env->nsuscos--;
    env->left_from_yield = 0;
 
    setcontext(&co->uc);
    ly_perror("getcontext error");
}

static void ly_coroutine_yield(ly_coroutine_env_t *env)
{
    if (!env->started) {
        ly_info_and_exit("ly_coroutine_run_all not started yet");
    }

    env->left_from_yield = 1;

    if (getcontext(&env->currco->uc) == -1) {
        ly_perror("getcontext error");
    }

    if (!env->left_from_yield) {
        return;
    }

    env->nsuscos++;
    env->currco->status = LY_COROUTINE_SUSPENDED;

    ly_coroutine_run_all(env);
}

static void ly_coroutine_exit(ly_coroutine_env_t *env, void *arg)
{
    ly_debug_info("come to exit");
    ly_coroutine_t *co = env->currco;

    if (co->status != LY_COROUTINE_RUNNING) {
        ly_info_and_exit("co->status != LY_COROUTINE_RUNNING");
    }
    co->status = LY_COROUTINE_NORMAL;

    if (co->susfd != -1 || co->events != 0) {
        ly_info_and_exit("co->susfd(=%d) != -1 || co->events(=%d) != 0",
                          co->susfd, co->events);
    }

    ly_coroutine_run_all(env);
}

int ly_coroutine_init(ly_coroutine_env_t *env, int ncos)
{
    if (env == NULL) {
        ly_info_and_exit("env == NULL");
    }

    if (env->inuse) {
        ly_debug_info("the coroutine env has init");
        return -1;
    }

    memset(env, 0, sizeof(ly_coroutine_env_t));

    if (ncos <= 0 || ncos > LY_COROUTINE_MAX_NUM) {
        ly_debug_info("ncos(=%lu) exceeds the range", ncos);
        return -1;
    }
    env->ncos = ncos;
    
    env->cos = (ly_coroutine_t *)calloc(ncos, sizeof(ly_coroutine_t));
    if (env->cos == NULL) {
        ly_info_and_exit("env->cos calloc error");
    } else {
        ly_debug_info("env = %p, ncos = %d", env, ncos);
    }

    env->nstacks = ncos * 2;
    env->stacks = (void **)calloc(env->nstacks, sizeof(void *));
    if (env->stacks == NULL) {
        ly_info_and_exit("env->stacks calloc error");
    }

    env->npollfds = ncos;
    env->pollfds = calloc(env->npollfds, sizeof(struct pollfd));
    if (env->pollfds == NULL) {
        ly_info_and_exit("env->npollfds calloc error");
    }

    env->nsuscos = 0;
    env->currco = NULL;
    env->stack_curr = 0;
    env->pollfd_curr = 0;
    env->inuse = 1;

    return 0;
}

int ly_coroutine_add(ly_coroutine_env_t *env, ly_coroutine_task_t task,
    void *arg)
{
    int i;
    ly_coroutine_t *co;

    if (!env->inuse) {
        ly_debug_info("the coroutine env hasn't init");
        return -1;
    }

    /* all the coroutinue are LY_COROUTINE_SUSPENDED here */
    if (env->nsuscos == env->ncos) {
        ly_debug_info("too many coroutine added to the single env");
        return -1;
    }

    for (i = 0; i < env->ncos; ++i) {
        if (env->cos[i].status == LY_COROUTINE_NORMAL) {
            break;
        }
    }
    if (i == env->ncos) {
        ly_info_and_exit("unused coroutine slot not found");
    }
    co = &env->cos[i];

    if (ly_coroutine_init_uc(env, &co->exituc, ly_coroutine_exit, NULL, NULL)
        == -1)
    {
        return -1;
    }
    if (ly_coroutine_init_uc(env, &co->uc, task, arg, &co->exituc) == -1) {
        return -1;
    }

    env->nsuscos++;
    co->susfd = -1;
    co->events = 0;
    co->status = LY_COROUTINE_SUSPENDED;

    return 0;
}

static void ly_coroutine_set_all_events(ly_coroutine_env_t *env)
{
    int i;
    ly_coroutine_t *co;

    if (env->ncos != env->npollfds) {
        ly_info_and_exit("env->ncos != env->npollfds");
    }

    for (i = 0; i < env->ncos; ++i) {
        co = &env->cos[i];
        if (co->status != LY_COROUTINE_SUSPENDED) {
            if (co->status != LY_COROUTINE_NORMAL) {
                ly_info_and_exit("co->status = %d", co->status);
            }
        }

        if ((co->susfd == -1 && co->events != 0)
            || (co->susfd != -1 && co->events == 0))
        {
            ly_info_and_exit("susfd = %d, events = %d", co->susfd, co->events); 
        }
        env->pollfds[i].fd = co->susfd;
        env->pollfds[i].events = (co->events | POLLERR | POLLHUP);
    }
}

void ly_coroutine_run_all(ly_coroutine_env_t *env)
{
    int n, tn;
    ly_coroutine_t *co;

    if (!env->started) {
        if (getcontext(&env->startuc) == -1) {
            ly_info_and_exit("getcontext error");
        }
        if (env->started) {
            if (!env->inuse) {
                ly_info_and_exit("!env->inuse");
            }
            env->started = 0;
            return;
        }
        env->started = 1;
    }

    if (!env->inuse) {
        ly_info_and_exit("the coroutine env hasn't init");
    }

    if (env->nsuscos == 0) {
        ly_debug_info("env = %p all coroutines exit", env);
        setcontext(&env->startuc);
        ly_perror_and_exit("setcontext error");
    }

    /* 判定是否有挂起且有事件到来的协程 */
    for (n = env->pollfd_curr, tn = 0; tn != env->npollfds;
         n= (n + 1) % env->npollfds, ++tn)
    {
        co = &env->cos[n];
        if (co->status != LY_COROUTINE_SUSPENDED) {
            continue;
        }

        if (env->pollfds[n].revents || co->susfd == -1) {
            break;
        }
    }

    if (tn == env->npollfds) {
        ly_coroutine_set_all_events(env);
        if (ly_coroutine_poll_delay > 0) {
            ly_delay(ly_coroutine_poll_delay);
        }

        for (n = 0; n <= 0; ) {
            /* 所有事件都无效, timeout为-1将不会返回? */
            n = poll(env->pollfds, env->npollfds, 1);
 
            if (n == -1) {
                if (errno == EAGAIN) {
                    ly_debug_info("errno == EAGAIN");
                    continue;
                }
                if (errno == EINTR) {
                    ly_debug_info("errno == EINTR");
                    continue;
                }
                ly_perror_and_exit("poll error");
            }
            ly_debug_info("env = %p poll %d from %d", env, n, env->npollfds);
        }
    }

    for ( ; ; ) {
        n = env->pollfd_curr;
        co = &env->cos[n];
        env->pollfd_curr = (n + 1) % env->npollfds;

        if (co->status != LY_COROUTINE_SUSPENDED) {
            continue;
        }

        if (env->pollfds[n].revents == 0 && co->susfd != -1) {
            continue;
        }

        env->pollfds[n].revents = 0;
        ly_coroutine_resume(env, co);
        ly_info_and_exit("ly_coroutine_resume returned");
    }

    ly_info_and_exit("at the end of ly_coroutine_run_all");
}

void ly_coroutine_destroy(ly_coroutine_env_t *env)
{
    int i;

    if (!env->inuse) {
        ly_info_and_exit("env not inuse");
    }
    env->inuse = 0;

    for (i = 0; i < env->nstacks; ++i) {
        if (env->stacks[i]) {
            munmap(env->stacks[i], LY_COROUTINE_STACK_SIZE);
            env->stacks[i] = NULL;
        }
    }
    free(env->cos);
}

ssize_t ly_coroutine_read(ly_coroutine_env_t *env, int fd, void *buf,
    size_t count)
{
    ssize_t ret;

    while (1) {
        env->currco->susfd = fd;
        env->currco->events = POLLIN;
        ly_coroutine_yield(env);

        ret = read(fd, buf, count);
        if (ret == 0) {
            // closed by the peer
            ly_info_and_exit("read return zero");
            return -1;
        }

        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN) {
                return -1;
            }
            env->read_eagain++;
            ly_debug_info("read EAGAIN fd = %d", fd);
        } else {
            return ret;
        }
    }
}

ssize_t ly_coroutine_write(ly_coroutine_env_t *env, int fd, const void *buf,
    size_t count)
{
    ssize_t len = 0, ret;

    while (1) {
        ret = write(fd, buf, count);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN) {
                return -1;
            }
            env->write_eagain++;
            ly_debug_info("write EAGAIN fd = %d", fd);
        } else {
            if (ret == 0) {
                ly_debug_info("write fd = %d return zero", fd);
            }
            len += ret;
            count -= ret;
            if (count == 0) {
                return len;
            }
            ly_info_and_exit("len = %d, count = %d", len, count);
        }

        env->currco->susfd = fd;
        env->currco->events = POLLOUT;
        ly_coroutine_yield(env);
    }
}
