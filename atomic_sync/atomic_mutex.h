#pragma once
#include <atomic>
#include <cassert>

/** Tiny, non-recursive mutex that keeps a count of waiters.

We count pending lock() requests, so that unlock() will only invoke
notify_one() when pending requests exist. */
class atomic_mutex : std::atomic<uint32_t>
{
#ifdef _WIN32
#elif __cplusplus >= 202002L
#else /* Emulate the C++20 primitives */
  void notify_one() noexcept;
  inline void wait(uint32_t old) const noexcept;
#endif
  /** A flag identifying that the lock is being held */
  static constexpr uint32_t HOLDER = 1U << 31;
  /** Wait until the mutex has been acquired */
  void wait_and_lock() noexcept;
public:
#ifdef SPINLOOP
# ifdef _MSC_VER
  __declspec(dllexport)
# endif
  /** number of spin loops in wait_and_lock() */
  static unsigned spin_rounds;
#endif

  /** @return whether the mutex is being held or waited for */
  bool is_locked_or_waiting() const
  { return load(std::memory_order_relaxed) != 0; }
  /** @return whether the mutex is being held by any thread */
  bool is_locked() const
  { return (load(std::memory_order_relaxed) & HOLDER) != 0; }

  void init() { assert(!is_locked_or_waiting()); }
  void destroy() { assert(!is_locked_or_waiting()); }

  /** @return whether the mutex was acquired */
  bool trylock() noexcept
  {
    uint32_t lk = 0;
    return compare_exchange_strong(lk, HOLDER + 1, std::memory_order_acquire,
                                   std::memory_order_relaxed);
  }

  void lock() noexcept { if (!trylock()) wait_and_lock(); }
  void unlock() noexcept
  {
    const uint32_t lk = fetch_sub(HOLDER + 1, std::memory_order_release);
    if (lk != HOLDER + 1)
    {
      assert(lk & HOLDER);
      notify_one();
    }
  }
};
