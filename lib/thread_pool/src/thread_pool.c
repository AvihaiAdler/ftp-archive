#include "include/thread_pool.h"

#include <stdlib.h>

#include "thread_pool_impl.h"

// somewhat inefficient. if there're tasks but no available threads - manager
// will run
static int manage(void *arg) {
  struct thread_pool *thread_pool = arg;

  while (atomic_flag_test_and_set(&thread_pool->halt) == false) {
    atomic_flag_clear(&thread_pool->halt);

    mtx_lock(&thread_pool->tasks_mtx);       // assume never fails
    if (!vector_size(thread_pool->tasks)) {  // no tasks yet
      // release the lock. wait
      cnd_wait(&thread_pool->tasks_cnd, &thread_pool->tasks_mtx);
    }

    for (uint8_t i = 0; i < thread_pool->num_of_threads; i++) {
      if (atomic_flag_test_and_set(&thread_pool->threads[i].busy) == false) {
        // assumes task != NULL
        struct task *task = vector_remove_at(thread_pool->tasks, 0);

        // the thread take ownership of task and thus it will free() the task

        // if thread creation failed
        struct thread_arg arg = {.self = &thread_pool->threads[i],
                                 .task = task};
        if (thrd_create(&thread_pool->threads[i].thread, task->handle_task,
                        &arg) != thrd_success) {
          add_task(thread_pool, task);
          free(task);
          continue;
        }

        atomic_flag_test_and_set(&thread_pool->threads[i].busy);
        break;
      }
    }

    mtx_unlock(&thread_pool->tasks_mtx);  // assume never fails
  }
  return 0;
}

static void cleanup(struct thread_pool *thread_pool, bool mtx, bool cnd) {
  if (!thread_pool) return;

  if (thread_pool->threads) free(thread_pool->threads);

  if (thread_pool->tasks) vector_destroy(thread_pool->tasks, NULL);

  if (mtx) mtx_destroy(&thread_pool->tasks_mtx);

  if (cnd) cnd_destroy(&thread_pool->tasks_cnd);
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

  // init the threads
  for (uint8_t i = 0; i < num_of_threads; i++) {
    // the thread hasn't been created yet - and thus it's not busy
    atomic_flag_clear(&thread_pool[i].threads->busy);
  }

  thread_pool->tasks = vector_init(sizeof(struct task));
  if (!thread_pool->tasks) {
    cleanup(thread_pool, false, false);
    return NULL;
  }

  if (mtx_init(&thread_pool->tasks_mtx, mtx_plain) != thrd_success) {
    cleanup(thread_pool, true, false);
    return NULL;
  }

  if (cnd_init(&thread_pool->tasks_cnd) != thrd_success) {
    cleanup(thread_pool, true, true);
    return NULL;
  }

  atomic_flag_clear(&thread_pool->halt);

  // init manager thread
  if (thrd_create(&thread_pool->manager.thread, manage, thread_pool) !=
      thrd_success) {
    cleanup(thread_pool, true, true);
    return NULL;
  }
  atomic_flag_test_and_set(&thread_pool->manager.busy);

  return thread_pool;
}

void thread_pool_destroy(struct thread_pool *thread_pool) {
  if (!thread_pool) return;
}

bool add_task(struct thread_pool *thread_pool, struct task *task) {}