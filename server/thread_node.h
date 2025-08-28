//
// Created by niko on 8/2/25.
//
#include <stdbool.h>
#include <bits/pthreadtypes.h>
#include "queue.h"

/**
 * This struct should represent a linked list with thread data
 * and passes as arg to pthread_create, should be returned from
 * the thread fun and be joined later
 */
typedef struct thread_node thread_node;
struct thread_node {
    pthread_t thread_id;
    char client_ip[16];
    int client_fd;
    int file_fd;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool is_completed;
    SLIST_ENTRY(thread_node) next;

};

