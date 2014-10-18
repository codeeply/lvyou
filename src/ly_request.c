#include "ly_request.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static ly_request_t ly_requests[LY_REQUEST_MAX_NUM];
static int ly_request_curr;
static pthread_mutex_t ly_request_mutex;

int ly_request_init() {
    int i;
    pthread_mutex_init(&ly_request_mutex, NULL);

    for (i = 0; i < LY_REQUEST_MAX_NUM; ++i) {
        ly_requests[i].fd = -1;
    }

    return 0;
}

ly_request_t *ly_request_new() {
    int i;
    pthread_mutex_lock(&ly_request_mutex);
    for (i = (ly_request_curr + 1) % LY_REQUEST_MAX_NUM; i != ly_request_curr;
            i = (i + 1) % LY_REQUEST_MAX_NUM)
    {
        if (ly_requests[i].fd == -1) {
            ly_request_curr = i;
            pthread_mutex_unlock(&ly_request_mutex);
            return &ly_requests[i];
        }
    }
    pthread_mutex_unlock(&ly_request_mutex);
    return NULL;
}

void ly_request_free(ly_request_t *r) {
    assert(r->fd != -1);
    pthread_mutex_lock(&ly_request_mutex);
    r->fd = -1;
    pthread_mutex_unlock(&ly_request_mutex);
}
