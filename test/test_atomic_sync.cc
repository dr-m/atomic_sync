#include <cstdio>
#include <thread>
#include <cassert>
#include "atomic_mutex.h"
#include "atomic_shared_mutex.h"
#include "atomic_recursive_shared_mutex.h"
#include "atomic_condition_variable.h"
#include "transactional_lock_guard.h"

static std::atomic<bool> critical;

constexpr unsigned N_THREADS = 30;
constexpr unsigned N_ROUNDS = 100;
constexpr unsigned M_ROUNDS = 100;

static atomic_spin_mutex m;

#if !defined WITH_ELISION || defined NDEBUG
# define transactional_assert(x) assert(x)
#else
# define transactional_assert(x) if (!x) goto abort;
#endif

TRANSACTIONAL_TARGET static void test_atomic_mutex()
{
  for (auto i = N_ROUNDS * M_ROUNDS; i--; )
  {
    transactional_lock_guard<atomic_spin_mutex> g{m};
    transactional_assert(!critical);
    critical = true;
    critical = false;
  }
#if !defined WITH_ELISION || defined NDEBUG
#else
  return;
abort:
  abort();
#endif
}

static atomic_condition_variable cv;

TRANSACTIONAL_TARGET static void test_condition_variable()
{
  transactional_lock_guard<atomic_spin_mutex> g{m};
  if (critical)
    return;
#ifdef WITH_ELISION
  if (!critical && g.was_elided())
    xabort();
#endif
  while (!critical)
    cv.wait(m);
}

static atomic_spin_shared_mutex sux;

TRANSACTIONAL_TARGET static void test_shared_mutex()
{
  for (auto i = N_ROUNDS; i--; )
  {
    {
      transactional_lock_guard<atomic_spin_shared_mutex> g{sux};
      transactional_assert(!critical);
      critical = true;
      critical = false;
    }

    for (auto j = M_ROUNDS; j--; )
    {
      transactional_shared_lock_guard<atomic_spin_shared_mutex> g{sux};
      transactional_assert(!critical);
    }

    for (auto j = M_ROUNDS; j--; )
    {
      transactional_update_lock_guard<atomic_spin_shared_mutex> g{sux};
      transactional_assert(!critical);
      if (!g.was_elided())
        sux.update_lock_upgrade();
      transactional_assert(!critical);
      critical = true;
      critical = false;
      if (!g.was_elided())
        sux.lock_update_downgrade();
    }
  }
#if !defined WITH_ELISION || defined NDEBUG
#else
  return;
abort:
  abort();
#endif
}

TRANSACTIONAL_TARGET static void test_shared_condition_variable()
{
  transactional_shared_lock_guard<atomic_spin_shared_mutex> g{sux};
#ifdef WITH_ELISION
  if (!critical && g.was_elided())
    xabort();
#endif
  while (!critical)
    cv.wait_shared(sux);
}

static atomic_spin_recursive_shared_mutex recursive_sux;

static void test_recursive_shared_mutex()
{
  for (auto i = N_ROUNDS; i--; )
  {
    recursive_sux.lock();
    assert(!critical);
    critical = true;
    for (auto j = M_ROUNDS; j--; )
      recursive_sux.lock();
    for (auto j = M_ROUNDS; j--; )
      recursive_sux.unlock();
    assert(critical);
    critical = false;
    recursive_sux.unlock();

    for (auto j = M_ROUNDS; j--; )
    {
      recursive_sux.lock_shared();
      assert(!critical);
      recursive_sux.unlock_shared();
    }

    for (auto j = M_ROUNDS / 2; j--; )
    {
      recursive_sux.lock_update();
      assert(!critical);
      recursive_sux.lock_update();
      recursive_sux.update_lock_upgrade();
      assert(!critical);
      critical = true;
      recursive_sux.unlock();
      assert(critical);
      critical = false;
      recursive_sux.lock_update_downgrade();
      recursive_sux.unlock_update();
    }
  }
}

TRANSACTIONAL_TARGET
int main(int, char **)
{
  std::thread t[N_THREADS];

#ifdef WITH_ELISION
  fputs(have_transactional_memory ? "transactional " : "non-transactional ",
        stderr);
#endif

#ifdef SPINLOOP
  fputs("atomic_spin_mutex", stderr);
#else
  fputs("atomic_mutex", stderr);
#endif

  assert(!m.is_locked_or_waiting());
  for (auto i = N_THREADS; i--; )
    t[i]= std::thread(test_atomic_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();
  assert(!m.is_locked_or_waiting());

  for (auto j = N_ROUNDS; j--; )
  {
    for (auto i = N_THREADS; i--; )
      t[i]= std::thread(test_condition_variable);
    bool is_waiting;
    {
      transactional_lock_guard<atomic_spin_mutex> g{m};
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

#ifdef SPINLOOP
  fputs(", atomic_spin_shared_mutex", stderr);
#else
  fputs(", atomic_shared_mutex", stderr);
#endif

  assert(!sux.is_locked_or_waiting());
  for (auto i = N_THREADS; i--; )
    t[i]= std::thread(test_shared_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();
  assert(!sux.is_locked_or_waiting());

  for (auto j = N_ROUNDS; j--; )
  {
    for (auto i = N_THREADS; i--; )
      t[i]= std::thread(test_shared_condition_variable);
    bool is_waiting;
    {
      transactional_lock_guard<atomic_spin_shared_mutex> g{sux};
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

#ifdef SPINLOOP
  fputs(", atomic_spin_recursive_shared_mutex", stderr);
#else
  fputs(", atomic_recursive_shared_mutex", stderr);
#endif

  recursive_sux.init();
  for (auto i = N_THREADS; i--; )
    t[i]= std::thread(test_recursive_shared_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();
  recursive_sux.destroy();

  fputs(".\n", stderr);

  return 0;
}
