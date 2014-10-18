#include "ly_common.h"
#include "ly_task.h"

#define LY_CLIENT_RBUFFER_SIZE 256

static int task_group_num;
static int task_num_per_group;

int main(int argc, char **argv)
{
    int i, j, fd, n, acfd;
    u_char *mmap_addr;
    int64_t start_time, end_time, large_size, size;
    pthread_t *tid;
    ly_task_group_t *tgs;
    ly_ip_addr_t ip_addr;

    start_time = ly_get_ms();

    if (argc < 6) {
        ly_info_and_exit("./ly_client ip port task_group_num "
                         "task_num_per_group result");
    }

    ip_addr.ip = argv[1];
    ip_addr.port = atoi(argv[2]);

    task_group_num = atoi(argv[3]);
    task_num_per_group = atoi(argv[4]);

    large_size = (((int64_t)1) << 30) * 1;
    fd = open(argv[5], O_CREAT | O_RDWR | O_TRUNC, 0644);
    lseek(fd, large_size - 1, SEEK_SET);
    write(fd, "", 1);
    mmap_addr = mmap(NULL, large_size, PROT_WRITE, MAP_SHARED, fd, 0);
    if (mmap_addr == MAP_FAILED) {
        ly_perror_and_exit("mmap error");
    }

    tgs = calloc(task_group_num, sizeof(ly_task_group_t));
    if (tgs == NULL) {
        ly_info_and_exit("calloc error");
    }
    for (i = 0; i < task_group_num; ++i) {
        ly_task_group_init(tgs + i, task_num_per_group, mmap_addr);
    }
 
    n = task_group_num * (task_num_per_group + 1);
    tid = (pthread_t *)calloc(n, sizeof(pthread_t));
    if (tid == NULL) {
        ly_info_and_exit("calloc error");
    }

    n = 0;
    for (i = 0; i < task_group_num; ++i) {
        if ((acfd = ly_connect_server(&ip_addr)) == -1) {                          
            ly_info_and_exit("ly_connect_server error");                            
        }
        tgs[i].fd = acfd;
        printf("acfd = %d\n", acfd);
        pthread_create(tid + (n++), NULL, ly_task_group_run, tgs + i);
        for (j = 0; j < task_num_per_group; ++j) {
            pthread_create(tid + (n++), NULL, ly_task_run, &tgs[i].tasks[j]);
        }
    }
    for (i = 0; i < n; ++i) {
        pthread_join(tid[i], NULL);
    }

    size = 0;
    for (i = 0; i < task_group_num; ++i) {
        size += tgs[i].group_size;
    }
    munmap(mmap_addr, large_size);
    ftruncate(fd, size);
    close(fd);

    end_time = ly_get_ms();
    printf("cost=%ld\n", end_time - start_time);

    return 0;
}
