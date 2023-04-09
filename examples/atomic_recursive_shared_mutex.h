#pragma once
#include "atomic_shared_mutex.h"
#include <thread>

/** Shared/Update/Exclusive lock with recursion (re-entrancy).

At most one thread may hold exclusive locks, such that no other threads
may hold any locks at the same time.

At most one thread may hold update locks at a time.

As long as no thread is holding exclusive locks,
any number of threads may hold shared locks, which are always
non-recursive (not reentrant).
If a thread is waiting for an exclusive lock(), further concurrent
lock_shared() requests will be blocked until the exclusive lock
has been granted and released in unlock().

This extends atomic_shared_mutex by allowing re-entrant
lock() and lock_update() calls. In lock_update_upgrade() and
update_lock_downgrade(), all locks will be transformed.

There is no explicit constructor or destructor.
The object may be zero-initialized, depending on the
value of std::thread::id{}. init() provides delayed
initialization. destroy() may be invoked to ensure that the
lock is unoccupied right before destruction.

We keep track of the thread that holds lock() or update_lock().
The predicates holding_lock(), holding_lock_update(), and
holding_lock_update_or_lock() are available.

If the current thread is not already holding a lock,
we allow the update or exclusive lock to be acquired in
a disowned state, so that set_holder() may be invoked
for the thread that will finally hold and release the lock.

If the current thread is known to already hold a lock,
lock_recursive() or lock_update_recursive() will allow
the recursion or re-entrancy count to be incremented quickly.

This is based on the ssux_lock in MariaDB Server 10.6. */
template<typename storage = shared_mutex_storage<>>
class atomic_recursive_shared_mutex : atomic_shared_mutex<storage>
{
  using super = atomic_shared_mutex<storage>;

  /** Numbers of update and exclusive locks.
  Protected by atomic_shared_mutex. */
  uint32_t recursive;
  /** The owner of update or exclusive locks.
  Protected by atomic_shared_mutex. */
  std::atomic<std::thread::id> writer{};

  /** The multiplier in recursive for X locks */
  static constexpr uint32_t RECURSIVE_X = 1U;
  /** The multiplier in recursive for U locks */
  static constexpr uint32_t RECURSIVE_U = 1U << 16;
  /** The maximum allowed level of recursion */
  static constexpr uint32_t RECURSIVE_MAX = RECURSIVE_U - 1;

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

  /** Release an update or exclusive lock.
      @tparam U   true=update lock, false=exclusive lock */
  template<bool U> void update_or_lock_unlock() noexcept
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
      set_holder(std::thread::id{});
      if (U)
        super::unlock_update();
      else
        super::unlock();
    }
  }

