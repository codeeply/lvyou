rm client
gcc -Wall -DLY_DEBUG=0  -Werror -O0 -g ly_util.c ly_common.c ly_client.c ly_task.c -pthread -o client
