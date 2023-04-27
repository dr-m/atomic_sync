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

static atomic_mutex a_m;

static void test_atomic_mutex()
{
  for (auto i = N_ROUNDS; i; i--)
  {
    std::lock_guard<atomic_mutex> g{a_m};
    assert(!critical);
    critical = true;
    critical = false;
  }
}

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

  const auto start_mutex = std::chrono::steady_clock::now();

  for (auto i = N_THREADS; i--; )
    t[i] = std::thread(test_mutex);
  for (auto i = N_THREADS; i--; )
    t[i].join();

  const auto start_output = std::chrono::steady_clock::now();
  using duration = std::chrono::duration<double>;
  fprintf(stderr, "atomic_mutex: %lfs, mutex: %lfs\n",
          duration{start_mutex - start_atomic_mutex}.count(),
          duration{start_output - start_mutex}.count());

  return 0;
}
