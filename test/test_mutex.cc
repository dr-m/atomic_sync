#include <cstdio>
#include <cstdlib>
#include <thread>
#include <cassert>
#include <vector>
#include <chrono>

#include <mutex>
#include "atomic_mutex.h"

static bool critical;

static unsigned long N_THREADS;
static unsigned long N_ROUNDS;

static atomic_mutex<> a_m;

static void test_atomic_mutex()
{
  for (auto i = N_ROUNDS; i; i--)
  {
    std::lock_guard<atomic_mutex<>> g{a_m};
    assert(!critical);
    critical = true;
    critical = false;
  }
}

#if defined WITH_SPINLOOP && defined SPINLOOP
/** Like atomic_mutex, but with a spinloop in lock() */
template<typename storage = mutex_storage<>>
class atomic_spin_mutex : public atomic_mutex<storage>
{
public:
  void lock() noexcept { atomic_mutex<storage>::spin_lock(SPINLOOP); }
};

static atomic_spin_mutex<> a_sm;

static void test_atomic_spin_mutex()
{
  for (auto i = N_ROUNDS; i; i--)
  {
    std::lock_guard<atomic_spin_mutex<>> g{a_sm};
    assert(!critical);
    critical = true;
    critical = false;
  }
}
#endif

static std::mutex m;

static void test_mutex()
{
  for (auto i = N_ROUNDS; i; i--)
  {
    std::lock_guard<std::mutex> g{m};
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

  const auto start_atomic_mutex = std::chrono::steady_clock::now();

  for (auto i = N_THREADS; i--; )
    t[i] = std::thread(test_atomic_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();

#if defined WITH_SPINLOOP && defined SPINLOOP
  const auto start_atomic_spin_mutex = std::chrono::steady_clock::now();

  for (auto i = N_THREADS; i--; )
    t[i] = std::thread(test_atomic_spin_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();
#endif
  const auto start_mutex = std::chrono::steady_clock::now();

  for (auto i = N_THREADS; i--; )
    t[i] = std::thread(test_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();

  const auto start_output = std::chrono::steady_clock::now();
  using duration = std::chrono::duration<double>;
#if defined WITH_SPINLOOP && defined SPINLOOP
  fprintf(stderr, "atomic_mutex: %lfs, atomic_spin_mutex: %lfs, mutex: %lfs\n",
          duration{start_atomic_spin_mutex - start_atomic_mutex}.count(),
          duration{start_mutex - start_atomic_spin_mutex}.count(),
          duration{start_output - start_mutex}.count());
#else
  fprintf(stderr, "atomic_mutex: %lfs, mutex: %lfs\n",
          duration{start_mutex - start_atomic_mutex}.count(),
          duration{start_output - start_mutex}.count());
#endif

  return 0;
}
