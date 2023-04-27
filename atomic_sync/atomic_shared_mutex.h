#pragma once
#include "atomic_mutex.h"
#include <cassert>

template<typename Storage> class atomic_shared_mutex;

template<typename T = uint32_t>
class shared_mutex_storage
{
  // exposition only
  std::atomic<T> inner;
  atomic_mutex outer;
  using type = T;
  static constexpr type X = type(~(type(~type(0)) >> 1));
  static constexpr type WAITER = 1;

public:
  constexpr bool is_locked() const noexcept
  { return inner.load(std::memory_order_acquire) == X; }
  constexpr bool is_locked_or_waiting() const noexcept
  { return outer.is_locked_or_waiting() || is_locked(); }
private:
  friend class atomic_shared_mutex<shared_mutex_storage>;

  void lock_outer() noexcept { outer.lock(); }
  void unlock_outer() noexcept { outer.unlock(); }

  /** Try to acquire a shared mutex
  @return whether the shared mutex was acquired */
  bool shared_lock_inner() noexcept
  {
    type lk = 0;
    while (!inner.compare_exchange_weak(lk, lk + WAITER,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed))
      if (lk & X)
        return false;
    return true;
  }
  /** Release a shared mutex
  @return whether an exclusive mutex is being waited for */
  bool shared_unlock_inner() noexcept
  {
    type lk = inner.fetch_sub(WAITER, std::memory_order_release);
    assert(~X & lk);
    return lk == X + WAITER;
  }

  /** For atomic_shared_mutex::lock()
  @return lock word to be passed to lock_inner_wait()
  @retval 0 if the exclusive lock was granted */
  type lock_inner() noexcept
  {
#if defined __i386__||defined __x86_64__||defined _M_IX86||defined _M_IX64
    /* On IA-32 and AMD64, this type of fetch_or() can only be implemented
    as a loop around LOCK CMPXCHG. In this particular case, toggling the
    most significant bit using fetch_add() is equivalent, and is
    translated into a simple LOCK XADD. */
    return inner.fetch_add(X, std::memory_order_acquire);
#endif
    return inner.fetch_or(X, std::memory_order_acquire);
  }

  /** Wait for an exclusive lock to be granted (any S locks to be released)
  @param lk  recent number of conflicting S lock holders */
  void lock_inner_wait(type lk) noexcept;

  /** Release an exclusive lock of an atomic_shared_mutex */
  void unlock_inner() noexcept
  {
    assert(this->is_locked());
    inner.store(0, std::memory_order_release);
  }

#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
  void shared_unlock_inner_notify() noexcept;
#else
  /** Notify waiters after shared_unlock_inner() returned true */
  void shared_unlock_inner_notify() noexcept { inner.notify_one(); }
#endif

  /** For atomic_shared_mutex::update_lock() */
  void update_lock_inner() noexcept
  {
    assert(outer.is_locked());
#ifndef NDEBUG
    type lk =
#endif
      inner.fetch_add(WAITER, std::memory_order_acquire);
    assert(lk < X - WAITER);
  }
  /** For atomic_shared_mutex::update_lock_upgrade() */
  type update_lock_upgrade_inner() noexcept
  {
    assert(outer.is_locked());
    return inner.fetch_add(X - WAITER, std::memory_order_acquire) - WAITER;
  }
  /** For atomic_shared_mutex::update_lock_downgrade() */
  void update_lock_downgrade_inner() noexcept
  {
    assert(outer.is_locked());
    assert(this->is_locked());
    inner.store(WAITER, std::memory_order_release);
  }
  /** For atomic_shared_mutex::unlock_update() */
  void update_unlock_inner() noexcept
  {
    assert(outer.is_locked());
#ifndef NDEBUG
    type lk =
#endif
      inner.fetch_sub(WAITER, std::memory_order_release);
    assert(lk);
    assert(lk < X);
  }
};

