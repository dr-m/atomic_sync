#pragma once
#include "atomic_sux_lock.h"
#include <thread>

/** Shared/Update/Exclusive lock with recursion (re-entrancy).

At most one thread may hold X (exclusive) locks, and no other threads
may hold any locks at the same time.

At most one thread may hold U (update) locks at a time.

Any number of threads may hold S (shared) locks, which are always
non-recursive (not reentrant), provided that no X lock is granted.
If a thread is waiting for an X lock, further S lock requests will
be blocked until the X lock has been granted and released.

This is based on the ssux_lock in MariaDB Server 10.6. */
class atomic_recursive_sux_lock : atomic_sux_lock
{
  /** Numbers of U and X locks. Protected by the atomic_sux_lock. */
  uint32_t recursive;
  /** The owner of the U or X lock; protected by lock */
  std::atomic<std::thread::id> writer{};

  /** The multiplier in recursive for X locks */
  static constexpr uint32_t RECURSIVE_X= 1U;
  /** The multiplier in recursive for U locks */
  static constexpr uint32_t RECURSIVE_U= 1U << 16;
  /** The maximum allowed level of recursion */
  static constexpr uint32_t RECURSIVE_MAX= RECURSIVE_U - 1;

public:
  void init() noexcept
  {
    atomic_sux_lock::init();
    assert(!recursive);
    assert(writer == std::thread::id{});
  }

  /** Free the rw-lock after create() */
  void destroy() noexcept { assert(!recursive); atomic_sux_lock::destroy(); }

#ifndef NDEBUG
  /** @return whether no recursive locks are being held */
  bool not_recursive() const noexcept
  {
    assert(recursive);
    return recursive == RECURSIVE_X || recursive == RECURSIVE_U;
  }
#endif

  /** Acquire a recursive lock.
      @tparam U true=update lock, false=exclusive lock */
  template<bool U> void writer_recurse() noexcept
  {
    assert(writer == std::this_thread::get_id());
#ifndef NDEBUG
    auto rec = (recursive / (U ? RECURSIVE_U : RECURSIVE_X)) & RECURSIVE_MAX;
#endif
    assert(U ? recursive : rec);
    assert(rec < RECURSIVE_MAX);
    recursive += U ? RECURSIVE_U : RECURSIVE_X;
  }

public:
  /** Transfer the ownership of a write lock to another thread
  @param id the new owner of the U or X lock */
  void set_writer(std::thread::id id) noexcept
  { writer.store(id, std::memory_order_relaxed); }

  /** Transfer the writer ownership to the current thread */
  void set_writer() noexcept { set_writer(std::this_thread::get_id()); }

  /** @return whether the current thread is holding X or U latch */
  bool have_u_or_x() const noexcept
  {
    const bool is_writer = writer.load(std::memory_order_relaxed) ==
      std::this_thread::get_id();
    assert(!is_writer || recursive);
    return is_writer;
  }
  /** @return whether the current thread is holding U but not X latch */
  bool have_u_not_x() const noexcept
  { return have_u_or_x() && !((recursive / RECURSIVE_X) & RECURSIVE_MAX); }
  /** @return whether the current thread is holding X latch */
  bool have_x() const noexcept
  { return have_u_or_x() && ((recursive / RECURSIVE_X) & RECURSIVE_MAX); }

  bool s_trylock() noexcept { return atomic_sux_lock::s_trylock(); }
  void s_lock() noexcept { atomic_sux_lock::s_lock(); }
  void s_unlock() noexcept { atomic_sux_lock::s_unlock(); }

