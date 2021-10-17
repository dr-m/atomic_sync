#pragma once
#include <atomic>
#include <cassert>

/** Tiny, non-recursive mutex that keeps a count of waiters.

The interface intentionally resembles std::mutex.
We do not define native_handle().

We define the predicates is_locked_or_waiting() and is_locked().

We define spin_lock(), which is like lock(), but with an initial spinloop.

The object is expected to be zero-initialized, so that
!is_locked_or_waiting() will hold.

The implementation counts pending lock() requests, so that unlock()
will only invoke notify_one() when pending requests exist. */
class atomic_mutex : std::atomic<uint32_t>
{
  /** number of spin loops in spin_wait_and_lock() */
  static unsigned spin_rounds;
#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
  void notify_one() noexcept;
  inline void wait(uint32_t old) const noexcept;
#endif
  /** A flag identifying that the lock is being held */
  static constexpr uint32_t HOLDER = 1U << 31;
  /** Wait until the mutex has been acquired, with initial spinloop */
  void spin_wait_and_lock() noexcept;
  /** Wait until the mutex has been acquired */
  void wait_and_lock() noexcept;
public:
  /** Default constructor */
  constexpr atomic_mutex() : std::atomic<uint32_t>(0) {}
  /** No copy constructor */
  atomic_mutex(const atomic_mutex&) = delete;
  /** No assignment operator */
  atomic_mutex& operator=(const atomic_mutex&) = delete;

  /** @return whether the mutex is being held or waited for */
  bool is_locked_or_waiting() const noexcept
  { return load(std::memory_order_acquire) != 0; }
  /** @return whether the mutex is being held by any thread */
  bool is_locked() const noexcept
  { return (load(std::memory_order_acquire) & HOLDER) != 0; }

  /** @return whether the mutex was acquired */
  bool try_lock() noexcept
  {
    uint32_t lk = 0;
    return compare_exchange_strong(lk, HOLDER + 1,
                                   std::memory_order_acquire,
                                   std::memory_order_relaxed);
  }

  void lock() noexcept { if (!try_lock()) wait_and_lock(); }
  void spin_lock() noexcept { if (!try_lock()) spin_wait_and_lock(); }
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

/** Like atomic_mutex, but with a spinloop in lock() */
class atomic_spin_mutex : public atomic_mutex
{
public:
  void lock() noexcept { spin_lock(); }
};
