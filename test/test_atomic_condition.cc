#include <cstdio>
#include <thread>
#include <cassert>
#include "atomic_mutex.h"
#include "atomic_shared_mutex.h"
#include "atomic_condition_variable.h"
#include "transactional_lock_guard.h"

static unsigned pending;

constexpr unsigned N_THREADS = 30;
constexpr unsigned N_ROUNDS = 100;

static atomic_mutex<> m;
static atomic_shared_mutex<> sux;
static atomic_condition_variable cv;

TRANSACTIONAL_TARGET static void test_condition_variable()
{
  transactional_lock_guard<typeof m> g{m};
  if (pending)
  {
    pending--;
    return;
  }
#ifdef WITH_ELISION
  if (!pending && g.was_elided())
    xabort();
#endif
  while (!pending)
    cv.wait(m);
  pending--;
}

#include <condition_variable>
static std::condition_variable_any cva;

TRANSACTIONAL_TARGET static void test_condition_variable_any()
{
  transactional_lock_guard<typeof m> g{m};
  if (pending)
  {
    pending--;
    return;
  }
#ifdef WITH_ELISION
  if (!pending && g.was_elided())
    xabort();
#endif
  while (!pending)
    cva.wait(m);
  pending--;
}

TRANSACTIONAL_TARGET static void test_shared_condition_variable()
{
  transactional_shared_lock_guard<typeof sux> g{sux};
#ifdef WITH_ELISION
  if (!pending && g.was_elided())
    xabort();
#endif
  while (!pending)
    cv.wait_shared(sux);
}

TRANSACTIONAL_TARGET
int main(int, char **)
{
  std::thread t[N_THREADS];

#ifdef WITH_ELISION
  fputs(have_transactional_memory
        ? "condition variables with transactional "
        : "condition variables with non-transactional ",
        stderr);
#else
  fputs("condition variables with ", stderr);
#endif

  for (auto j = N_ROUNDS; j--; )
  {
    for (auto i = N_THREADS; i--; )
      t[i] = std::thread(test_condition_variable);
    for (auto i = N_THREADS; i--; )
    {
      transactional_lock_guard<typeof m> g{m};
      pending++;
      if (cv.is_waiting())
        cv.signal();
    }
    for (auto i = N_THREADS; i--; )
      t[i].join();
    assert(!cv.is_waiting());
    assert(!pending);
  }

  fputs("atomic_mutex ", stderr);

  for (auto j = N_ROUNDS; j--; )
  {
    for (auto i = N_THREADS; i--; )
      t[i] = std::thread(test_condition_variable_any);
    for (auto i = N_THREADS; i--; )
    {
      transactional_lock_guard<typeof m> g{m};
      pending++;
      cva.notify_one();
    }
    for (auto i = N_THREADS; i--; )
      t[i].join();
    assert(!pending);
  }

  fputs("(any), ", stderr);

  for (auto j = N_ROUNDS; j--; )
  {
    for (auto i = N_THREADS; i--; )
      t[i] = std::thread(test_shared_condition_variable);
    bool is_waiting;
    {
      transactional_lock_guard<typeof sux> g{sux};
      pending = 1;
      is_waiting = cv.is_waiting();
    }
    if (is_waiting)
      cv.broadcast();
    for (auto i = N_THREADS; i--; )
      t[i].join();
    assert(!cv.is_waiting());
    pending = 0;
  }

  fputs("atomic_shared_mutex.\n", stderr);
  return 0;
}
