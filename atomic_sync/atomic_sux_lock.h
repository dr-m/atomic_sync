#pragma once
#include "atomic_mutex.h"

/** Slim Shared/Update/Exclusive lock without recursion (re-entrancy).

At most one thread may hold an X (exclusive) lock, and no other threads
may hold any locks at the same time.

At most one thread may hold a U (update) lock at a time.

Any number of threads may hold S (shared) locks, provided that no X lock
is granted. If a thread is waiting for an X lock, further S lock requests
will be blocked until the X lock has been granted and released.

This lock can be used for buffer pool blocks in a database.
We use the special std::thread::id{} value to denote that a granted
U or X lock is not currently owned by any thread. Such locks may be
released by another thread, for example, in a write completion callback.

This is based on the sux_lock in MariaDB Server 10.6.

Inspiration for using a composition of a mutex and lock word was provided by
http://locklessinc.com/articles/sleeping_rwlocks/
(which discusses several alternatives for implementing rw-locks).

For efficiency, we rely on two wait queues that are provided by the
runtime system or the operating system kernel: the one in the mutex for
exclusive locking, and another for waking up an exclusive lock waiter
that is already holding the mutex, once the last shared lock is released.
We count shared locks to have necessary and sufficient notify_one() calls. */
class atomic_sux_lock : std::atomic<uint32_t>
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
  void x_wait(uint32_t lk) noexcept;

  /** Wait for a shared lock to be granted (any X lock to be released) */
  void s_wait() noexcept;
public:
  void init() noexcept { assert(is_vacant()); ex.init(); }
  void destroy() noexcept { assert(is_vacant()); ex.destroy(); }
  /** @return whether an exclusive lock is being held or waited for */
  bool is_waiting() const noexcept
  { return (load(std::memory_order_relaxed) & X) != 0; }
  /** @return whether the exclusive lock is being held */
  bool is_x_locked() const noexcept
  { return load(std::memory_order_relaxed) == X; }

  /** @return whether the lock is being held or waited for */
  bool is_vacant() const noexcept
  { return !load(std::memory_order_relaxed) && !ex.is_locked_or_waiting(); }

  /** Try to acquire a shared lock.
  @return whether the S lock was acquired */
  bool s_trylock() noexcept
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
  bool u_trylock() noexcept
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
  bool x_trylock() noexcept
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
  void s_lock() noexcept { if (!s_trylock()) s_wait(); }
  /** Acquire an update lock (which can coexist with S locks). */
  void u_lock() noexcept
  {
    ex.lock();
#ifndef NDEBUG
    uint32_t lk =
#endif
    fetch_add(1, std::memory_order_acquire);
    assert(lk < X - 1);
  }
  /** Acquire an exclusive lock. */
  void x_lock() noexcept
  {
    ex.lock();
    if (uint32_t lk = fetch_or(X, std::memory_order_acquire))
      x_wait(lk);
  }

  /** Upgrade an update lock to exclusive. */
  void u_x_upgrade() noexcept
  {
    assert(ex.is_locked());
    uint32_t lk = fetch_add(X - 1, std::memory_order_acquire);
    if (lk != 1)
      x_wait(lk - 1);
  }
  /** Downgrade an exclusive lock to update. */
  void x_u_downgrade() noexcept
  {
    assert(ex.is_locked());
    assert(is_x_locked());
    store(1, std::memory_order_release);
    /* Note: Any pending s_lock() will not be woken up until u_unlock() */
  }

  /** Release a shared lock. */
  void s_unlock() noexcept
  {
    uint32_t lk = fetch_sub(1, std::memory_order_release);
    assert(~X & lk);
    if (lk == X + 1)
      notify_one();
  }
  /** Release an update lock. */
  void u_unlock() noexcept
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
  void x_unlock() noexcept
  {
    assert(is_x_locked());
    store(0, std::memory_order_release);
    ex.unlock();
  }
};
