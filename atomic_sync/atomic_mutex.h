#pragma once
#include <atomic>
#include <cassert>
#include "tsan.h"

template<typename Storage> class atomic_mutex;

template<typename T = uint32_t>
class mutex_storage
{
  using type = T;
  // exposition only; see test_native_mutex for a possible alternative
  std::atomic<type> m;

  static constexpr type HOLDER = type(~(type(~type(0)) >> 1));
  static constexpr type WAITER = 1;

public:
  constexpr bool is_locked() const noexcept
  { return m.load(std::memory_order_acquire) & HOLDER; }
  constexpr bool is_locked_or_waiting() const noexcept
  { return m.load(std::memory_order_acquire) != 0; }
  constexpr bool is_locked_not_waiting() const noexcept
  { return m.load(std::memory_order_acquire) == HOLDER; }

private:
  friend class atomic_mutex<mutex_storage>;

  /** @return default argument for spin_lock_wait() */
  unsigned default_spin_rounds();

  /** Try to acquire a mutex
  @return whether the mutex was acquired */
  bool lock_impl() noexcept
  {
    type lk = 0;
    return m.compare_exchange_strong(lk, HOLDER + WAITER,
                                     std::memory_order_acquire,
                                     std::memory_order_relaxed);
  }
  void lock_wait() noexcept;
  void spin_lock_wait(unsigned spin_rounds) noexcept;

  /** Release a mutex
  @return whether the lock is being waited for */
  bool unlock_impl() noexcept
  {
    T lk= m.fetch_sub(HOLDER + WAITER, std::memory_order_release);
    assert(lk & HOLDER);
    return lk != HOLDER + WAITER;
  }
#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
  void notify_one() noexcept;
  inline void wait(T old) const noexcept;
  /** Notify waiters after unlock_impl() returned true */
  void unlock_notify() noexcept { this->notify_one(); }
#else
  /** Notify waiters after unlock_impl() returned true */
  void unlock_notify() noexcept { m.notify_one(); }
#endif
};

/** Tiny, non-recursive mutex that keeps a count of waiters.

The interface intentionally resembles std::mutex.
The counterpart of get_storage() is std::mutex::native_handle().

We define spin_lock(), which is like lock(), but with an initial spinloop.

The implementation counts pending lock() requests, so that unlock()
will only invoke notify_one() when pending requests exist. */
template<typename Storage = mutex_storage<>>
class atomic_mutex
{
  Storage storage;
public:
#ifdef __SANITIZE_THREAD__
  constexpr atomic_mutex()
  { __tsan_mutex_create(&storage, __tsan_mutex_linker_init); }
  constexpr ~atomic_mutex()
  { __tsan_mutex_destroy(&storage, __tsan_mutex_linker_init); }
#else
  /** Default constructor */
  constexpr atomic_mutex() = default;
#endif
  /** No copy constructor */
  atomic_mutex(const atomic_mutex&) = delete;
  /** No assignment operator */
  atomic_mutex& operator=(const atomic_mutex&) = delete;

  constexpr const Storage& get_storage() const { return storage; }

  /** @return whether the mutex was acquired */
  bool try_lock() noexcept
  {
    __tsan_mutex_pre_lock(&storage, __tsan_mutex_try_lock);
    bool locked = storage.lock_impl();
    __tsan_mutex_post_lock(&storage, locked
                           ? __tsan_mutex_try_lock
                           : __tsan_mutex_try_lock_failed, 0);
    return locked;
  }

  void lock() noexcept
  {
    __tsan_mutex_pre_lock(&storage, 0);
    if (!storage.lock_impl())
      storage.lock_wait();
    __tsan_mutex_post_lock(&storage, 0, 0);
  }
  void spin_lock(unsigned spin_rounds) noexcept
  {
    __tsan_mutex_pre_lock(&storage, 0);
    if (!storage.lock_impl())
      storage.spin_lock_wait(spin_rounds);
    __tsan_mutex_post_lock(&storage, 0, 0);
  }
  void spin_lock() noexcept
  { return spin_lock(storage.default_spin_rounds()); }
  void unlock() noexcept
  {
    __tsan_mutex_pre_unlock(&storage, 0);
    bool notify= storage.unlock_impl();
    __tsan_mutex_post_unlock(&storage, 0);
    if (notify)
    {
      __tsan_mutex_pre_signal(&storage, 0);
      storage.unlock_notify();
      __tsan_mutex_post_signal(&storage, 0);
    }
  }
};
