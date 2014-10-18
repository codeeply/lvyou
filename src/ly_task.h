#ifndef LVYOU_TASK_H_
#define LVYOU_TASK_H_

#define LY_TASK_MAX_NUM 500

typedef struct ly_task_result_s ly_task_result_t;
typedef struct ly_task_s ly_task_t;
typedef struct ly_task_group_s ly_task_group_t;

enum {
    ly_task_status_normal = 0,
    ly_task_status_active
} ly_task_status;

struct ly_task_result_s {
    u_char *data;
    int64_t len;
};

struct ly_task_s {
    uint64_t echo_num;
    ly_task_result_t *result;
    u_char cache_line_gap[64];
};

struct ly_task_group_s {
    int ntasks;
    ly_task_t *tasks;
    int nstops;

    ly_task_result_t *result;

    int *task_status;

    int fd;

    int64_t group_size;
    u_char *mmap_addr;
    u_char *buffer;
};

int ly_task_group_init(ly_task_group_t *tg, int ntasks, u_char *mmap_addr);
void *ly_task_run(void *arg);
void *ly_task_group_run(void *arg);

#endif