public:
  void init() noexcept
  {
    assert(!this->get_storage().is_locked_or_waiting());
    assert(!recursive);
    assert(writer == std::thread::id{});
  }

  void destroy() noexcept
  {
    assert(!this->get_storage().is_locked_or_waiting());
    assert(!recursive);
  }

  /** Transfer the ownership of a write lock to another thread
  @param id the new owner of the U or X lock */
  void set_holder(std::thread::id id) noexcept
  { writer.store(id, std::memory_order_relaxed); }

  /** Transfer the writer ownership to the current thread */
  void set_holder() noexcept { set_holder(std::this_thread::get_id()); }

  /** @return whether the current thread is holding exclusive or update latch */
  bool holding_lock_update_or_lock() const noexcept
  {
    const bool is_writer = writer.load(std::memory_order_relaxed) ==
      std::this_thread::get_id();
    assert(!is_writer || recursive);
    return is_writer;
  }
  /** @return whether the current thread is holding the update lock */
  bool holding_lock_update() const noexcept
  {
    return holding_lock_update_or_lock() &&
      !((recursive / RECURSIVE_X) & RECURSIVE_MAX);
  }
  /** @return whether the current thread is holding the exclusive lock */
  bool holding_lock() const noexcept
  {
    return holding_lock_update_or_lock() &&
      ((recursive / RECURSIVE_X) & RECURSIVE_MAX);
  }

  bool try_lock_shared() noexcept { return super::try_lock_shared(); }
  void lock_shared() noexcept { super::lock_shared(); }
  void spin_lock_shared(unsigned spin_rounds) noexcept
  { super::spin_lock_shared(spin_rounds); }
  void unlock_shared() noexcept { super::unlock_shared(); }

  /** Acquire an update lock */
  void lock_update() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
      writer_recurse<true>();
    else
    {
      super::lock_update();
      assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
      assert(!recursive);
      recursive = RECURSIVE_U;
      set_holder(id);
    }
  }

  void spin_lock_update(unsigned spin_rounds) noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
      writer_recurse<true>();
    else
    {
      super::spin_lock_update(spin_rounds);
      assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
      assert(!recursive);
      recursive = RECURSIVE_U;
      set_holder(id);
    }
  }

  /** Acquire an update lock, for set_holder() to be called later. */
  void lock_update_disowned() noexcept
  {
    assert(!(writer.load(std::memory_order_relaxed) ==
             std::this_thread::get_id()));
    super::lock_update();
    assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
    assert(!recursive);
    recursive = RECURSIVE_U;
  }

  /** Acquire an update lock, for set_holder() to be called later. */
  void spin_lock_update_disowned(unsigned spin_rounds) noexcept
  {
    assert(!(writer.load(std::memory_order_relaxed) ==
             std::this_thread::get_id()));
    super::spin_lock_update(spin_rounds);
    assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
    assert(!recursive);
    recursive = RECURSIVE_U;
  }

  /** Acquire an exclusive lock */
  void lock() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
      writer_recurse<false>();
    else
    {
      super::lock();
      assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
      assert(!recursive);
      recursive = RECURSIVE_X;
      set_holder(id);
    }
  }

  /** Acquire an exclusive lock */
  void spin_lock(unsigned spin_rounds) noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
      writer_recurse<false>();
    else
    {
      super::spin_lock(spin_rounds);
      assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
      assert(!recursive);
      recursive = RECURSIVE_X;
      set_holder(id);
    }
  }

  /** Acquire an exclusive lock, for set_holder() to be called later. */
  void lock_disowned() noexcept
  {
    assert(!(writer.load(std::memory_order_relaxed) ==
             std::this_thread::get_id()));
    super::lock();
    assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
    assert(!recursive);
    recursive = RECURSIVE_X;
  }

  /** Acquire an exclusive lock, for set_holder() to be called later. */
  void spin_lock_disowned(unsigned spin_rounds) noexcept
  {
    assert(!(writer.load(std::memory_order_relaxed) ==
             std::this_thread::get_id()));
    super::spin_lock(spin_rounds);
    assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
    assert(!recursive);
    recursive = RECURSIVE_X;
  }

  /** Acquire a recursive exclusive lock */
  void lock_recursive() noexcept { writer_recurse<false>(); }
  /** Acquire a recursive update lock */
  void lock_update_recursive() noexcept { writer_recurse<true>(); }

  /** Upgrade an update lock to exclusive */
  void update_lock_upgrade() noexcept
  {
    assert(holding_lock_update());
    super::update_lock_upgrade();
    recursive /= RECURSIVE_U;
  }

  /** Downgrade a single exclusive lock to an update lock */
  void update_lock_downgrade() noexcept
  {
    assert(holding_lock());
    assert(recursive <= RECURSIVE_MAX);
    recursive *= RECURSIVE_U;
    super::update_lock_downgrade();
  }

  /** Acquire an exclusive lock or upgrade an update lock
  @return whether U locks were upgraded to X */
  bool lock_upgraded() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
    {
      assert(recursive);
      static_assert(RECURSIVE_X == 1, "compatibility");
      if (!(recursive & RECURSIVE_MAX))
      {
        update_lock_upgrade();
        return true;
      }
      writer_recurse<false>();
    }
    else
    {
      super::lock();
      assert(!recursive);
      recursive = RECURSIVE_X;
      set_holder(id);
    }
    return false;
  }

  /** Try to acquire an update lock.
      @return whether the update lock was acquired */
  bool try_lock_update() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
    {
      writer_recurse<true>();
      return true;
    }
    if (!super::try_lock_update())
      return false;
    assert(!recursive);
    assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
    recursive = RECURSIVE_U;
    set_holder(id);
    return true;
  }

  /** Try to acquire an update lock, for set_holder() to be called later.
      @return whether the update lock was acquired */
  bool try_lock_update_disowned() noexcept
  {
    assert(!(writer.load(std::memory_order_relaxed) ==
             std::this_thread::get_id()));
    if (super::try_lock_update())
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
  bool try_lock() noexcept
  {
    const std::thread::id id = std::this_thread::get_id();
    if (writer.load(std::memory_order_relaxed) == id)
    {
      writer_recurse<false>();
      return true;
    }
    if (super::try_lock())
    {
      assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
      assert(!recursive);
      recursive = RECURSIVE_X;
      set_holder(id);
      return true;
    }
    return false;
  }

  /** Try to acquire an exclusive lock, for set_holder() to be called later.
      @return whether the update lock was acquired */
  /** Try to acquire an exclusive lock.
      @return whether an exclusive lock was acquired */
  bool try_lock_disowned() noexcept
  {
    assert(!(writer.load(std::memory_order_relaxed) ==
             std::this_thread::get_id()));
    if (super::try_lock())
    {
      assert(writer.load(std::memory_order_relaxed) == std::thread::id{});
      assert(!recursive);
      recursive = RECURSIVE_X;
      return true;
    }
    return false;
  }

  /** Release an update lock */
  void unlock_update() noexcept { update_or_lock_unlock<true>(); }
  /** Release an exclusive lock */
  void unlock() noexcept { update_or_lock_unlock<false>(); }
};
