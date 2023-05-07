#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <float.h>
#include <asm-generic/errno.h>
#include <limits.h>
#include <math.h>


struct thread_task {
    thread_task_f function;
    void *arg;

    /* PUT HERE OTHER MEMBERS */
//    atomic_bool is_finished;
    bool is_finished;
    bool is_running;
    bool is_join;
    bool is_pushed;
    bool is_detach;

    void *returned;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

struct queue_task {
    struct thread_task *queue[TPOOL_MAX_TASKS];
    int left_current; // is_valid
    int right_current; // not_valid
    int sz;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

void init_queue_task(struct queue_task *queue_task) {
    queue_task->left_current = 0;
    queue_task->right_current = 0;
    queue_task->sz = 0;

    pthread_mutex_init(&queue_task->mutex, NULL);
    pthread_cond_init(&queue_task->cond, NULL);
}

struct thread_state {
    pthread_t thread;
    bool is_working;
};

struct thread_pool {
    struct thread_state *threads;
    int size;
    int capacity;
    atomic_int busy_now;

    struct queue_task queue_task;
    atomic_bool is_dead;
};

struct arg_task_thread {
    struct thread_pool *thread_pool;
    int idx;
};

void *task_thread_fun(void *void_arg_task_thread_fun) {
    struct arg_task_thread *arg_task_thread = void_arg_task_thread_fun;

    int idx = arg_task_thread->idx;

    struct thread_pool *thread_pool = arg_task_thread->thread_pool;
    struct queue_task *queue_task = &thread_pool->queue_task;

    while (atomic_load(&thread_pool->is_dead) == false) {
        struct thread_task *thread_task = NULL;
        {
            pthread_mutex_lock(&queue_task->mutex);

            while (queue_task->sz == 0) {
                pthread_cond_wait(&queue_task->cond, &queue_task->mutex);
                if (atomic_load(&thread_pool->is_dead) == true) {
                    pthread_mutex_unlock(&queue_task->mutex);
                    free(void_arg_task_thread_fun);
                    return NULL;
                }
            }

            atomic_fetch_add(&thread_pool->busy_now, 1);

            thread_task = queue_task->queue[queue_task->left_current++];
            queue_task->left_current %= TPOOL_MAX_TASKS;
            queue_task->sz--;

            pthread_mutex_unlock(&queue_task->mutex);
        }

        if (thread_task == NULL) {
            continue;
        }

        thread_pool->threads[idx].is_working = true;
        {
            pthread_mutex_lock(&thread_task->mutex);

            thread_task->is_running = true;
            thread_task->is_finished = false;

            pthread_mutex_unlock(&thread_task->mutex);
            thread_task->returned = thread_task->function(thread_task->arg);
            pthread_mutex_lock(&thread_task->mutex);

            thread_task->is_running = false;
            thread_task->is_finished = true;

            pthread_cond_signal(&thread_task->cond);
            pthread_mutex_unlock(&thread_task->mutex);
            if (thread_task->is_detach) {
                free(thread_task);
            }
        }

        thread_pool->threads[idx].is_working = false;
        atomic_fetch_sub(&thread_pool->busy_now, 1);

    }

    free(void_arg_task_thread_fun);
    return NULL;
}

int
thread_pool_new(int max_thread_count, struct thread_pool **pool) {
    if (max_thread_count < 1 || max_thread_count > 20 || pool == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    *pool = calloc(1, sizeof(struct thread_pool));

    (*pool)->capacity = max_thread_count;
    (*pool)->threads = NULL;
    (*pool)->size = 0;
    (*pool)->threads = calloc(max_thread_count, sizeof(struct thread_state));
    (*pool)->is_dead = ATOMIC_VAR_INIT(false);
    (*pool)->busy_now = ATOMIC_VAR_INIT(0);

    init_queue_task(&(*pool)->queue_task);

    return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool) {
    return pool->size;
}

int
thread_pool_delete(struct thread_pool *pool) {
    if (pool->queue_task.sz > 0) {
        return TPOOL_ERR_HAS_TASKS;
    }

    for (int i = 0; i < pool->size; ++i) {
        if ((pool->threads + i) != NULL && pool->threads[i].is_working) {
            return TPOOL_ERR_HAS_TASKS;
        }
    }

    atomic_store(&pool->is_dead, true);
    pthread_cond_broadcast(&pool->queue_task.cond);
    for (int i = 0; i < pool->size; ++i) {
        pthread_join(pool->threads[i].thread, NULL);
    }

    free(pool->threads);
    free(pool);
    return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) {
    if (pool->queue_task.sz + atomic_load(&pool->busy_now) >= TPOOL_MAX_TASKS) {
        return TPOOL_ERR_TOO_MANY_TASKS;
    }
    if (pool == NULL || task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    task->is_finished = false;
    task->is_running = false;
    task->is_join = false;
    task->is_pushed = true;
    task->is_detach = false;

    struct queue_task *queue_task = &pool->queue_task;
    pthread_mutex_lock(&queue_task->mutex);

    queue_task->queue[queue_task->right_current] = task;
    queue_task->right_current = (queue_task->right_current + 1) % TPOOL_MAX_TASKS;
    ++queue_task->sz;

    if (queue_task->sz > 0 && pool->busy_now == pool->size &&
        pool->size < pool->capacity) {

        pool->threads[pool->size].is_working = false;
        struct arg_task_thread *arg_task_thread = calloc(1, sizeof(struct arg_task_thread));
        arg_task_thread->idx = pool->size;
        arg_task_thread->thread_pool = pool;

        pthread_create(&pool->threads->thread,
                       NULL,
                       task_thread_fun,
                       (void *) arg_task_thread);

        pool->size++;
    }

    pthread_cond_signal(&queue_task->cond);
    pthread_mutex_unlock(&queue_task->mutex);
    return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg) {
    if (task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    (*task) = calloc(1, sizeof(struct thread_task));

    (*task)->function = function;
    (*task)->arg = arg;
    (*task)->is_finished = false;
    (*task)->is_running = false;
    (*task)->is_join = false;
    (*task)->is_pushed = false;
    (*task)->is_detach = false;

    return 0;
}

bool
thread_task_is_finished(const struct thread_task *task) {
    return task->is_finished;
}

bool
thread_task_is_running(const struct thread_task *task) {
    return task->is_running;
}

#ifdef NEED_TIMED_JOIN
int
thread_task_timed_join(struct thread_task *task, double timeout, void **result) {
    if (task->is_detach) {
        return TPOOL_ERR_TASK_IS_DETACH;
    }

    if (task->is_pushed == false) {
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    long double eps = 10e-9;
    if (timeout < 0 || fabsl(timeout - eps) < 0) {
        return TPOOL_ERR_TIMEOUT;
    }
    timeout += eps;
    if (timeout < 1) {
        timeout = 1;
    }

    struct timespec timespec;
    clock_gettime(CLOCK_REALTIME, &timespec);
    long int sec = (long int)timeout;
    long int n_sec = (long int)((timeout - (double)sec) * 1e9);
    timespec.tv_sec += sec;
    timespec.tv_nsec += n_sec;

    pthread_mutex_lock(&task->mutex);
    while (task->is_finished == false) {
        if (sec == 0 && n_sec == 0 || pthread_cond_timedwait(&task->cond, &task->mutex, &timespec) == ETIMEDOUT) {
            pthread_mutex_unlock(&task->mutex);
            return TPOOL_ERR_TIMEOUT;
        }
    }

    *result = task->returned;
    task->is_join = true;
    pthread_mutex_unlock(&task->mutex);

    return 0;
}
#endif

int
thread_task_delete(struct thread_task *task) {
    if (task->is_detach) {
       return TPOOL_ERR_TASK_IS_DETACH;
    }

    if (task->is_join != true && task->is_pushed == true) {
        return TPOOL_ERR_TASK_IN_POOL;
    }
    free(task);

    return 0;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
    if (task->is_pushed == false) {
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
    task->is_detach = true;
    return 0;
}

#endif


int
thread_task_join(struct thread_task *task, void **result)
{
    return thread_task_timed_join(task, INT_MAX, result);
}

