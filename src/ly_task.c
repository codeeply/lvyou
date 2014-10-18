#include "ly_common.h"
#include "ly_task.h"

static int ly_task_send_request(ly_task_t *task);
static ly_task_result_t *ly_task_recv_response(ly_task_t *task);
static int ly_task_group_run_inner(ly_task_group_t *tg, int fd);
static int ly_task_sync_read(int fd, u_char *buffer, int len);
static int ly_task_sync_write(int fd, u_char *buffer, int len);

static int ly_task_send_request(ly_task_t *task)
{
    __sync_fetch_and_add(&task->echo_num, 1);
    return 0;
}

static ly_task_result_t *ly_task_recv_response(ly_task_t *task)
{
    int i;
    uint64_t echo_num;

    for (i = 0; ; ++i) {
        echo_num = __sync_fetch_and_add(&task->echo_num, 0);
        if (echo_num & 1) {
            //ly_delay(100);
            continue;
        }
        return task->result;
    }

    return NULL;
}

void *ly_task_run(void *arg)
{
    ly_task_t *task = arg;
    ly_task_result_t *result;

    while (1) {
        if (ly_task_send_request(task) == -1) {
            break;
        }
        result = ly_task_recv_response(task);
        if (result == NULL) {
            ly_info_and_exit("ly_task_recv_response error");
        }
        if (result->data == NULL) {
            ly_debug_info("task stop");
            return NULL;
        }
        //printf("result = %p ->data = %p\n", result, result->data);
        // do something with the result
    }

    ly_info_and_exit("at the bottom of ly_task_run");
    return NULL;
}

static int ly_task_sync_read(int fd, u_char *buffer, int len)
{
    int nb, n = 0;

    while (len > 0) {
        nb = read(fd, buffer + n, len);
        //printf("len = %d, nb = %d\n", len, nb);
        if (nb == 0) {
            ly_info_and_exit("closed by the server");
        }
        if (nb == -1) {
            if (nb == EINTR) {
                ly_debug_info("read EINTR");
                continue;
            }
            ly_perror("read error");
            return -1;
        }
        len -= nb;
        n += nb;
    }

    return 0;
}

static int ly_task_sync_write(int fd, u_char *buffer, int len)
{
    int nb, n = 0;

    while (len > 0) {
        nb = write(fd, buffer + n, len);
        if (nb == -1) {
            if (nb == EINTR) {
                ly_debug_info("write EINTR");
                continue;
            }
            ly_perror("write error");
            return -1;
        }
        len -= nb;
        n += nb;
    }

    return 0;
}

static int ly_task_group_run_inner(ly_task_group_t *tg, int fd)
{
    int i, j, n;
    int32_t nas = 0;
    int64_t offset, first_offset;

    for (i = 0; i < tg->ntasks; ++i) {
        if (tg->task_status[i] == ly_task_status_active) {
            nas++;
        }
    }
    if (nas == 0) {
        return 0;
    }

    ly_debug_info("nas = %d", nas);
    if (ly_task_sync_write(fd, (u_char *)&nas, 4) != 0) {
        ly_debug_info("ly_task_sync_write error");
        return -1;
    }
 
    n = 8 + nas;
    if (ly_task_sync_read(fd, tg->buffer, n) != 0) {
        ly_debug_info("ly_task_sync_read error");
        return -1;
    }
    
    offset = *(int64_t *)(tg->buffer);
    first_offset = offset;
    for (i = 0, j = 0; i < tg->ntasks; ++i) {
        if (tg->task_status[i] != ly_task_status_active) {
            continue;
        }

        n = tg->buffer[(j++) + 8];
        if (n == 255) {
            ly_debug_info("result = %p, n == 255", tg->result + i);
            tg->nstops++;
            tg->result[i].data = NULL;
            tg->result[i].len = 0;
        } else {
            //printf("n = %d, offset = %ld nas = %d\n", n, offset, nas);
            tg->result[i].data = tg->mmap_addr + offset;
            tg->result[i].len = n;
            offset += n;
        }
    }
    ly_debug_info("nas = %d, j = %d", nas, j);
    assert(nas == j);

    if (first_offset == -1) {
        return nas;
    }
    n = offset - first_offset;
    tg->group_size += n;
    if (ly_task_sync_read(fd, tg->mmap_addr + first_offset, n) != 0) {
        ly_debug_info("ly_task_sync_read error");
        return -1;
    }

    return nas;
}

void *ly_task_group_run(void *arg)
{
    int i, ret;
    uint64_t echo_num;
    ly_task_group_t *tg = arg;

    while (1) {
        for (i = 0; i < tg->ntasks; ++i) {
            echo_num = __sync_fetch_and_add(&tg->tasks[i].echo_num, 0);
            if (echo_num & 1) {
                tg->task_status[i] = ly_task_status_active;
            }
        }
        ret = ly_task_group_run_inner(tg, tg->fd);
        if (ret == -1) {
            ly_info_and_exit("ly_task_group_run_inner error");
        }
        if (ret == 0) {
            //ly_delay(10);
        }
        for (i = 0; i < tg->ntasks; ++i) {
            if (tg->task_status[i] == ly_task_status_normal) {
                continue;
            }
            tg->task_status[i] = ly_task_status_normal;

            __sync_fetch_and_add(&tg->tasks[i].echo_num, 1);
        }
        if (tg->nstops == tg->ntasks) {
            ly_debug_info("group stop");
            return NULL;
        }
    }

    return NULL;
}

int ly_task_group_init(ly_task_group_t *tg, int ntasks, u_char *mmap_addr)
{
    int i;

    tg->ntasks = ntasks;
    tg->tasks = calloc(ntasks, sizeof(ly_task_t));
    if (tg->tasks == NULL) {
        ly_info_and_exit("calloc error");
    }

    tg->result = calloc(ntasks, sizeof(ly_task_result_t));
    if (tg->result == NULL) {
        ly_info_and_exit("calloc error");
    }

    tg->task_status = calloc(ntasks, sizeof(int));
    if (tg->task_status == NULL) {
        ly_info_and_exit("calloc error");
    }

    tg->buffer = malloc(ntasks);
    if (tg->buffer == NULL) {
        ly_info_and_exit("malloc error");
    }

    tg->nstops = 0;
    tg->mmap_addr = mmap_addr;
    for (i = 0; i < ntasks; ++i) {
        tg->tasks[i].result = &tg->result[i];
    }

    return 0;
}
