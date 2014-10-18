#include "ly_coroutine.h"
#include <stdio.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <stdint.h>


ly_coroutine_env_t *env;
char buf[128];

void func1(ly_coroutine_env_t *env, void *arg) {
    ssize_t ret = ly_coroutine_read(env, 0, buf, 128);
    printf("%ld %s\n", ret, buf);
    printf("func1\n");
    ly_coroutine_write(env, 1, "func111a\n", 9);
    ly_coroutine_write(env, 1, "func111b\n", 9);
    ly_coroutine_read(env, 0, 0, 0);
}

void func2(ly_coroutine_env_t *env, void *arg) {
    ssize_t ret = ly_coroutine_read(env, 0, buf, 128);
    printf("%ld %s\n", ret, buf);
    printf("func2 %lu\n", (uintptr_t)arg);
    ly_coroutine_write(env, 1, "func222a\n", 9);
    ly_coroutine_write(env, 1, "func222b\n", 9);
}

void func3(ly_coroutine_env_t *env, void *arg) {
    uintptr_t tmp = (uintptr_t)arg;
    ly_coroutine_write(env, 1, "func333a\n", 9);
    ly_coroutine_write(env, 1, "func333b\n", 9);
    printf("func3 %lu\n", tmp);
}

int main(int argc, char **argv)
{
    env = calloc(1, sizeof(ly_coroutine_env_t));
    ly_coroutine_init(env, 3);
 
    ly_coroutine_add(env, func2, (void *)2);
    ly_coroutine_add(env, func3, (void *)1);
    ly_coroutine_add(env, func1, (void *)3);
 
    ly_coroutine_run_all(env);
    ly_coroutine_destroy(env);
    free(env);
    
    //ly_coroutine_add(func2, (void *)223);
    //ly_coroutine_run_all(NULL);
    return (0);
}
