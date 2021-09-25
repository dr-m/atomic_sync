#include "atomic_shared_mutex.h"
#include <thread>

#ifdef _WIN32
#elif __cplusplus >= 202002L
#else /* Emulate the C++20 primitives */
# if defined __linux__
#  include <linux/futex.h>
#  include <unistd.h>
#  include <sys/syscall.h>
#  define FUTEX(op,n) \
   syscall(SYS_futex, this, FUTEX_ ## op ## _PRIVATE, n, nullptr, nullptr, 0)
# elif defined __OpenBSD__
#  include <sys/time.h>
#  include <sys/futex.h>
#  define FUTEX(op,n) \
   futex((volatile uint32_t*) this, FUTEX_ ## op, n, nullptr, nullptr)
# else
#  error "no C++20 nor futex support"
# endif
void atomic_shared_mutex_impl<atomic_mutex>::notify_one() noexcept
{ FUTEX(WAKE, 1); }
inline void atomic_mutex_impl<atomic_mutex>::wait(uint32_t old) const noexcept
{ FUTEX(WAIT, old); }
# ifdef SPINLOOP
void atomic_shared_mutex_impl<atomic_spin_mutex>::notify_one() noexcept
{ FUTEX(WAKE, 1); }
inline void atomic_mutex_impl<atomic_spin_mutex>::wait(uint32_t old)
const noexcept
{ FUTEX(WAIT, old); }
# endif
#endif

template<typename mutex>
void atomic_shared_mutex_impl<mutex>::lock_wait(uint32_t lk) noexcept
{
  assert(ex.is_locked());
  assert(lk);
  assert(lk < X);
  lk |= X;
  do
  {
    assert(lk > X);
    wait(lk);
    lk = load(std::memory_order_acquire);
  }
  while (lk != X);
}

template<typename mutex>
void atomic_shared_mutex_impl<mutex>::shared_lock_wait() noexcept
{
  for (;;)
  {
    ex.lock(std::memory_order_relaxed);
    bool acquired= try_lock_shared();
    ex.unlock(std::memory_order_relaxed);
    if (acquired)
      break;
    std::this_thread::yield();
  }
}

template
void atomic_shared_mutex_impl<atomic_mutex>::lock_wait(uint32_t) noexcept;
template
void atomic_shared_mutex_impl<atomic_mutex>::shared_lock_wait() noexcept;
#ifdef SPINLOOP
template
void atomic_shared_mutex_impl<atomic_spin_mutex>::lock_wait(uint32_t) noexcept;
template
void atomic_shared_mutex_impl<atomic_spin_mutex>::shared_lock_wait() noexcept;
#endif
