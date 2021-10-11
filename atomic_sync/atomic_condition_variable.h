#pragma once
#include <atomic>
#include <cassert>

/** Tiny condition variable that keeps a count of waiters.

The interface intentionally resembles std::condition_variable.

In addition to wait(), we also define wait_shared() and wait_update(),
to go with atomic_shared_mutex.

We define the predicate is_waiting().

There is no explicit constructor or destructor.
The object is expected to be zero-initialized, so that
!is_waiting() will hold.

The implementation counts pending wait() requests, so that signal()
and broadcast() will only invoke notify_one() or notify_all() when
pending requests exist. */

class atomic_condition_variable : private std::atomic<uint32_t>
{
#if defined _WIN32 || __cplusplus >= 202002L
  void wait(uint32_t old) const noexcept { atomic::wait(old); }
#else /* Emulate the C++20 primitives */
  void notify_one() noexcept;
  void notify_all() noexcept;
  void wait(uint32_t old) const noexcept;
#endif
public:
  template<class mutex> void wait(mutex &m)
  {
    const uint32_t val = fetch_add(1, std::memory_order_acquire);
    m.unlock();
    wait(1 + val);
    m.lock();
  }

  template<class mutex> void wait_shared(mutex &m)
  {
    const uint32_t val = fetch_add(1, std::memory_order_acquire);
    m.unlock_shared();
    wait(1 + val);
    m.lock_shared();
  }

  template<class mutex> void wait_update(mutex &m)
  {
    const uint32_t val = fetch_add(1, std::memory_order_acquire);
    m.unlock_update();
    wait(1 + val);
    m.lock_update();
  }

  bool is_waiting() const noexcept { return load(std::memory_order_acquire); }

  void signal() noexcept
  { if (exchange(0, std::memory_order_release)) notify_one(); }

  void broadcast() noexcept
  { if (exchange(0, std::memory_order_release)) notify_all(); }
};
