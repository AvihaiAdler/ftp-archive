#include "include/thread_pool.h"

#include <stdlib.h>

#include "thread_pool_impl.h"

static int thrd_func_wrapper(void *arg) {
  struct thrd_args_inner *thread_args = arg;

  // as long as the thread shouldn't terminate
  while (!atomic_load(&thread_args->self->terminate)) {
    mtx_lock(thread_args->tasks_mtx);  // assumes never fails
    // there're no tasks
    if (vector_size(thread_args->tasks) == 0) { cnd_wait(thread_args->tasks_cnd, thread_args->tasks_mtx); }

    struct task *task = vector_remove_at(thread_args->tasks, 0);

    mtx_unlock(thread_args->tasks_mtx);  // assumes never fails

    // handle the task
    // as long as task is valid as the thread shouldn't stop
    if (task && !atomic_load(&thread_args->self->stop_task)) {
      thread_args->args.fd = task->fd;
      thread_args->args.additional_args = task->additional_args;
      thread_args->args.thrd_id = &thread_args->self->thread;
      thread_args->args.stop = &thread_args->self->stop_task;

      task->handle_task(&thread_args->args);
    }

    if (task) free(task);
  }

  free(thread_args);

  return 0;
}

static void cleanup(struct thrd_pool *thread_pool, bool tasks_mtx, bool tasks_cnd) {
  if (!thread_pool) return;

  if (thread_pool->threads) free(thread_pool->threads);

  if (thread_pool->tasks) vector_destroy(thread_pool->tasks, NULL);

  if (tasks_mtx) mtx_destroy(&thread_pool->tasks_mtx);

  if (tasks_cnd) cnd_destroy(&thread_pool->tasks_cnd);

  free(thread_pool);
}

struct thrd_pool *thrd_pool_init(uint8_t num_of_threads) {
  if (num_of_threads == 0) return NULL;

  struct thrd_pool *thread_pool = calloc(1, sizeof *thread_pool);
  if (!thread_pool) return NULL;

  thread_pool->num_of_threads = num_of_threads;

  // init threads
  thread_pool->threads = calloc(num_of_threads, sizeof *thread_pool->threads);
  if (!thread_pool->threads) {
    cleanup(thread_pool, false, false);
    return NULL;
  }

  // init tasks vector
  thread_pool->tasks = vector_init(sizeof(struct task));
  if (!thread_pool->tasks) {
    cleanup(thread_pool, false, false);
    return NULL;
  }

  // init mutex
  if (mtx_init(&thread_pool->tasks_mtx, mtx_plain) != thrd_success) {
    cleanup(thread_pool, true, false);
    return NULL;
  }

  // init condition variable
  if (cnd_init(&thread_pool->tasks_cnd) != thrd_success) {
    cleanup(thread_pool, true, true);
    return NULL;
  }

  // creates the threads
  for (uint8_t i = 0; i < num_of_threads; i++) {
    struct thrd_args_inner *thread_args = calloc(1, sizeof *thread_args);
    if (!thread_args) cleanup(thread_pool, true, true);

    thread_args->tasks = thread_pool->tasks;
    thread_args->tasks_mtx = &thread_pool->tasks_mtx;
    thread_args->tasks_cnd = &thread_pool->tasks_cnd;
    thread_args->self = &thread_pool->threads[i];

    atomic_init(&thread_pool->threads[i].terminate, false);
    atomic_init(&thread_pool->threads[i].stop_task, false);

    /* the thread takes owership of thread_args. its his responsibility to free
     * it at the end*/
    thrd_create(&thread_pool->threads[i].thread, thrd_func_wrapper,
                thread_args);  // assumes never fails
  }

  return thread_pool;
}

bool thrd_pool_stop_thrd(struct thrd_pool *thread_pool, thrd_t thread_id) {
  if (!thread_pool) return false;

  struct thrd *thread = NULL;
  for (uint8_t i = 0; i < thread_pool->num_of_threads; i++) {
    if (thrd_equal(thread_pool->threads[i].thread, thread_id)) { thread = &thread_pool->threads[i]; }

    if (!thread) return false;

    // signal the thread to stop
    atomic_store(&thread->stop_task, true);
    return false;
  }
}

void thrd_pool_destroy(struct thrd_pool *thread_pool) {
  if (!thread_pool) return;

  // signal all threads to terminate
  for (uint8_t i = 0; i < thread_pool->num_of_threads; i++) {
    atomic_store(&thread_pool->threads[i].terminate, true);
    atomic_store(&thread_pool->threads[i].stop_task, true);
  }

  // wakeup all threads
  cnd_broadcast(&thread_pool->tasks_cnd);

  // wait on all threads to exit
  for (uint8_t i = 0; i < thread_pool->num_of_threads; i++) {
    thrd_join(thread_pool->threads[i].thread, NULL);
  }

  cleanup(thread_pool, true, true);
}

bool thrd_pool_add_task(struct thrd_pool *thread_pool, struct task *task) {
  if (!thread_pool) return false;

  if (!task) return false;

  mtx_lock(&thread_pool->tasks_mtx);  // assumes never fails

  bool ret = vector_push(thread_pool->tasks, task);

  // wakeup all threads
  if (ret) cnd_broadcast(&thread_pool->tasks_cnd);

  mtx_unlock(&thread_pool->tasks_mtx);  // assumes never fails

  return ret;
}