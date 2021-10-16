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

We define the predicates is_locked_or_waiting(), is_locked(),
and is_waiting().

Unlike std::shared_mutex, we support lock_update() that is like
lock(), but allows concurrent locks_shared().
We also define the operations try_lock_update(), unlock_update().
For conversions between update locks and exclusive locks, we define
update_lock_upgrade(), lock_update_downgrade().

We define spin_lock(), spin_lock_shared(), and spin_lock_update(),
which are like lock(), lock_shared(), lock_update(), but with an
initial spinloop.

There is no explicit constructor or destructor.
The object is expected to be zero-initialized, so that
!is_locked_or_waiting() will hold.

For efficiency, we rely on two wait queues that are provided by the
runtime system or the operating system kernel: the one in the mutex for
exclusive locking, and another for waking up an exclusive lock waiter
that is already holding the mutex, once the last shared lock is released.
We count shared locks to have necessary and sufficient notify_one() calls. */
class atomic_shared_mutex : std::atomic<uint32_t>
{
  /** mutex for synchronization; continuously held by U or X lock holder */
  atomic_mutex ex;
  /** flag to indicate an exclusive request; X lock is held when load()==X */
  static constexpr uint32_t X = 1U << 31;

#ifdef _WIN32
#elif __cplusplus >= 202002L
#else /* Emulate the C++20 primitives */
  void notify_one() noexcept;
  inline void wait(uint32_t old) const noexcept;
#endif

  /** Wait for an exclusive lock to be granted (any S locks to be released)
  @param lk  recent number of conflicting S lock holders */
  void lock_wait(uint32_t lk) noexcept;

  /** Wait for a shared lock to be granted (any X lock to be released) */
  void shared_lock_wait() noexcept;
  /** Wait for a shared lock to be granted (any X lock to be released),
  with initial spinloop. */
  void spin_shared_lock_wait() noexcept;

  /** Increment the shared lock count while holding the mutex */
  void shared_acquire()
  {
#ifndef NDEBUG
    uint32_t lk =
#endif
    fetch_add(1, std::memory_order_acquire);
    assert(lk < X - 1);
  }

  /** Acquire an exclusive lock while holding the mutex */
  void exclusive_acquire()
  {
#if defined __i386__||defined __x86_64__||defined _M_IX86||defined _M_IX64
    /* On IA-32 and AMD64, this type of fetch_or() can only be implemented
    as a loop around LOCK CMPXCHG. In this particular case, toggling the
    most significant bit using fetch_add() is equivalent, and is
    translated into a simple LOCK XADD. */
    static_assert(X == 1U << 31, "compatibility");
    if (uint32_t lk = fetch_add(X, std::memory_order_acquire))
      lock_wait(lk);
#else
    if (uint32_t lk = fetch_or(X, std::memory_order_acquire))
      lock_wait(lk);
#endif
  }

public:
  /** @return whether an exclusive lock is being held or waited for */
  bool is_waiting() const noexcept
  { return (load(std::memory_order_acquire) & X) != 0; }
  /** @return whether the exclusive lock is being held */
  bool is_locked() const noexcept
  { return load(std::memory_order_acquire) == X; }

  /** @return whether the lock is being held or waited for */
  bool is_locked_or_waiting() const noexcept
  { return is_waiting() || ex.is_locked_or_waiting(); }

  /** Try to acquire a shared lock.
  @return whether the S lock was acquired */
  bool try_lock_shared() noexcept
  {
    uint32_t lk = 0;
    while (!compare_exchange_weak(lk, lk + 1,
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
    if (!ex.trylock())
      return false;
#ifndef NDEBUG
    uint32_t lk =
#endif
    fetch_add(1, std::memory_order_acquire);
    assert(lk < X - 1);
    return true;
  }

  /** Try to acquire an exclusive lock.
  @return whether the X lock was acquired */
  bool try_lock() noexcept
  {
    if (!ex.trylock())
      return false;
    uint32_t lk = 0;
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
    uint32_t lk = fetch_add(X - 1, std::memory_order_acquire);
    if (lk != 1)
      lock_wait(lk - 1);
  }
  /** Downgrade an exclusive lock to update. */
  void lock_update_downgrade() noexcept
  {
    assert(ex.is_locked());
    assert(is_locked());
    store(1, std::memory_order_release);
    /* Note: Any pending s_lock() will not be woken up until u_unlock() */
  }

  /** Release a shared lock. */
  void unlock_shared() noexcept
  {
    uint32_t lk = fetch_sub(1, std::memory_order_release);
    assert(~X & lk);
    if (lk == X + 1)
      notify_one();
  }
  /** Release an update lock. */
  void unlock_update() noexcept
  {
#ifndef NDEBUG
    uint32_t lk =
#endif
    fetch_sub(1, std::memory_order_release);
    assert(lk);
    assert(lk < X);
    ex.unlock();
  }
  /** Release an exclusive lock. */
  void unlock() noexcept
  {
    assert(is_locked());
    store(0, std::memory_order_release);
    ex.unlock();
  }
};

/** Like atomic_shared_mutex, but with spinloops */
class atomic_spin_shared_mutex : public atomic_shared_mutex
{
public:
  void lock() noexcept { spin_lock(); }
  void shared_lock() noexcept { spin_lock_shared(); }
  void update_lock() noexcept { spin_lock_update(); }
};
