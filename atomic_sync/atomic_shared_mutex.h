#pragma once
#include "atomic_mutex.h"

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
We do not define native_handle().

Unlike std::shared_mutex, we support lock_update() that is like
lock(), but allows concurrent locks_shared().
We also define the operations try_lock_update(), unlock_update().
For conversions between update locks and exclusive locks, we define
update_lock_upgrade(), lock_update_downgrade().

We define spin_lock(), spin_lock_shared(), and spin_lock_update(),
which are like lock(), lock_shared(), lock_update(), but with an
initial spinloop.

For efficiency, we rely on two wait queues that are provided by the
runtime system or the operating system kernel: the one in the mutex for
exclusive locking, and another for waking up an exclusive lock waiter
that is already holding the mutex, once the last shared lock is released.
We count shared locks to have necessary and sufficient notify_one() calls. */
template<typename storage = mutex_storage<>>
class atomic_shared_mutex : public storage
{
  /** mutex for synchronization; continuously held by U or X lock holder */
  atomic_mutex<storage> ex;

  using type = typename storage::type;
  /** flag to indicate an exclusive request; X lock is held when load()==X */
  static constexpr type X = storage::HOLDER;
  static constexpr type WAITER = storage::WAITER;

  /** Wait for an exclusive lock to be granted (any S locks to be released)
  @param lk  recent number of conflicting S lock holders */
  void lock_wait(type lk) noexcept
  {
    assert(ex.is_locked());
    assert(lk);
    assert(lk < X);
    storage::lock_wait(lk | X);
  }

  /** Wait for a shared lock to be granted (any X lock to be released) */
  void shared_lock_wait() noexcept
  {
    bool acquired;
    do {
      ex.lock();
      acquired = try_lock_shared();
      ex.unlock();
    } while (!acquired);
  }
  /** Wait for a shared lock to be granted (any X lock to be released),
  with initial spinloop. */
  void spin_shared_lock_wait() noexcept
  {
    ex.spin_lock();
    bool acquired;
    acquired = try_lock_shared();
    ex.unlock();
    if (!acquired)
      shared_lock_wait();
  }

  /** Increment the shared lock count while holding the mutex */
  void shared_acquire() noexcept
  {
#ifndef NDEBUG
    type lk =
#endif
      storage::fetch_add(WAITER, std::memory_order_acquire);
    assert(lk < X - WAITER);
  }

  /** Acquire an exclusive lock while holding the mutex */
  void exclusive_acquire() noexcept
  {
    type lk;
#if defined __i386__||defined __x86_64__||defined _M_IX86||defined _M_IX64
    /* On IA-32 and AMD64, this type of fetch_or() can only be implemented
    as a loop around LOCK CMPXCHG. In this particular case, toggling the
    most significant bit using fetch_add() is equivalent, and is
    translated into a simple LOCK XADD. */
    if (X == type(~(type(~type(0) >> 1))))
      lk = storage::fetch_add(X, std::memory_order_acquire);
    else
#endif
      lk = storage::fetch_or(X, std::memory_order_acquire);
    if (lk)
      lock_wait(lk);
  }

public:
  /** Default constructor */
  constexpr atomic_shared_mutex() : storage(), ex() {}
  /** No copy constructor */
  atomic_shared_mutex(const atomic_shared_mutex&) = delete;
  /** No assignment operator */
  atomic_shared_mutex& operator=(const atomic_shared_mutex&) = delete;

#if defined WITH_ELISION || !defined NDEBUG
  /* FIXME: how to define this in mutex_storage? */
  bool is_locked_or_waiting() const noexcept
  { return storage::is_locked() || ex.is_locked_or_waiting(); }
#endif

  /** Try to acquire a shared lock.
  @return whether the S lock was acquired */
  bool try_lock_shared() noexcept
  {
    type lk = 0;
    while (!storage::compare_exchange_weak(lk, lk + WAITER,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed))
      if (lk & X)
        return false;
    return true;
  }

  /** Try to acquire an Update lock (which conflicts with other U or X lock).
  @return whether the U lock was acquired */
  bool try_lock_update() noexcept
  {
    if (!ex.try_lock())
      return false;
#ifndef NDEBUG
    type lk =
#endif
    storage::fetch_add(WAITER, std::memory_order_acquire);
    assert(lk < X - WAITER);
    return true;
  }

  /** Try to acquire an exclusive lock.
  @return whether the X lock was acquired */
  bool try_lock() noexcept
  {
    if (!ex.try_lock())
      return false;
    type lk = 0;
    if (compare_exchange_strong(lk, X, std::memory_order_acquire,
                                std::memory_order_relaxed))
      return true;
    ex.unlock();
    return false;
  }

  /** Acquire a shared lock (which can coexist with S or U locks). */
  void lock_shared() noexcept { if (!try_lock_shared()) shared_lock_wait(); }
  void spin_lock_shared() noexcept
  { if (!try_lock_shared()) spin_shared_lock_wait(); }

  /** Acquire an update lock (which can coexist with S locks). */
  void lock_update() noexcept { ex.lock(); shared_acquire(); }
  void spin_lock_update() noexcept { ex.spin_lock(); shared_acquire(); }

  /** Acquire an exclusive lock. */
  void lock() noexcept { ex.lock(); exclusive_acquire(); }
  void spin_lock() noexcept { ex.spin_lock(); exclusive_acquire(); }

  /** Upgrade an update lock to exclusive. */
  void update_lock_upgrade() noexcept
  {
    assert(ex.is_locked());
    type lk = storage::fetch_add(X - WAITER, std::memory_order_acquire);
    if (lk != WAITER)
      lock_wait(lk - WAITER);
  }
  /** Downgrade an exclusive lock to update. */
  void lock_update_downgrade() noexcept
  {
    assert(ex.is_locked());
    assert(storage::is_shared_locked());
    storage::store(WAITER, std::memory_order_release);
    /* Note: Any pending s_lock() will not be woken up until u_unlock() */
  }

  /** Release a shared lock. */
  void unlock_shared() noexcept
  {
    type lk = storage::fetch_sub(WAITER, std::memory_order_release);
    assert(~X & lk);
    if (lk == X + WAITER)
      storage::notify_one();
  }
  /** Release an update lock. */
  void unlock_update() noexcept
  {
#ifndef NDEBUG
    type lk =
#endif
      storage::fetch_sub(WAITER, std::memory_order_release);
    assert(lk);
    assert(lk < X);
    ex.unlock();
  }
  /** Release an exclusive lock. */
  void unlock() noexcept
  {
    assert(storage::is_shared_locked());
    storage::store(0, std::memory_order_release);
    ex.unlock();
  }
};

/** Like atomic_shared_mutex, but with spinloops */
template<typename storage = mutex_storage<>>
class atomic_spin_shared_mutex : public atomic_shared_mutex<storage>
{
public:
  void lock() noexcept
  { atomic_shared_mutex<storage>::spin_lock(); }
  void shared_lock() noexcept
  { atomic_shared_mutex<storage>::spin_lock_shared(); }
  void update_lock() noexcept
  { atomic_shared_mutex<storage>::spin_lock_update(); }
};
