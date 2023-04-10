#pragma once
#include "atomic_mutex.h"

template<typename Storage> class atomic_shared_mutex;

template<typename T = uint32_t>
class shared_mutex_storage
{
  // exposition only
  std::atomic<T> inner;
  atomic_mutex<mutex_storage<T>> outer;
  using type = T;
  static constexpr type X = type(~(type(~type(0)) >> 1));
  static constexpr type WAITER = 1;

public:
  constexpr bool is_locked() const noexcept
  { return inner.load(std::memory_order_acquire) == X; }
  constexpr bool is_locked_or_waiting() const noexcept
  { return outer.get_storage().is_locked_or_waiting() || is_locked(); }
private:
  friend class atomic_shared_mutex<shared_mutex_storage>;
  /** @return default argument for spin_lock_outer() */
  static unsigned default_spin_rounds();

  void lock_outer() noexcept { outer.lock(); }
  void spin_lock_outer(unsigned spin_rounds) noexcept
  { outer.spin_lock(spin_rounds); }
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
    assert(outer.get_storage().is_locked());
#ifndef NDEBUG
    type lk =
#endif
      inner.fetch_add(WAITER, std::memory_order_acquire);
    assert(lk < X - WAITER);
  }
  /** For atomic_shared_mutex::update_lock_upgrade() */
  type update_lock_upgrade_inner() noexcept
  {
    assert(outer.get_storage().is_locked());
    return inner.fetch_add(X - WAITER, std::memory_order_acquire) - WAITER;
  }
  /** For atomic_shared_mutex::update_lock_downgrade() */
  void update_lock_downgrade_inner() noexcept
  {
    assert(outer.get_storage().is_locked());
    assert(this->is_locked());
    inner.store(WAITER, std::memory_order_release);
  }
  /** For atomic_shared_mutex::unlock_update() */
  void update_unlock_inner() noexcept
  {
    assert(outer.get_storage().is_locked());
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

We define spin_lock(), spin_lock_shared(), and spin_lock_update(),
which are like lock(), lock_shared(), lock_update(), but with an
initial spinloop.

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
  /** Wait for a shared lock to be granted (any X lock to be released),
  with initial spinloop. */
  void spin_shared_lock_wait(unsigned spin_rounds) noexcept
  {
    storage.spin_lock_outer(spin_rounds);
    bool acquired = storage.shared_lock_inner();
    storage.unlock_outer();
    if (!acquired)
      shared_lock_wait();
  }

  /** Increment the shared lock count while holding the mutex */
  void shared_acquire() noexcept
  {
    __tsan_mutex_pre_lock(&storage, __tsan_mutex_read_lock);
    storage.update_lock_inner();
    __tsan_mutex_post_lock(&storage, __tsan_mutex_read_lock, 0);
  }

  /** Acquire an exclusive lock while holding lock_outer() */
  void lock_inner() noexcept
  {
    __tsan_mutex_pre_lock(&storage, __tsan_mutex_try_lock);
    if (auto lk = storage.lock_inner()) {
      __tsan_mutex_post_lock(&storage, __tsan_mutex_try_lock_failed, 0);
      __tsan_mutex_pre_lock(&storage, 0);
      storage.lock_inner_wait(lk);
      __tsan_mutex_post_lock(&storage, 0, 0);
    } else
      __tsan_mutex_post_lock(&storage, __tsan_mutex_try_lock, 0);
  }

public:
#ifdef __SANITIZE_THREAD__
  atomic_shared_mutex()
  { __tsan_mutex_create(&storage, __tsan_mutex_linker_init); }
  ~atomic_shared_mutex()
  { __tsan_mutex_destroy(&storage, __tsan_mutex_linker_init); }
#else
  /** Default constructor */
  constexpr atomic_shared_mutex() = default;
#endif
  /** No copy constructor */
  atomic_shared_mutex(const atomic_shared_mutex&) = delete;
  /** No assignment operator */
  atomic_shared_mutex& operator=(const atomic_shared_mutex&) = delete;

  constexpr const Storage& get_storage() const { return storage; }

  /** Try to acquire a shared lock.
  @return whether the S lock was acquired */
  bool try_lock_shared() noexcept
  {
    __tsan_mutex_pre_lock(&storage, __tsan_mutex_try_read_lock);
    bool acquired = storage.shared_lock_inner();
    __tsan_mutex_post_lock(&storage, acquired
                           ? __tsan_mutex_try_read_lock_failed
                           : __tsan_mutex_try_read_lock, 0);
    return acquired;
  }

  /** Try to acquire an Update lock (which conflicts with other U or X lock).
  @return whether the U lock was acquired */
  bool try_lock_update() noexcept
  {
    if (!storage.outer.try_lock())
      return false;
    __tsan_mutex_pre_lock(&storage, __tsan_mutex_read_lock);
    storage.update_lock_inner();
    __tsan_mutex_post_lock(&storage, __tsan_mutex_read_lock, 0);
    return true;
  }

  /** Try to acquire an exclusive lock.
  @return whether the X lock was acquired */
  bool try_lock() noexcept
  {
    if (!storage.outer.try_lock())
      return false;
    __tsan_mutex_pre_lock(&storage, 0);
    lock_inner();
    __tsan_mutex_post_lock(&storage, 0, 0);
    return true;
  }

  /** Acquire a shared lock (which can coexist with S or U locks). */
  void lock_shared() noexcept
  {
    __tsan_mutex_pre_lock(&storage, __tsan_mutex_read_lock);
    if (!storage.shared_lock_inner())
      shared_lock_wait();
    __tsan_mutex_post_lock(&storage, __tsan_mutex_read_lock, 0);
  }
  void spin_lock_shared(unsigned spin_rounds) noexcept
  {
    __tsan_mutex_pre_lock(&storage, __tsan_mutex_read_lock);
    if (!storage.shared_lock_inner())
      spin_shared_lock_wait(spin_rounds);
    __tsan_mutex_post_lock(&storage, __tsan_mutex_read_lock, 0);
  }
  void spin_lock_shared() noexcept
  { return spin_lock_shared(storage.default_spin_rounds()); }

  /** Acquire an update lock (which can coexist with S locks). */
  void lock_update() noexcept { storage.lock_outer(); shared_acquire(); }
  void spin_lock_update(unsigned spin_rounds) noexcept
  { storage.spin_lock_outer(spin_rounds); shared_acquire(); }
  void spin_lock_update() noexcept
  { return spin_lock_update(storage.default_spin_rounds()); }

  /** Acquire an exclusive lock. */
  void lock() noexcept { storage.lock_outer(); lock_inner(); }
  void spin_lock(unsigned spin_rounds) noexcept
  { storage.spin_lock_outer(spin_rounds); lock_inner(); }
  void spin_lock() noexcept
  { return spin_lock(storage.default_spin_rounds()); }

  /** Upgrade an update lock to exclusive. */
  void update_lock_upgrade() noexcept
  {
    __tsan_mutex_pre_unlock(&storage, __tsan_mutex_read_lock);
    __tsan_mutex_pre_lock(&storage, 0);
    auto lk = storage.update_lock_upgrade_inner();
    __tsan_mutex_post_unlock(&storage, __tsan_mutex_read_lock);
    if (lk)
      storage.lock_inner_wait(lk);
    __tsan_mutex_post_lock(&storage, 0, 0);
  }
  /** Downgrade an exclusive lock to update. */
  void update_lock_downgrade() noexcept
  {
    __tsan_mutex_pre_unlock(&storage, 0);
    __tsan_mutex_pre_lock(&storage, __tsan_mutex_read_lock);
    storage.update_lock_downgrade_inner();
    __tsan_mutex_post_unlock(&storage, 0);
    __tsan_mutex_post_lock(&storage, __tsan_mutex_read_lock, 0);
    /* Note: Any pending lock_shared() will not be woken up until
       unlock_update() */
  }

  /** Release a shared lock. */
  void unlock_shared() noexcept
  {
    __tsan_mutex_pre_unlock(&storage, __tsan_mutex_read_lock);
    bool notify = storage.shared_unlock_inner();
    __tsan_mutex_post_unlock(&storage, __tsan_mutex_read_lock);
    if (notify)
    {
      __tsan_mutex_pre_signal(&storage, 0);
      storage.shared_unlock_inner_notify();
      __tsan_mutex_post_signal(&storage, 0);
    }
  }
  /** Release an update lock. */
  void unlock_update() noexcept
  {
    __tsan_mutex_pre_unlock(&storage, __tsan_mutex_read_lock);
    storage.update_unlock_inner();
    __tsan_mutex_post_unlock(&storage, __tsan_mutex_read_lock);
    storage.unlock_outer();
  }
  /** Release an exclusive lock. */
  void unlock() noexcept
  {
    __tsan_mutex_pre_unlock(&storage, 0);
    storage.unlock_inner();
    __tsan_mutex_post_unlock(&storage, 0);
    storage.unlock_outer();
  }
};
