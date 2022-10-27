#include "include/thread_pool.h"

#include <stdlib.h>

#include "thread_pool_impl.h"

static int thread_func_wrapper(void *arg) {
  struct thread_args *thread_args = arg;

  // as long as the thread shouldn't terminate
  while (!atomic_load(&thread_args->self->terminate)) {
    mtx_lock(thread_args->tasks_mtx);  // assumes never fails
    // there're no tasks
    if (list_size(thread_args->tasks) == 0) {
      cnd_wait(thread_args->tasks_cnd, thread_args->tasks_mtx);

      if (atomic_load(&thread_args->self->terminate)) {
        mtx_unlock(thread_args->tasks_mtx);
        goto end_lbl;
      }
    }

    struct task *task = list_remove_first(thread_args->tasks);

    mtx_unlock(thread_args->tasks_mtx);  // assumes never fails

    // handle the task
    // as long as task is valid the thread shouldn't stop

    if (task) {
      if (task->handle_task) task->handle_task(task->args);
      if (thread_args->destroy_task) thread_args->destroy_task(task);
      free(task);
    }
  }

end_lbl:
  free(thread_args);

  return 0;
}

static void cleanup(struct thread_pool *thread_pool, bool tasks_mtx, bool tasks_cnd) {
  if (!thread_pool) return;

  if (thread_pool->threads) free(thread_pool->threads);

  if (thread_pool->tasks) list_destroy(thread_pool->tasks, thread_pool->destroy_task);

  if (tasks_mtx) mtx_destroy(&thread_pool->tasks_mtx);

  if (tasks_cnd) cnd_destroy(&thread_pool->tasks_cnd);

  free(thread_pool);
}

struct thread_pool *thread_pool_init(uint8_t num_of_threads, void (*destroy_task)(void *task)) {
  if (num_of_threads == 0) return NULL;

  struct thread_pool *thread_pool = calloc(1, sizeof *thread_pool);
  if (!thread_pool) return NULL;

  thread_pool->num_of_threads = num_of_threads;

  // init threads
  thread_pool->threads = calloc(num_of_threads, sizeof *thread_pool->threads);
  if (!thread_pool->threads) {
    cleanup(thread_pool, false, false);
    return NULL;
  }

  // init tasks
  thread_pool->tasks = list_init();
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

  thread_pool->destroy_task = destroy_task;

  // creates the threads
  for (uint8_t i = 0; i < num_of_threads; i++) {
    struct thread_args *thread_args = calloc(1, sizeof *thread_args);
    if (!thread_args) cleanup(thread_pool, true, true);

    thread_args->tasks = thread_pool->tasks;
    thread_args->tasks_mtx = &thread_pool->tasks_mtx;
    thread_args->tasks_cnd = &thread_pool->tasks_cnd;
    thread_args->self = &thread_pool->threads[i];
    thread_args->destroy_task = thread_pool->destroy_task;

    atomic_init(&thread_pool->threads[i].terminate, false);

    /* the thread takes owership of thread_args. its his responsibility to free
     * it at the end*/
    thrd_create(&thread_pool->threads[i].thread, thread_func_wrapper,
                thread_args);  // assumes never fails
  }

  return thread_pool;
}

void thread_pool_destroy(struct thread_pool *thread_pool) {
  if (!thread_pool) return;

  // signal all threads to terminate
  for (uint8_t i = 0; i < thread_pool->num_of_threads; i++) {
    atomic_store(&thread_pool->threads[i].terminate, true);
  }

  // wakeup all threads
  cnd_broadcast(&thread_pool->tasks_cnd);

  // wait on all threads to exit
  for (uint8_t i = 0; i < thread_pool->num_of_threads; i++) {
    thrd_join(thread_pool->threads[i].thread, NULL);
  }

  cleanup(thread_pool, true, true);
}

bool thread_pool_add_task(struct thread_pool *thread_pool, struct task *task) {
  if (!thread_pool) return false;

  if (!task) return false;

  mtx_lock(&thread_pool->tasks_mtx);  // assumes never fails

  bool ret = list_append(thread_pool->tasks, task, sizeof *task);

  // wakeup all threads
  if (ret) cnd_broadcast(&thread_pool->tasks_cnd);

  mtx_unlock(&thread_pool->tasks_mtx);  // assumes never fails

  return ret;
}