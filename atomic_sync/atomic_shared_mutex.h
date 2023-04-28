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

Unlike std::shared_mutex, we support lock_update() that is like
lock(), but allows concurrent lock_shared().
We also define the operations try_lock_update(), unlock_update().
For conversions between update locks and exclusive locks, we define
update_lock_upgrade(), lock_update_downgrade().

For efficiency, we rely on two wait queues that are provided by the
runtime system or the operating system kernel: the one in the mutex for
exclusive locking, and another for waking up an exclusive lock waiter
that is already holding the mutex, once the last shared lock is released.
We count shared locks to have necessary and sufficient notify_one() calls. */
class atomic_shared_mutex
{
  // exposition only
  using type = unsigned;
  atomic_unsigned_lock_free inner;
  atomic_mutex outer;
  static constexpr type X = type(~(type(~type(0)) >> 1));

  /** Wait for lock() to be granted (any lock_shared() to be released)
  @param lk  recent number of pending unlock_shared() calls */
  void lock_inner_wait(type lk) noexcept;

  /** Acquire an exclusive lock while holding outer lock */
  void lock_inner() noexcept
  {
    if (type lk = inner.fetch_or(X))
      lock_inner_wait(lk);
  }

public:
  bool is_locked() const noexcept { return inner == X; }
  bool is_locked_or_waiting() const noexcept
  { return is_locked() || outer.is_locked_or_waiting(); }

  bool try_lock() noexcept
  {
    if (!outer.try_lock())
      return false;
    lock_inner();
    return true;
  }
  void lock() noexcept { outer.lock(); lock_inner(); }
  void unlock() noexcept { inner = 0; outer.unlock();  }

  bool try_lock_shared() noexcept
  {
    type lk = 0;
    using o = std::memory_order;
    while (!inner.compare_exchange_weak(lk, lk + 1, o::acquire, o::relaxed))
      if (lk & X)
        return false;
    return true;
  }
  void lock_shared() noexcept
  {
    if (!try_lock_shared()) {
      bool acquired;
      do {
        outer.lock();
        acquired = try_lock_shared();
        outer.unlock();
      } while (!acquired);
    }
  }

  void unlock_shared() noexcept { if (--inner == X) inner.notify_one(); }

  bool try_lock_update() noexcept
  {
    if (!outer.try_lock())
      return false;
    inner++;
    return true;
  }
  void lock_update() noexcept { outer.lock(); inner++; }
  void unlock_update() noexcept { inner--; outer.unlock(); }

  /** Downgrade lock() to lock_update() */
  void update_lock_downgrade() noexcept { inner = 1; }
  /** Upgrade lock_update() to lock() */
  void update_lock_upgrade() noexcept
  {
    if (type lk = inner.fetch_add(X - 1) - 1)
      lock_inner_wait(lk);
  }
};
