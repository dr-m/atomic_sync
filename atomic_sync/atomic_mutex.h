#pragma once
#include <atomic>
#include <cassert>
#include "tsan.h"

template<typename T = uint32_t>
struct mutex_storage : std::atomic<T>
{
  using type = T;

  static constexpr type HOLDER = type(~(type(~type(0)) >> 1));
  static constexpr type WAITER = 1;

  static unsigned spin_rounds;

#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
  void notify_one() noexcept;
  inline void wait(T old) const noexcept;
#endif

  void wait_and_lock() noexcept;
  void spin_wait_and_lock() noexcept;

  // for atomic_shared_mutex
  void lock_wait(T lk) noexcept;
};

/** Tiny, non-recursive mutex that keeps a count of waiters.

The interface intentionally resembles std::mutex.
We do not define native_handle().

We define spin_lock(), which is like lock(), but with an initial spinloop.

The implementation counts pending lock() requests, so that unlock()
will only invoke notify_one() when pending requests exist. */
template<typename storage = mutex_storage<>>
class atomic_mutex : public storage
{
  using type = typename storage::type;
  static constexpr auto WAITER = storage::WAITER;
  static constexpr auto HOLDER = storage::HOLDER;

  /** @return whether the mutex was acquired */
  bool try_lock_low() noexcept
  {
    type lk = 0;
    return this->compare_exchange_strong(lk, HOLDER + WAITER,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed);
  }
public:
#ifdef __SANITIZE_THREAD__
  constexpr atomic_mutex()
  { __tsan_mutex_create(this, __tsan_mutex_linker_init); }
  constexpr ~atomic_mutex()
  { __tsan_mutex_destroy(this, __tsan_mutex_linker_init); }
#else
  /** Default constructor */
  constexpr atomic_mutex() = default;
#endif
  /** No copy constructor */
  atomic_mutex(const atomic_mutex&) = delete;
  /** No assignment operator */
  atomic_mutex& operator=(const atomic_mutex&) = delete;

  /** @return whether the mutex was acquired */
  bool try_lock() noexcept
  {
    __tsan_mutex_pre_lock(this, __tsan_mutex_try_lock);
    bool locked = try_lock_low();
    __tsan_mutex_post_lock(this, locked
                           ? __tsan_mutex_try_lock
                           : __tsan_mutex_try_lock_failed, 0);
    return locked;
  }

  void lock() noexcept
  {
    __tsan_mutex_pre_lock(this, 0);
    if (!try_lock_low())
      this->wait_and_lock();
    __tsan_mutex_post_lock(this, 0, 0);
  }
  void spin_lock() noexcept
  {
    __tsan_mutex_pre_lock(this, 0);
    if (!try_lock_low())
      this->spin_wait_and_lock();
    __tsan_mutex_post_lock(this, 0, 0);
  }
  void unlock() noexcept
  {
    __tsan_mutex_pre_unlock(this, 0);
    const type lk =
      this->fetch_sub(HOLDER + WAITER, std::memory_order_release);
    __tsan_mutex_post_unlock(this, 0);
    if (lk != HOLDER + WAITER)
    {
      assert(lk & HOLDER);
      __tsan_mutex_pre_signal(this, 0);
      this->notify_one();
      __tsan_mutex_post_signal(this, 0);
    }
  }
};

/** Like atomic_mutex, but with a spinloop in lock() */
template<typename storage = mutex_storage<>>
class atomic_spin_mutex : public atomic_mutex<storage>
{
public:
  void lock() noexcept { atomic_mutex<storage>::spin_lock(); }
};
