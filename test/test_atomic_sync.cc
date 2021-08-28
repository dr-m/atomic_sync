#include <cstdio>
#include <thread>
#include <cassert>
#include "atomic_mutex.h"
#include "atomic_shared_mutex.h"
#include "atomic_recursive_shared_mutex.h"

static std::atomic<bool> critical;

constexpr unsigned N_THREADS = 30;
constexpr unsigned N_ROUNDS = 100;
constexpr unsigned M_ROUNDS = 100;

static atomic_spin_mutex m;

static void test_atomic_mutex()
{
  for (auto i = N_ROUNDS * M_ROUNDS; i--; )
  {
    m.lock();
    assert(!critical);
    critical = true;
    critical = false;
    m.unlock();
  }
}

static atomic_spin_shared_mutex sux;

static void test_shared_mutex()
{
  for (auto i = N_ROUNDS; i--; )
  {
    sux.lock();
    assert(!critical);
    critical = true;
    critical = false;
    sux.unlock();

    for (auto j = M_ROUNDS; j--; )
    {
      sux.lock_shared();
      assert(!critical);
      sux.unlock_shared();
    }

    for (auto j = M_ROUNDS; j--; )
    {
      sux.lock_update();
      assert(!critical);
      sux.update_lock_upgrade();
      assert(!critical);
      critical = true;
      critical = false;
      sux.lock_update_downgrade();
      sux.unlock_update();
    }
  }
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

int main(int, char **)
{
  std::thread t[N_THREADS];

#ifdef SPINLOOP
  atomic_spin_mutex::spin_rounds = 10;
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
