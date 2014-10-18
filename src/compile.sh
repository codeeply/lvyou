rm coroutine pserver gen_data
#gcc -Wall -DLY_DEBUG=0 -Werror ly_util.c test_co.c ly_coroutine.c ly_common.c -pthread -o coroutine -g -O0
#gcc -Wall -DLY_DEBUG=0  -Werror ly_util.c ly_common.c ly_client.c ly_coroutine.c -pthread -o client
gcc gen_data.c -o gen_data
gcc -DLY_DEBUG=0 -Werror -O2 ly_server.c ly_common.c ly_util.c ly_request.c ly_file.c -o pserver -pthread

