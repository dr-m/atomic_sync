#include <cstdio>
#include <thread>
#include <cassert>
#include "atomic_mutex.h"
#include "atomic_shared_mutex.h"
#include "atomic_condition_variable.h"
#include "transactional_lock_guard.h"
#include "transactional_mutex_storage.h"

static bool critical;

constexpr unsigned N_THREADS = 30;
constexpr unsigned N_ROUNDS = 100;

static atomic_mutex<transactional_mutex_storage> m;
static atomic_shared_mutex<transactional_shared_mutex_storage> sux;
static atomic_condition_variable cv;

TRANSACTIONAL_TARGET static void test_condition_variable()
{
  transactional_lock_guard<typeof m> g{m};
  if (critical)
    return;
#ifdef WITH_ELISION
  if (!critical && g.was_elided())
    xabort();
#endif
  while (!critical)
    cv.wait(m);
}

#include <condition_variable>
static std::condition_variable_any cva;

TRANSACTIONAL_TARGET static void test_condition_variable_any()
{
  transactional_lock_guard<typeof m> g{m};
  if (critical)
    return;
#ifdef WITH_ELISION
  if (!critical && g.was_elided())
    xabort();
#endif
  while (!critical)
    cva.wait(m);
}

TRANSACTIONAL_TARGET static void test_shared_condition_variable()
{
  transactional_shared_lock_guard<typeof sux> g{sux};
#ifdef WITH_ELISION
  if (!critical && g.was_elided())
    xabort();
#endif
  while (!critical)
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
    bool is_waiting;
    {
      transactional_lock_guard<typeof m> g{m};
      critical = true;
      is_waiting = cv.is_waiting();
    }
    if (is_waiting)
      cv.broadcast();
    for (auto i = N_THREADS; i--; )
      t[i].join();
    assert(!cv.is_waiting());
    critical = false;
  }

  fputs("atomic_mutex ", stderr);

  for (auto j = N_ROUNDS; j--; )
  {
    for (auto i = N_THREADS; i--; )
      t[i] = std::thread(test_condition_variable_any);
    {
      std::lock_guard<typeof m> g{m};
      critical = true;
      cva.notify_all();
    }
    for (auto i = N_THREADS; i--; )
      t[i].join();
    critical = false;
  }

  fputs("(any), ", stderr);

  for (auto j = N_ROUNDS; j--; )
  {
    for (auto i = N_THREADS; i--; )
      t[i] = std::thread(test_shared_condition_variable);
    bool is_waiting;
    {
      transactional_lock_guard<typeof sux> g{sux};
      critical = true;
      is_waiting = cv.is_waiting();
    }
    if (is_waiting)
      cv.broadcast();
    for (auto i = N_THREADS; i--; )
      t[i].join();
    assert(!cv.is_waiting());
    critical = false;
  }

  fputs("atomic_shared_mutex.\n", stderr);
  return 0;
}
