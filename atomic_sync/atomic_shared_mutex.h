#pragma once
#include "atomic_mutex.h"

template<typename T = uint32_t>
struct shared_mutex_storage : mutex_storage<T>
{
  atomic_mutex<mutex_storage<T>> ex;

  constexpr bool is_locked() const noexcept
  { return this->load(std::memory_order_acquire) == this->HOLDER; }
  constexpr bool is_locked_or_waiting() const noexcept
  { return ex.load(std::memory_order_acquire) || this->is_locked(); }
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
template<typename storage = shared_mutex_storage<>>
class atomic_shared_mutex : public storage
{
  using type = typename storage::type;
  /** flag to indicate an exclusive request; X lock is held when load()==X */
  static constexpr type X = storage::HOLDER;
  static constexpr type WAITER = storage::WAITER;

#ifndef NDEBUG
  bool is_ex_locked() const noexcept
  { return this->ex.load(std::memory_order_acquire) != 0; }
  bool is_exclusively_locked() const noexcept
  { return this->load(std::memory_order_acquire) == this->HOLDER; }
#endif

  /** Wait for an exclusive lock to be granted (any S locks to be released)
  @param lk  recent number of conflicting S lock holders */
  void exclusive_lock_wait(type lk) noexcept
  {
    assert(is_ex_locked());
    assert(lk);
    assert(lk < X);
    this->lock_wait(lk | X);
  }

  /** Wait for a shared lock to be granted (any X lock to be released) */
  void shared_lock_wait() noexcept
  {
    bool acquired;
    do {
      this->ex.lock();
      acquired = try_lock_shared();
      this->ex.unlock();
    } while (!acquired);
  }
  /** Wait for a shared lock to be granted (any X lock to be released),
  with initial spinloop. */
  void spin_shared_lock_wait() noexcept
  {
    this->ex.spin_lock();
    bool acquired = try_lock_shared();
    this->ex.unlock();
    if (!acquired)
      shared_lock_wait();
  }

  /** Increment the shared lock count while holding the mutex */
  void shared_acquire() noexcept
  {
    __tsan_mutex_pre_lock(this, __tsan_mutex_read_lock);
#ifndef NDEBUG
    type lk =
#endif
      this->fetch_add(WAITER, std::memory_order_acquire);
    __tsan_mutex_post_lock(this, __tsan_mutex_read_lock, 0);
    assert(lk < X - WAITER);
  }

  /** Acquire an exclusive lock while holding the mutex */
  void exclusive_acquire() noexcept
  {
    type lk;
    __tsan_mutex_pre_lock(this, __tsan_mutex_try_lock);
#if defined __i386__||defined __x86_64__||defined _M_IX86||defined _M_IX64
    /* On IA-32 and AMD64, this type of fetch_or() can only be implemented
    as a loop around LOCK CMPXCHG. In this particular case, toggling the
    most significant bit using fetch_add() is equivalent, and is
    translated into a simple LOCK XADD. */
    if (X == type(~(type(~type(0) >> 1))))
      lk = this->fetch_add(X, std::memory_order_acquire);
    else
#endif
      lk = this->fetch_or(X, std::memory_order_acquire);
    if (lk) {
      __tsan_mutex_post_lock(this, __tsan_mutex_try_lock_failed, 0);
      __tsan_mutex_pre_lock(this, 0);
      exclusive_lock_wait(lk);
      __tsan_mutex_post_lock(this, 0, 0);
    } else
      __tsan_mutex_post_lock(this, __tsan_mutex_try_lock, 0);
  }

public:
#ifdef __SANITIZE_THREAD__
  atomic_shared_mutex()
  { __tsan_mutex_create(this, __tsan_mutex_linker_init); }
  ~atomic_shared_mutex()
  { __tsan_mutex_destroy(this, __tsan_mutex_linker_init); }
#else
  /** Default constructor */
  constexpr atomic_shared_mutex() = default;
#endif
  /** No copy constructor */
  atomic_shared_mutex(const atomic_shared_mutex&) = delete;
  /** No assignment operator */
  atomic_shared_mutex& operator=(const atomic_shared_mutex&) = delete;

  /** Try to acquire a shared lock.
  @return whether the S lock was acquired */
  bool try_lock_shared() noexcept
  {
    type lk = 0;
    __tsan_mutex_pre_lock(this, __tsan_mutex_try_read_lock);
    while (!this->compare_exchange_weak(lk, lk + WAITER,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed))
      if (lk & X) {
        __tsan_mutex_post_lock(this, __tsan_mutex_try_read_lock_failed, 0);
        return false;
      }
    __tsan_mutex_post_lock(this, __tsan_mutex_try_read_lock, 0);
    return true;
  }

