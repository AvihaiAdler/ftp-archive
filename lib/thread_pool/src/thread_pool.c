#include "include/thread_pool.h"

#include <stdlib.h>

#include "thread_pool_impl.h"

static int thread_func_wrapper(void *arg) {
  struct thread_args *thread_args = arg;

  while (!atomic_flag_test_and_set(&thread_args->args.self->halt)) {
    atomic_flag_clear(&thread_args->args.self->halt);

    mtx_lock(&thread_args->thread_pool->tasks_mtx);  // assumes never fails
    // there're no tasks
    if (vector_size(thread_args->thread_pool->tasks) == 0) {
      cnd_wait(&thread_args->thread_pool->tasks_cnd,
               &thread_args->thread_pool->tasks_mtx);
    }
    struct task *task = vector_remove_at(thread_args->thread_pool->tasks, 0);

    mtx_unlock(&thread_args->thread_pool->tasks_mtx);  // assumes never fails

    if (task && !atomic_flag_test_and_set(&thread_args->args.self->halt)) {
      atomic_flag_clear(&thread_args->args.self->halt);
      thread_args->args.task = task;
      task->handle_task(&thread_args->args);
      free(task);
    }
  }

  free(thread_args);

  return 0;
}

static void cleanup(struct thread_pool *thread_pool, bool tasks_mtx,
                    bool tasks_cnd) {
  if (!thread_pool) return;

  if (thread_pool->threads) free(thread_pool->threads);

  if (thread_pool->tasks) vector_destroy(thread_pool->tasks, NULL);

  if (tasks_mtx) mtx_destroy(&thread_pool->tasks_mtx);

  if (tasks_cnd) cnd_destroy(&thread_pool->tasks_cnd);

  free(thread_pool);
}

struct thread_pool *thread_pool_init(uint8_t num_of_threads) {
  if (num_of_threads == 0) return NULL;

  struct thread_pool *thread_pool = calloc(1, sizeof *thread_pool);
  if (!thread_pool) return NULL;

  thread_pool->num_of_threads = num_of_threads;

  thread_pool->threads = calloc(num_of_threads, sizeof *thread_pool->threads);
  if (!thread_pool->threads) {
    cleanup(thread_pool, false, false);
    return NULL;
  }

  thread_pool->tasks = vector_init(sizeof(struct task));
  if (!thread_pool->tasks) {
    cleanup(thread_pool, false, false);
    return NULL;
  }

  // init the threads
  for (uint8_t i = 0; i < num_of_threads; i++) {
    struct thread_args *thread_args = calloc(1, sizeof *thread_args);
    if (!thread_args) {
      cleanup(thread_pool, true, true);
    }

    thread_args->thread_pool = thread_pool;
    thread_args->args.self = &thread_pool->threads[i];

    atomic_flag_clear(&thread_pool->threads[i].halt);

    // the tread taks owership of args. its his responsibility to free it at the
    // end
    thrd_create(&thread_pool->threads[i].thread, thread_func_wrapper,
                &thread_args);
  }

  if (mtx_init(&thread_pool->tasks_mtx, mtx_plain) != thrd_success) {
    cleanup(thread_pool, true, false);
    return NULL;
  }

  if (cnd_init(&thread_pool->tasks_cnd) != thrd_success) {
    cleanup(thread_pool, true, true);
    return NULL;
  }

  return thread_pool;
}

void thread_pool_destroy(struct thread_pool *thread_pool) {
  if (!thread_pool) return;

  // signal all threads to shutdown
  for (uint8_t i = 0; i < thread_pool->num_of_threads; i++) {
    // signal the thread to stop
    atomic_flag_test_and_set(&thread_pool->threads[i].halt);

    // wait on the thread to exit
    thrd_join(thread_pool->threads[i].thread, NULL);

    // clear the halt flags of the thread
    atomic_flag_clear(&thread_pool->threads[i].halt);
  }

  cleanup(thread_pool, true, true);
}

bool add_task(struct thread_pool *thread_pool, struct task *task) {
  if (!thread_pool) return false;
  if (!task) return false;

  mtx_lock(&thread_pool->tasks_mtx);  // assumes never fails

  bool ret = vector_push(thread_pool->tasks, task);
  if (ret) {
    cnd_broadcast(&thread_pool->tasks_cnd);
  }

  mtx_unlock(&thread_pool->tasks_mtx);  // assumes never fails

  return ret;
}