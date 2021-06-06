#include <cstdio>
#include <thread>
#include <cassert>
#include "atomic_mutex.h"
#include "atomic_sux_lock.h"
#include "atomic_recursive_sux_lock.h"

static std::atomic<bool> critical;

constexpr unsigned N_THREADS = 30;
constexpr unsigned N_ROUNDS = 100;
constexpr unsigned M_ROUNDS = 100;

static atomic_mutex m;

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

static atomic_sux_lock sux;

static void test_sux_lock()
{
  for (auto i = N_ROUNDS; i--; )
  {
    sux.x_lock();
    assert(!critical);
    critical = true;
    critical = false;
    sux.x_unlock();

    for (auto j = M_ROUNDS; j--; )
    {
      sux.s_lock();
      assert(!critical);
      sux.s_unlock();
    }

    for (auto j = M_ROUNDS; j--; )
    {
      sux.u_lock();
      assert(!critical);
      sux.u_x_upgrade();
      assert(!critical);
      critical = true;
      critical = false;
      sux.x_u_downgrade();
      sux.u_unlock();
    }
  }
}

static atomic_recursive_sux_lock recursive_sux;

static void test_recursive_sux_lock()
{
  for (auto i = N_ROUNDS; i--; )
  {
    recursive_sux.x_lock();
    assert(!critical);
    critical = true;
    for (auto j = M_ROUNDS; j--; )
      recursive_sux.x_lock();
    for (auto j = M_ROUNDS; j--; )
      recursive_sux.x_unlock();
    assert(critical);
    critical = false;
    recursive_sux.x_unlock();

    for (auto j = M_ROUNDS; j--; )
    {
      recursive_sux.s_lock();
      assert(!critical);
      recursive_sux.s_unlock();
    }

    for (auto j = M_ROUNDS / 2; j--; )
    {
      recursive_sux.u_lock();
      assert(!critical);
      recursive_sux.u_lock();
      recursive_sux.u_x_upgrade();
      assert(!critical);
      critical = true;
      recursive_sux.x_unlock();
      assert(critical);
      critical = false;
      recursive_sux.x_u_downgrade();
      recursive_sux.u_unlock();
    }
  }
}

int main(int, char **)
{
  std::thread t[N_THREADS];

#ifdef SPINLOOP
  atomic_mutex::spin_rounds = 10;
#endif

  fputs("atomic_mutex", stderr);

  m.init();
  for (auto i = N_THREADS; i--; )
    t[i]= std::thread(test_atomic_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();
  m.destroy();

  fputs(", atomic_sux_lock", stderr);

  sux.init();
  for (auto i = N_THREADS; i--; )
    t[i]= std::thread(test_sux_lock);
  for (auto i = N_THREADS; i--; )
    t[i].join();
  sux.destroy();

  fputs(", atomic_recursive_sux_lock", stderr);

  recursive_sux.init();
  for (auto i = N_THREADS; i--; )
    t[i]= std::thread(test_recursive_sux_lock);
  for (auto i = N_THREADS; i--; )
    t[i].join();
  recursive_sux.destroy();

  fputs(".\n", stderr);

  return 0;
}
