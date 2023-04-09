#include <cstdio>
#include <cstdlib>
#include <thread>
#include <cassert>
#include <vector>
#include <chrono>
#include <mutex>

#include "atomic_mutex.h"

#ifndef _WIN32
# include <pthread.h>

class native_mutex_storage
{
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

public:
  /** Try to acquire a mutex
  @return whether the mutex was acquired */
  bool lock_impl() noexcept
  {
    return pthread_mutex_trylock(&mutex) == 0;
  }
  void lock_wait() noexcept { pthread_mutex_lock(&mutex); }
  void spin_lock_wait(unsigned spin_rounds) noexcept; // not defined

  /** Release a mutex
  @return whether the lock is being waited for */
  bool unlock_impl() noexcept
  {
    pthread_mutex_unlock(&mutex);
    return false;
  }
  /** Notify waiters after unlock_impl() returned true */
  void unlock_notify() noexcept {}
};
#else
# include <synchapi.h>

class native_mutex_storage
{
  SRWLOCK mutex = SRWLOCK_INIT;

public:
  /** Try to acquire a mutex
  @return whether the mutex was acquired */
  bool lock_impl() noexcept { return TryAcquireSRWLockExclusive(&mutex); }
  void lock_wait() noexcept { AcquireSRWLockExclusive(&mutex); }
  void spin_lock_wait(unsigned spin_rounds) noexcept; // not defined

  /** Release a mutex
  @return whether the lock is being waited for */
  bool unlock_impl() noexcept
  {
    ReleaseSRWLockExclusive(&mutex);
    return false;
  }
  /** Notify waiters after unlock_impl() returned true */
  void unlock_notify() noexcept {}
};
#endif

static bool critical;

static unsigned long N_THREADS;
static unsigned long N_ROUNDS;

static atomic_mutex<native_mutex_storage> a_m;

static void test_native_mutex()
{
  for (auto i = N_ROUNDS; i; i--)
  {
    std::lock_guard<atomic_mutex<native_mutex_storage>> g{a_m};
    assert(!critical);
    critical = true;
    critical = false;
  }
}

int main(int argc, char **argv)
{
  if (argc != 3)
  {
  usage:
    fprintf(stderr, "usage: %s N_THREADS N_ROUNDS\n", *argv);
    return 1;
  }
  else
  {
    char *endp;
    N_THREADS = strtoul(argv[1], &endp, 0);
    if (endp == argv[1] || *endp)
      goto usage;
    N_ROUNDS = strtoul(argv[2], &endp, 0);
    if (endp == argv[2] || *endp)
      goto usage;
  }

  std::vector<std::thread> t(N_THREADS);

  const auto start_native_mutex = std::chrono::steady_clock::now();

  for (auto i = N_THREADS; i--; )
    t[i] = std::thread(test_native_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();

  const auto start_output = std::chrono::steady_clock::now();
  using duration = std::chrono::duration<double>;
  fprintf(stderr, "native_mutex: %lfs\n",
          duration{start_output - start_native_mutex}.count());
  return 0;
}
