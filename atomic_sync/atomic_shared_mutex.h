#pragma once
#include "atomic_mutex.h"

template<typename T = uint32_t>
class shared_mutex_storage :
#if !defined _WIN32 && __cplusplus < 202002L
  mutex_storage<T> // emulates std::atomic::wait(), std::atomic::notify_one()
#else
  std::atomic<T>
#endif
{
  atomic_mutex<mutex_storage<T>> ex;
  using type = T;
  static constexpr type X = type(~(type(~type(0)) >> 1));
  static constexpr type WAITER = 1;

public:
  constexpr bool is_locked() const noexcept
  { return this->load(std::memory_order_acquire) == X; }
  constexpr bool is_locked_or_waiting() const noexcept
  { return ex.is_locked_or_waiting() || this->is_locked(); }
protected:
  void lock_outer() noexcept { ex.lock(); }
  void spin_lock_outer(unsigned spin_rounds) noexcept
  { ex.spin_lock(spin_rounds); }
  void unlock_outer() noexcept { ex.unlock(); }

  /** Try to acquire a shared mutex
  @return whether the shared mutex was acquired */
  bool shared_lock_inner() noexcept
  {
    type lk = 0;
    while (!this->compare_exchange_weak(lk, lk + WAITER,
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
    type lk = this->fetch_sub(WAITER, std::memory_order_release);
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
    return this->fetch_add(X, std::memory_order_acquire);
#endif
    return this->fetch_or(X, std::memory_order_acquire);
  }

  /** For atomic_shared_mutex::try_lock()
  @return whether the exclusive lock was acquired */
  bool try_lock_inner() noexcept
  {
    type lk = 0;
    return compare_exchange_strong(lk, X, std::memory_order_acquire,
                                   std::memory_order_relaxed);
  }

  /** Wait for an exclusive lock to be granted (any S locks to be released)
  @param lk  recent number of conflicting S lock holders */
  void lock_inner_wait(type lk) noexcept;

  /** Release an exclusive lock of an atomic_shared_mutex */
  void unlock_inner() noexcept
  {
    assert(this->is_locked());
    this->store(0, std::memory_order_release);
  }

  /** Notify waiters after shared_unlock_inner() returned true */
  void shared_unlock_inner_notify() noexcept { this->notify_one(); }

  /** For atomic_shared_mutex::update_lock() */
  void update_lock_inner() noexcept
  {
    assert(ex.is_locked());
#ifndef NDEBUG
    type lk =
#endif
      this->fetch_add(WAITER, std::memory_order_acquire);
    assert(lk < X - WAITER);
  }
  /** For atomic_shared_mutex::update_lock_upgrade() */
  type update_lock_upgrade_inner() noexcept
  {
    assert(ex.is_locked());
    return this->fetch_add(X - WAITER, std::memory_order_acquire) - WAITER;
  }
  /** For atomic_shared_mutex::update_lock_downgrade() */
  void update_lock_downgrade_inner() noexcept
  {
    assert(ex.is_locked());
    assert(this->is_locked());
    this->store(WAITER, std::memory_order_release);
  }
  /** For atomic_shared_mutex::unlock_update() */
  void update_unlock_inner() noexcept
  {
    assert(ex.is_locked());
#ifndef NDEBUG
    type lk =
#endif
      this->fetch_sub(WAITER, std::memory_order_release);
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
  /** Wait for a shared lock to be granted (any X lock to be released) */
  void shared_lock_wait() noexcept
  {
    bool acquired;
    do {
      this->lock_outer();
      acquired = this->shared_lock_inner();
      this->unlock_outer();
    } while (!acquired);
  }
  /** Wait for a shared lock to be granted (any X lock to be released),
  with initial spinloop. */
  void spin_shared_lock_wait(unsigned spin_rounds) noexcept
  {
    this->spin_lock_outer(spin_rounds);
    bool acquired = this->shared_lock_inner();
    this->unlock_outer();
    if (!acquired)
      shared_lock_wait();
  }

  /** Increment the shared lock count while holding the mutex */
  void shared_acquire() noexcept
  {
    __tsan_mutex_pre_lock(this, __tsan_mutex_read_lock);
    this->update_lock_inner();
    __tsan_mutex_post_lock(this, __tsan_mutex_read_lock, 0);
  }

  /** Acquire an exclusive lock while holding lock_outer() */
  void lock_inner() noexcept
  {
    __tsan_mutex_pre_lock(this, __tsan_mutex_try_lock);
    if (auto lk = storage::lock_inner()) {
      __tsan_mutex_post_lock(this, __tsan_mutex_try_lock_failed, 0);
      __tsan_mutex_pre_lock(this, 0);
      this->lock_inner_wait(lk);
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
    __tsan_mutex_pre_lock(this, __tsan_mutex_try_read_lock);
    bool acquired = this->shared_lock_inner();
    __tsan_mutex_post_lock(this, acquired
                           ? __tsan_mutex_try_read_lock_failed
                           : __tsan_mutex_try_read_lock, 0);
    return acquired;
  }

  /** Try to acquire an Update lock (which conflicts with other U or X lock).
  @return whether the U lock was acquired */
  bool try_lock_update() noexcept
  {
    if (!this->ex.try_lock())
      return false;
    __tsan_mutex_pre_lock(this, __tsan_mutex_read_lock);
    this->update_lock_inner();
    __tsan_mutex_post_lock(this, __tsan_mutex_read_lock, 0);
    return true;
  }

  /** Try to acquire an exclusive lock.
  @return whether the X lock was acquired */
  bool try_lock() noexcept
  {
    if (!this->ex.try_lock())
      return false;

    __tsan_mutex_pre_lock(this, __tsan_mutex_try_lock);
    if (this->exclusive_try_lock_inner()) {
      __tsan_mutex_post_lock(this, __tsan_mutex_try_lock, 0);
      return true;
    }
    __tsan_mutex_post_lock(this, __tsan_mutex_try_lock_failed, 0);
    this->unlock_outer();
    return false;
  }

  /** Acquire a shared lock (which can coexist with S or U locks). */
  void lock_shared() noexcept
  {
    __tsan_mutex_pre_lock(this, __tsan_mutex_read_lock);
    if (!this->shared_lock_inner())
      shared_lock_wait();
    __tsan_mutex_post_lock(this, __tsan_mutex_read_lock, 0);
  }
  void spin_lock_shared(unsigned spin_rounds) noexcept
  {
    __tsan_mutex_pre_lock(this, __tsan_mutex_read_lock);
    if (!this->shared_lock_inner())
      spin_shared_lock_wait(spin_rounds);
    __tsan_mutex_post_lock(this, __tsan_mutex_read_lock, 0);
  }

  /** Acquire an update lock (which can coexist with S locks). */
  void lock_update() noexcept { this->lock_outer(); shared_acquire(); }
  void spin_lock_update(unsigned spin_rounds) noexcept
  { this->spin_lock_outer(spin_rounds); shared_acquire(); }

  /** Acquire an exclusive lock. */
  void lock() noexcept { this->lock_outer(); this->lock_inner(); }
  void spin_lock(unsigned spin_rounds) noexcept
  { this->spin_lock_outer(spin_rounds); this->lock_inner(); }

  /** Upgrade an update lock to exclusive. */
  void update_lock_upgrade() noexcept
  {
    __tsan_mutex_pre_unlock(this, __tsan_mutex_read_lock);
    __tsan_mutex_pre_lock(this, 0);
    auto lk = this->update_lock_upgrade_inner();
    __tsan_mutex_post_unlock(this, __tsan_mutex_read_lock);
    if (lk)
      this->lock_inner_wait(lk);
    __tsan_mutex_post_lock(this, 0, 0);
  }
  /** Downgrade an exclusive lock to update. */
  void update_lock_downgrade() noexcept
  {
    __tsan_mutex_pre_unlock(this, 0);
    __tsan_mutex_pre_lock(this, __tsan_mutex_read_lock);
    this->update_lock_downgrade_inner();
    __tsan_mutex_post_unlock(this, 0);
    __tsan_mutex_post_lock(this, __tsan_mutex_read_lock, 0);
    /* Note: Any pending lock_shared() will not be woken up until
       unlock_update() */
  }

  /** Release a shared lock. */
  void unlock_shared() noexcept
  {
    __tsan_mutex_pre_unlock(this, __tsan_mutex_read_lock);
    bool notify = this->shared_unlock_inner();
    __tsan_mutex_post_unlock(this, __tsan_mutex_read_lock);
    if (notify)
    {
      __tsan_mutex_pre_signal(this, 0);
      this->shared_unlock_inner_notify();
      __tsan_mutex_post_signal(this, 0);
    }
  }
  /** Release an update lock. */
  void unlock_update() noexcept
  {
    __tsan_mutex_pre_unlock(this, __tsan_mutex_read_lock);
    this->update_unlock_inner();
    __tsan_mutex_post_unlock(this, __tsan_mutex_read_lock);
    this->unlock_outer();
  }
  /** Release an exclusive lock. */
  void unlock() noexcept
  {
    __tsan_mutex_pre_unlock(this, 0);
    this->unlock_inner();
    __tsan_mutex_post_unlock(this, 0);
    this->unlock_outer();
  }
};
