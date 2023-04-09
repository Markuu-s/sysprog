#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>


struct thread_task {
	thread_task_f function;
	void *arg;

	/* PUT HERE OTHER MEMBERS */
    atomic_bool is_finished;
//    bool is_finished;
    bool is_running;
    pthread_mutex_t run;
    bool is_join;
    bool is_pushed;

    void *returned;
};

struct queue_task {
    struct thread_task *queue[TPOOL_MAX_TASKS];
    int left_current; // is_valid
    int right_current; // not_valid
    int sz;
    pthread_mutex_t mutex;
};

void init_queue_task(struct queue_task* queue_task) {
    queue_task->left_current = 0;
    queue_task->right_current = 0;
    queue_task->sz = 0;
}

struct thread_task* get_task(struct queue_task* queue_task) {
    pthread_mutex_lock(&queue_task->mutex);
    if (queue_task->sz == 0) {
        return NULL;
    }
    queue_task->sz--;
    struct thread_task* thread_task = queue_task->queue[queue_task->left_current++];
    pthread_mutex_unlock(&queue_task->mutex);
    return thread_task;
}

struct thread_state {
    pthread_t thread;
    bool is_working;
};

struct thread_pool {
    struct thread_state *threads;
    int size;
    int capacity;
    int busy_now;

    pthread_t main_thread;
    struct queue_task queue_task;
    bool is_dead;
};

struct arg_task_thread {
    struct thread_pool * thread_pool;
    int idx;
};

void *task_thread_fun(void* void_arg_task_thread_fun) {
    struct arg_task_thread * arg_task_thread = void_arg_task_thread_fun;

    int idx = arg_task_thread->idx;

    struct thread_pool *thread_pool = arg_task_thread->thread_pool;
    struct queue_task *queue_task = &thread_pool->queue_task;

    while(!thread_pool->is_dead) {
        if (queue_task->sz > 0) {
            struct thread_task* thread_task = get_task(queue_task);
            if (thread_task == NULL) {
                continue;
            }
            pthread_mutex_lock(&thread_task->run);

            thread_pool->busy_now++;
            thread_pool->threads[idx].is_working = true;

            thread_task->is_running = true;
            thread_task->is_finished = false;

            thread_task->returned = thread_task->function(thread_task->arg);

            thread_task->is_running = false;
            thread_task->is_finished = true;

            thread_pool->threads[idx].is_working = false;
            thread_pool->busy_now--;

            pthread_mutex_unlock(&thread_task->run);
        }
    }

    free(arg_task_thread);
    pthread_exit(0);
}


void* main_thread_fun(void * void_thread_pool) {
    struct thread_pool* thread_pool = (struct thread_pool*)void_thread_pool;
    struct queue_task* queue_task = &thread_pool->queue_task;

    while(!thread_pool->is_dead) {
        if (queue_task->sz > 0 && thread_pool->busy_now == thread_pool->size &&
            thread_pool->size < thread_pool->capacity) {

            thread_pool->threads[thread_pool->size].is_working = false;
            struct arg_task_thread *arg_task_thread = calloc(1, sizeof(struct arg_task_thread));
            arg_task_thread->idx = thread_pool->size;
            arg_task_thread->thread_pool = thread_pool;

            pthread_create(&thread_pool->threads->thread,
                           NULL,
                           task_thread_fun,
                           (void*)arg_task_thread);

            thread_pool->size++;
        }
    }
    pthread_exit(0);
}


int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
    if (max_thread_count < 1 || max_thread_count > 20 || pool == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    *pool = calloc(1, sizeof(struct thread_pool));

    (*pool)->capacity = max_thread_count;
    (*pool)->threads = NULL;
    (*pool)->size = 0;
    (*pool)->is_dead = false;
    (*pool)->threads = calloc(max_thread_count, sizeof(struct thread_state));
    (*pool)->is_dead = false;

    init_queue_task(&(*pool)->queue_task);

    pthread_create(&(*pool)->main_thread, NULL, main_thread_fun, (void*)(*pool));
    return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
    return pool->size;
}

int
thread_pool_delete(struct thread_pool *pool)
{
    if (pool->queue_task.sz > 0) {
        return TPOOL_ERR_HAS_TASKS;
    }

    for(int i = 0; i < pool->size; ++i) {
        if ((pool->threads + i) != NULL && pool->threads[i].is_working) {
            return TPOOL_ERR_HAS_TASKS;
        }
    }

    pool->is_dead = true;
    for(int i = 0; i < pool->size; ++i) {
        pthread_join(pool->threads[i].thread, NULL);
    }
    pthread_join(pool->main_thread, NULL);

    free(pool->threads);
    free(pool);
    return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
    if (pool->queue_task.sz == TPOOL_MAX_TASKS) {
        return TPOOL_ERR_TOO_MANY_TASKS;
    }
    if (pool == NULL || task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    task->is_pushed = true;

    struct queue_task *queue_task = &pool->queue_task;
    queue_task->queue[queue_task->right_current] = task;
    queue_task->right_current = (queue_task->right_current + 1) % TPOOL_MAX_TASKS;
    ++queue_task->sz;

    return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
    if (task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    *task = calloc(1, sizeof(struct thread_task));

    (*task)->function = function;
    (*task)->arg = arg;
    (*task)->is_finished = false;
    (*task)->is_running = false;
    (*task)->is_join = false;
    (*task)->is_pushed = false;

    return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
    return task->is_finished;
}

bool
thread_task_is_running(const struct thread_task *task)
{
    return task->is_running;
}

int
thread_task_join(struct thread_task *task, void **result)
{
    if (task->is_pushed == false) {
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
//    pthread_mutex_lock(&task->run);

    while(task->is_finished == false);

    *result = task->returned;
    task->is_join = true;

//    pthread_mutex_unlock(&task->run);
    return 0;
}

int
thread_task_delete(struct thread_task *task)
{
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
	/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif
