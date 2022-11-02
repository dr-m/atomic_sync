#pragma once
#include <atomic>
#include <cassert>

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

#if defined WITH_ELISION || !defined NDEBUG
  bool is_locked_or_waiting() const noexcept
  { return std::atomic<T>::load(std::memory_order_acquire) != 0; }
  bool is_locked() const noexcept
  { return std::atomic<T>::load(std::memory_order_acquire) & HOLDER; }
  // for atomic_shared_mutex
  bool is_shared_locked() const noexcept
  { return std::atomic<T>::load(std::memory_order_acquire) == HOLDER; }
#endif
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

  /** Wait until the mutex has been acquired, with initial spinloop */
  void spin_wait_and_lock() noexcept;
public:
  /** Default constructor */
  constexpr atomic_mutex() = default;
  /** No copy constructor */
  atomic_mutex(const atomic_mutex&) = delete;
  /** No assignment operator */
  atomic_mutex& operator=(const atomic_mutex&) = delete;

  /** @return whether the mutex was acquired */
  bool try_lock() noexcept
  {
    type lk = 0;
    return storage::compare_exchange_strong(lk, HOLDER + WAITER,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed);
  }

  void lock() noexcept { if (!try_lock()) storage::wait_and_lock(); }
  void spin_lock() noexcept { if (!try_lock()) storage::spin_wait_and_lock(); }
  void unlock() noexcept
  {
    const type lk =
      storage::fetch_sub(HOLDER + WAITER, std::memory_order_release);
    if (lk != HOLDER + WAITER)
    {
      assert(lk & HOLDER);
      storage::notify_one();
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
