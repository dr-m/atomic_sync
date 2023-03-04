#pragma once
#include <atomic>
#include <cassert>
#include "tsan.h"

template<typename T = uint32_t>
class mutex_storage :
#if !defined _WIN32 && __cplusplus < 202002L
  protected /* Emulate C++20 for shared_mutex_storage */
#endif
std::atomic<T>
{
  using type = T;

  static constexpr type HOLDER = type(~(type(~type(0)) >> 1));
  static constexpr type WAITER = 1;

#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
protected:
  void notify_one() noexcept;
  inline void wait(T old) const noexcept;
#endif

public:
  constexpr bool is_locked_or_waiting() const noexcept
  { return this->load(std::memory_order_acquire) != 0; }
  constexpr bool is_locked() const noexcept
  { return this->load(std::memory_order_acquire) & HOLDER; }
  constexpr bool is_locked_not_waiting() const noexcept
  { return this->load(std::memory_order_acquire) == HOLDER; }

protected:
  void wait_and_lock() noexcept;
  void spin_wait_and_lock(unsigned spin_rounds) noexcept;

  /** Try to acquire a mutex
  @return whether the mutex was acquired */
  bool lock_impl() noexcept
  {
    type lk = 0;
    return this->compare_exchange_strong(lk, HOLDER + WAITER,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed);
  }
  /** Release a mutex
  @return whether the lock is being waited for */
  bool unlock_impl() noexcept
  {
    T lk= this->fetch_sub(HOLDER + WAITER, std::memory_order_release);
    assert(lk & HOLDER);
    return lk != HOLDER + WAITER;
  }
  /** Notify waiters after unlock_impl() returned true */
  void unlock_notify() noexcept { this->notify_one(); }
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
    bool locked = this->lock_impl();
    __tsan_mutex_post_lock(this, locked
                           ? __tsan_mutex_try_lock
                           : __tsan_mutex_try_lock_failed, 0);
    return locked;
  }

  void lock() noexcept
  {
    __tsan_mutex_pre_lock(this, 0);
    if (!this->lock_impl())
      this->wait_and_lock();
    __tsan_mutex_post_lock(this, 0, 0);
  }
  void spin_lock(unsigned spin_rounds) noexcept
  {
    __tsan_mutex_pre_lock(this, 0);
    if (!this->lock_impl())
      this->spin_wait_and_lock(spin_rounds);
    __tsan_mutex_post_lock(this, 0, 0);
  }
  void unlock() noexcept
  {
    __tsan_mutex_pre_unlock(this, 0);
    bool notify= this->unlock_impl();
    __tsan_mutex_post_unlock(this, 0);
    if (notify)
    {
      __tsan_mutex_pre_signal(this, 0);
      this->unlock_notify();
      __tsan_mutex_post_signal(this, 0);
    }
  }
};