/** Slim Shared/Update/Exclusive lock without recursion (re-entrancy).

At most one thread may hold an exclusive lock, such that no other threads
may hold any locks at the same time.

At most one thread may hold an update lock at a time.

As long as no thread is holding an exclusive lock, any number of
threads may hold a shared lock.
If a thread is waiting for an exclusive lock(), further concurrent
lock_shared() requests will be blocked until the exclusive lock has
been granted and released in unlock().

This is based on the ssux_lock in MariaDB Server 10.6.

Inspiration for using a composition of a mutex and lock word was provided by
http://locklessinc.com/articles/sleeping_rwlocks/
(which discusses several alternatives for implementing rw-locks).

The naming intentionally resembles std::shared_mutex.
The counterpart of get_storage() is std::shared_mutex::native_handle().

Unlike std::shared_mutex, we support lock_update() that is like
lock(), but allows concurrent locks_shared().
We also define the operations try_lock_update(), unlock_update().
For conversions between update locks and exclusive locks, we define
update_lock_upgrade(), lock_update_downgrade().

For efficiency, we rely on two wait queues that are provided by the
runtime system or the operating system kernel: the one in the mutex for
exclusive locking, and another for waking up an exclusive lock waiter
that is already holding the mutex, once the last shared lock is released.
We count shared locks to have necessary and sufficient notify_one() calls. */
template<typename Storage = shared_mutex_storage<>>
class atomic_shared_mutex
{
  Storage storage;

  /** Wait for a shared lock to be granted (any X lock to be released) */
  void shared_lock_wait() noexcept
  {
    bool acquired;
    do {
      storage.lock_outer();
      acquired = storage.shared_lock_inner();
      storage.unlock_outer();
    } while (!acquired);
  }

  /** Increment the shared lock count while holding the mutex */
  void shared_acquire() noexcept
  {
    storage.update_lock_inner();
  }

  /** Acquire an exclusive lock while holding lock_outer() */
  void lock_inner() noexcept
  {
    if (auto lk = storage.lock_inner())
      storage.lock_inner_wait(lk);
  }

public:
  /** Default constructor */
  constexpr atomic_shared_mutex() = default;
  /** No copy constructor */
  atomic_shared_mutex(const atomic_shared_mutex&) = delete;
  /** No assignment operator */
  atomic_shared_mutex& operator=(const atomic_shared_mutex&) = delete;

  bool is_locked_or_waiting() const { return storage.is_locked_or_waiting(); }

  constexpr const Storage& get_storage() const { return storage; }

  /** Try to acquire a shared lock.
  @return whether the S lock was acquired */
  bool try_lock_shared() noexcept
  {
    return storage.shared_lock_inner();
  }

  /** Try to acquire an Update lock (which conflicts with other U or X lock).
  @return whether the U lock was acquired */
  bool try_lock_update() noexcept
  {
    if (!storage.outer.try_lock())
      return false;
    storage.update_lock_inner();
    return true;
  }

  /** Try to acquire an exclusive lock.
  @return whether the X lock was acquired */
  bool try_lock() noexcept
  {
    if (!storage.outer.try_lock())
      return false;
    lock_inner();
    return true;
  }

  /** Acquire a shared lock (which can coexist with S or U locks). */
  void lock_shared() noexcept
  {
    if (!storage.shared_lock_inner())
      shared_lock_wait();
  }

  /** Acquire an update lock (which can coexist with S locks). */
  void lock_update() noexcept { storage.lock_outer(); shared_acquire(); }

  /** Acquire an exclusive lock. */
  void lock() noexcept { storage.lock_outer(); lock_inner(); }

  /** Upgrade an update lock to exclusive. */
  void update_lock_upgrade() noexcept
  {
    if (auto lk = storage.update_lock_upgrade_inner())
      storage.lock_inner_wait(lk);
  }
  /** Downgrade an exclusive lock to update. */
  void update_lock_downgrade() noexcept
  {
    storage.update_lock_downgrade_inner();
    /* Note: Any pending lock_shared() will not be woken up until
       unlock_update() */
  }

  /** Release a shared lock. */
  void unlock_shared() noexcept
  {
    if (storage.shared_unlock_inner())
      storage.shared_unlock_inner_notify();
  }
  /** Release an update lock. */
  void unlock_update() noexcept
  {
    storage.update_unlock_inner();
    storage.unlock_outer();
  }
  /** Release an exclusive lock. */
  void unlock() noexcept
  {
    storage.unlock_inner();
    storage.unlock_outer();
  }
};
