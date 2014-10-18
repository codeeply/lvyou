#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <error.h>


int main(int argc, char **argv)
{
    int fd, num, i, j, n, nb;

    if (argc < 3) {
        fprintf(stderr, "./gen_data path_to_file num\n");
        exit(-1);
    }
    /* 打开文件 */
    if ((fd = open(argv[1], O_RDWR | O_TRUNC)) < 0) {
        perror("open");
    }

    static u_char buf[256];
    u_char tmp;
    int k;
    num = atoi(argv[2]);
    for (i = 0; i < num; ++i) {
        n = rand() % 201;
        for (j = 0; j < n; ++j) buf[j] = 'a' + j % 26;
        for (j = 0; j < n; ++j) {
            k = rand() % n;
            tmp = buf[j];
            buf[j] = buf[k];
            buf[k] = tmp;
        }
        buf[n] = '\r';
        buf[n + 1] = '\n';
        nb = write(fd, buf, n + 2);
        if (nb != n + 2) {
            fprintf(stderr, "write may error\n");
            exit(-1);
        }
    }

    return 0;
}
