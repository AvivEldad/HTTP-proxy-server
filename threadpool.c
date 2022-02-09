#include "threadpool.h"

#include <stdio.h>

#include <stdlib.h>

#include <unistd.h>


/**
 * create a threadpool, initialize the threadpool structure, the mutex and the cv. create the threads
 * @param int num_threads_in_pool number of threads to create
 * @return threadpool* pool if nothing failed in the process, NULL elsewhere
 */

threadpool * create_threadpool(int num_threads_in_pool) {

    ///input sanity check
    if (num_threads_in_pool > MAXT_IN_POOL || num_threads_in_pool < 1) {
        fprintf(stderr, "Illegal number of threads\n");
        return NULL;
    }
    ///initialize the threadpool structure
    threadpool * pool = (threadpool * ) calloc(1, sizeof(threadpool));
    if (pool == NULL) {
        return NULL;
    }
    pool -> num_threads = num_threads_in_pool;

    pool -> threads = (pthread_t * ) calloc(pool -> num_threads, sizeof(pthread_t));
    if (pool -> threads == NULL) {
        free(pool);
        return NULL;
    }
    ///initialized mutex and conditional variables
    if (pthread_mutex_init( & (pool -> qlock), NULL) != 0) {
        free(pool -> threads);
        free(pool);
        return NULL;
    }
    if (pthread_cond_init( & (pool -> q_not_empty), NULL) != 0) {
        pthread_mutex_destroy( & (pool -> qlock));
        free(pool -> threads);
        free(pool);
        return NULL;
    }
    if (pthread_cond_init( & (pool -> q_empty), NULL) != 0) {
        pthread_mutex_destroy( & (pool -> qlock));
        pthread_cond_destroy( & (pool -> q_not_empty));
        free(pool -> threads);
        free(pool);
        return NULL;
    }
    pool -> qsize = 0;
    pool -> qhead = NULL;
    pool -> qtail = NULL;
    pool -> shutdown = 0;
    pool -> dont_accept = 0;

    ///create the threads
    for (int i = 0; i < pool -> num_threads; i++) {
        if (pthread_create( & (pool -> threads[i]), NULL, do_work, pool) != 0) {
            perror("pthread_create:\n");
            pthread_mutex_destroy( & (pool -> qlock));
            pthread_cond_destroy( & (pool -> q_empty));
            pthread_cond_destroy( & (pool -> q_not_empty));
            free(pool -> threads);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

/**
 * Add new work to the queue (if destroy has not begun yet)
 * @param threadpool* from me the threadpool struct
 * @param dispatch_fn dispatch_to_here the function of the new work
 * @param void* arg the argument for the function dispatch_to_here
 */
void dispatch(threadpool * from_me, dispatch_fn dispatch_to_here, void * arg) {
    pthread_mutex_lock( & (from_me -> qlock));
    if (from_me -> dont_accept == 1) {
        pthread_mutex_unlock( & (from_me -> qlock));
        return;
    }
    pthread_mutex_unlock( & (from_me -> qlock));
    if ((dispatch_to_here == NULL) || (arg == NULL)) {
        fprintf(stderr, "invalid parameters");
        return;
    }

    work_t * new_work = (work_t * ) calloc(1, sizeof(work_t));
    if (new_work == NULL) {
        fprintf(stderr, "calloc:\n");
        return;
    }
    new_work -> routine = dispatch_to_here;
    new_work -> arg = arg;
    new_work -> next = NULL;

    pthread_mutex_lock( & (from_me -> qlock));

    if (from_me -> dont_accept == 1) {
        free(new_work);
        pthread_mutex_unlock( & (from_me -> qlock));
        return;
    }
    if (from_me -> qhead == NULL) {
        from_me -> qhead = new_work;
        from_me -> qtail = new_work;
    } else {
        from_me -> qtail -> next = new_work;
        from_me -> qtail = new_work;
    }

    from_me -> qsize++;

    pthread_cond_signal( & (from_me -> q_not_empty));

    pthread_mutex_unlock( & (from_me -> qlock));
}

/**
 * the thread function work. take a work from the queue and do it
 * @param threadpool* p the threadpool struct
 */
void * do_work(void * p) {
    threadpool * pool = (threadpool * ) p;
    while (1) {
        pthread_mutex_lock( & (pool -> qlock));
        if (pool -> shutdown == 1) {
            pthread_mutex_unlock( & (pool -> qlock));
            return NULL;
        }
        if (pool -> qsize == 0) {
            pthread_cond_wait( & (pool -> q_not_empty), & (pool -> qlock));
        }
        if (pool -> shutdown == 1) {
            pthread_mutex_unlock( & (pool -> qlock));
            return NULL;
        }
        work_t * w = pool -> qhead;
        if (w == NULL) {
            pthread_mutex_unlock( & (pool -> qlock));
            continue;
        }
        pool -> qhead = pool -> qhead -> next;
        pool -> qsize--;
        if (pool -> qsize == 0 && pool -> dont_accept == 1) {
            pthread_cond_signal( & (pool -> q_empty));
        }

        pthread_mutex_unlock( & (pool -> qlock));

        w -> routine(w -> arg);
        free(w);
    }

}

/**
 * destroy the threadpool, after all threads are done and the queue is empty
 * @param threadpool* destroyme the threadpool struct
 */
void destroy_threadpool(threadpool * destroyme) {
    pthread_mutex_lock( & (destroyme -> qlock));

    destroyme -> dont_accept = 1;
    if (destroyme -> qsize != 0) {
        pthread_cond_wait( & (destroyme -> q_empty), & (destroyme -> qlock));
    }

    destroyme -> shutdown = 1;
    pthread_cond_broadcast( & (destroyme -> q_not_empty));

    pthread_mutex_unlock( & (destroyme -> qlock));
    void * status;
    for (int i = 0; i < destroyme -> num_threads; i++) {
        pthread_join(destroyme -> threads[i], & status);
    }

    pthread_mutex_destroy( & (destroyme -> qlock));
    pthread_cond_destroy( & (destroyme -> q_empty));
    pthread_cond_destroy( & (destroyme -> q_not_empty));
    free(destroyme -> threads);
    free(destroyme);

}