  /** Try to acquire an Update lock (which conflicts with other U or X lock).
  @return whether the U lock was acquired */
  bool try_lock_update() noexcept
  {
    if (!this->ex.try_lock())
      return false;
    __tsan_mutex_pre_lock(this, __tsan_mutex_read_lock);
#ifndef NDEBUG
    type lk =
#endif
    this->fetch_add(WAITER, std::memory_order_acquire);
    __tsan_mutex_post_lock(this, __tsan_mutex_read_lock, 0);
    assert(lk < X - WAITER);
    return true;
  }

  /** Try to acquire an exclusive lock.
  @return whether the X lock was acquired */
  bool try_lock() noexcept
  {
    if (!this->ex.try_lock())
      return false;

    __tsan_mutex_pre_lock(this, __tsan_mutex_try_lock);
    type lk = 0;
    if (compare_exchange_strong(lk, X, std::memory_order_acquire,
                                std::memory_order_relaxed)) {
      __tsan_mutex_post_lock(this, __tsan_mutex_try_lock, 0);
      return true;
    }
    __tsan_mutex_post_lock(this, __tsan_mutex_try_lock_failed, 0);
    this->ex.unlock();
    return false;
  }

  /** Acquire a shared lock (which can coexist with S or U locks). */
  void lock_shared() noexcept { if (!try_lock_shared()) shared_lock_wait(); }
  void spin_lock_shared() noexcept
  { if (!try_lock_shared()) spin_shared_lock_wait(); }

  /** Acquire an update lock (which can coexist with S locks). */
  void lock_update() noexcept { this->ex.lock(); shared_acquire(); }
  void spin_lock_update() noexcept { this->ex.spin_lock(); shared_acquire(); }

  /** Acquire an exclusive lock. */
  void lock() noexcept { this->ex.lock(); exclusive_acquire(); }
  void spin_lock() noexcept { storage::ex.spin_lock(); exclusive_acquire(); }

  /** Upgrade an update lock to exclusive. */
  void update_lock_upgrade() noexcept
  {
    assert(is_ex_locked());
    __tsan_mutex_pre_unlock(this, __tsan_mutex_read_lock);
    __tsan_mutex_pre_lock(this, 0);
    type lk = this->fetch_add(X - WAITER, std::memory_order_acquire);
    __tsan_mutex_post_unlock(this, __tsan_mutex_read_lock);
    if (lk != WAITER)
      exclusive_lock_wait(lk - WAITER);
    __tsan_mutex_post_lock(this, 0, 0);
  }
  /** Downgrade an exclusive lock to update. */
  void lock_update_downgrade() noexcept
  {
    assert(is_ex_locked());
    assert(is_exclusively_locked());
    __tsan_mutex_pre_unlock(this, 0);
    __tsan_mutex_pre_lock(this, __tsan_mutex_read_lock);
    this->store(WAITER, std::memory_order_release);
    __tsan_mutex_post_unlock(this, 0);
    __tsan_mutex_post_lock(this, __tsan_mutex_read_lock, 0);
    /* Note: Any pending lock_shared() will not be woken up until
       unlock_update() */
  }

  /** Release a shared lock. */
  void unlock_shared() noexcept
  {
    __tsan_mutex_pre_unlock(this, __tsan_mutex_read_lock);
    type lk = this->fetch_sub(WAITER, std::memory_order_release);
    __tsan_mutex_post_unlock(this, __tsan_mutex_read_lock);
    assert(~X & lk);
    if (lk == X + WAITER)
    {
      __tsan_mutex_pre_signal(this, 0);
      this->notify_one();
      __tsan_mutex_post_signal(this, 0);
    }
  }
  /** Release an update lock. */
  void unlock_update() noexcept
  {
    __tsan_mutex_pre_unlock(this, __tsan_mutex_read_lock);
#ifndef NDEBUG
    type lk =
#endif
      this->fetch_sub(WAITER, std::memory_order_release);
    __tsan_mutex_post_unlock(this, __tsan_mutex_read_lock);
    assert(lk);
    assert(lk < X);
    this->ex.unlock();
  }
  /** Release an exclusive lock. */
  void unlock() noexcept
  {
    assert(is_exclusively_locked());
    __tsan_mutex_pre_unlock(this, 0);
    this->store(0, std::memory_order_release);
    __tsan_mutex_post_unlock(this, 0);
    this->ex.unlock();
  }
};

/** Like atomic_shared_mutex, but with spinloops */
template<typename storage = shared_mutex_storage<>>
class atomic_spin_shared_mutex : public atomic_shared_mutex<storage>
{
public:
  void lock() noexcept { this->spin_lock(); }
  void shared_lock() noexcept { this->spin_lock_shared(); }
  void update_lock() noexcept { this->spin_lock_update(); }
};
