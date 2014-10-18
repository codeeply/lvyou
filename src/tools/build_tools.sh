
gcc -I../ -DLY_DEBUG=1 -Werror -O2 tcp_killer.c ../ly_common.c ../ly_util.c  -o tcp_killer -pthread