  /** Acquire an update lock */
  void u_lock() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
      writer_recurse<true>();
    else
    {
      atomic_sux_lock::u_lock();
      assert(!recursive);
      recursive = RECURSIVE_U;
      set_writer(id);
    }
  }

  /** Acquire an exclusive lock */
  void x_lock() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
      writer_recurse<false>();
    else
    {
      atomic_sux_lock::x_lock();
      assert(!recursive);
      assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
      recursive = RECURSIVE_X;
      set_writer(id);
    }
  }

  /** Acquire an exclusive lock, for the ultimate owner to call set_writer() */
  void x_lock_disowned() noexcept
  {
    assert(!(writer.load(std::memory_order_relaxed) ==
             std::this_thread::get_id()));
    atomic_sux_lock::x_lock();
    assert(!recursive);
    assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
    recursive = RECURSIVE_X;
  }

  /** Acquire a recursive exclusive lock */
  void x_lock_recursive() noexcept { writer_recurse<false>(); }
  /** Acquire a recursive update lock */
  void u_lock_recursive() noexcept { writer_recurse<true>(); }

  /** Upgrade an update lock to exclusive */
  void u_x_upgrade() noexcept
  {
    assert(have_u_not_x());
    atomic_sux_lock::u_x_upgrade();
    recursive /= RECURSIVE_U;
  }

  /** Downgrade a single exclusive lock to an update lock */
  void x_u_downgrade() noexcept
  {
    assert(have_x());
    assert(recursive <= RECURSIVE_MAX);
    recursive *= RECURSIVE_U;
    atomic_sux_lock::x_u_downgrade();
  }

  /** Acquire an exclusive lock or upgrade an update lock
  @return whether U locks were upgraded to X */
  bool x_lock_upgraded() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
    {
      assert(recursive);
      static_assert(RECURSIVE_X == 1, "compatibility");
      if (!(recursive & RECURSIVE_MAX))
      {
        u_x_upgrade();
        return true;
      }
      writer_recurse<false>();
    }
    else
    {
      atomic_sux_lock::x_lock();
      assert(!recursive);
      recursive = RECURSIVE_X;
      set_writer(id);
    }
    return false;
  }

  /** Try to acquire an update lock.
      @return whether the update lock was acquired */
  bool u_trylock() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
    {
      writer_recurse<true>();
      return true;
    }
    if (!atomic_sux_lock::u_trylock())
      return false;
    assert(!recursive);
    assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
    recursive = RECURSIVE_U;
    set_writer(id);
    return true;
  }

  /** Try to acquire an update lock.
      @return whether the update lock was acquired */
  bool u_trylock_disowned() noexcept
  {
    assert(!(writer.load(std::memory_order_relaxed) ==
             std::this_thread::get_id()));
    if (atomic_sux_lock::u_trylock())
    {
      assert(!recursive);
      assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
      recursive = RECURSIVE_U;
      return true;
    }
    return false;
  }

  /** Try to acquire an exclusive lock.
      @return whether an exclusive lock was acquired */
  bool x_trylock() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
    {
      writer_recurse<false>();
      return true;
    }
    if (atomic_sux_lock::x_trylock())
    {
      assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
      assert(!recursive);
      recursive = RECURSIVE_X;
      set_writer(id);
      return true;
    }
    return false;
  }

  /** Release an update or exclusive lock.
      @tparam U   true=update lock, false=exclusive lock */
  template<bool U> void u_or_x_unlock() noexcept
  {
#ifndef NDEBUG
    const auto owner = writer.load(std::memory_order_relaxed);
#endif
    assert(owner == std::this_thread::get_id() ||
           (owner == std::thread::id{} &&
            recursive == (U ? RECURSIVE_U : RECURSIVE_X)));
    assert((recursive / (U ? RECURSIVE_U : RECURSIVE_X)) & RECURSIVE_MAX);

    if (!(recursive -= U ? RECURSIVE_U : RECURSIVE_X))
    {
      set_writer(std::thread::id{});
      if (U)
	atomic_sux_lock::u_unlock();
      else
	atomic_sux_lock::x_unlock();
    }
  }

  /** Release an update lock */
  void u_unlock() noexcept { u_or_x_unlock<true>(); }
  /** Release an exclusive lock */
  void x_unlock() noexcept { u_or_x_unlock<false>(); }
};
