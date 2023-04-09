#include <cstdio>
#include <thread>
#include <cassert>
#include "atomic_mutex.h"
#include "atomic_shared_mutex.h"
#include "atomic_recursive_shared_mutex.h"
#include "atomic_condition_variable.h"
#include "transactional_lock_guard.h"

static bool critical;

constexpr unsigned N_THREADS = 30;
constexpr unsigned N_ROUNDS = 100;
constexpr unsigned M_ROUNDS = 100;

#if defined WITH_SPINLOOP && defined SPINLOOP
# define ATOMIC_MUTEX_NAME(m) "atomic_spin_" #m
/** Like atomic_mutex, but with a spinloop in lock() */
template<typename storage = mutex_storage<>>
class atomic_spin_mutex : public atomic_mutex<storage>
{
public:
  void lock() noexcept { atomic_mutex<storage>::spin_lock(SPINLOOP); }
};
/** Like atomic_shared_mutex, but with spinloops */
template<typename storage = shared_mutex_storage<>>
class atomic_spin_shared_mutex : public atomic_shared_mutex<storage>
{
public:
  void lock() noexcept { this->spin_lock(SPINLOOP); }
  void shared_lock() noexcept { this->spin_lock_shared(SPINLOOP); }
  void update_lock() noexcept { this->spin_lock_update(SPINLOOP); }
};
template<typename storage = shared_mutex_storage<>>
class atomic_spin_recursive_shared_mutex :
  public atomic_recursive_shared_mutex<storage>
{
public:
  void lock_shared() noexcept { this->spin_lock_shared(SPINLOOP); }
  void lock_update() noexcept { this->spin_lock_update(SPINLOOP); }
  void lock_update_disowned() noexcept
  { this->spin_lock_update_disowned(SPINLOOP); }
  void lock() noexcept { this->spin_lock(SPINLOOP); }
  void lock_disowned() noexcept { this->spin_lock_disowned(SPINLOOP); }
};
#else
# define ATOMIC_MUTEX_NAME(m) "atomic_" #m
# define atomic_spin_mutex atomic_mutex
# define atomic_spin_shared_mutex atomic_shared_mutex
# define atomic_spin_recursive_shared_mutex atomic_recursive_shared_mutex
#endif
static atomic_spin_mutex<> m;

#if !defined WITH_ELISION || defined NDEBUG
# define transactional_assert(x) assert(x)
#else
# define transactional_assert(x) if (!x) goto abort;
#endif

TRANSACTIONAL_TARGET static void test_atomic_mutex()
{
  for (auto i = N_ROUNDS * M_ROUNDS; i--; )
  {
    transactional_lock_guard<typeof m> g{m};
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

static atomic_spin_shared_mutex<> sux;

TRANSACTIONAL_TARGET static void test_shared_mutex()
{
  for (auto i = N_ROUNDS; i--; )
  {
    {
      transactional_lock_guard<typeof sux> g{sux};
      transactional_assert(!critical);
      critical = true;
      critical = false;
    }

    for (auto j = M_ROUNDS; j--; )
    {
      transactional_shared_lock_guard<typeof sux> g{sux};
      transactional_assert(!critical);
    }

    for (auto j = M_ROUNDS; j--; )
    {
      transactional_update_lock_guard<typeof sux> g{sux};
      transactional_assert(!critical);
      if (!g.was_elided())
        sux.update_lock_upgrade();
      transactional_assert(!critical);
      critical = true;
      critical = false;
      if (!g.was_elided())
        sux.update_lock_downgrade();
    }
  }
#if !defined WITH_ELISION || defined NDEBUG
#else
  return;
abort:
  abort();
#endif
}

static atomic_spin_recursive_shared_mutex<> recursive_sux;

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
      recursive_sux.update_lock_downgrade();
      recursive_sux.unlock_update();
    }
  }
}

TRANSACTIONAL_TARGET
int main(int, char **)
{
  std::thread t[N_THREADS];

#ifdef WITH_ELISION
  fputs(have_transactional_memory
        ? "transactional " ATOMIC_MUTEX_NAME(mutex)
        : "non-transactional " ATOMIC_MUTEX_NAME(mutex),
        stderr);
#else
  fputs(ATOMIC_MUTEX_NAME(mutex), stderr);
#endif

  assert(!m.get_storage().is_locked_or_waiting());
  for (auto i = N_THREADS; i--; )
    t[i]= std::thread(test_atomic_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();
  assert(!m.get_storage().is_locked_or_waiting());

  fputs(", " ATOMIC_MUTEX_NAME(shared_mutex), stderr);

  assert(!sux.get_storage().is_locked_or_waiting());
  for (auto i = N_THREADS; i--; )
    t[i]= std::thread(test_shared_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();
  assert(!sux.get_storage().is_locked_or_waiting());

  fputs(", " ATOMIC_MUTEX_NAME(recursive_shared_mutex), stderr);

  recursive_sux.init();
  for (auto i = N_THREADS; i--; )
    t[i]= std::thread(test_recursive_shared_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();
  recursive_sux.destroy();

  fputs(".\n", stderr);

  return 0;
}
