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
# elif defined __FreeBSD__
#   include <sys/types.h>
#   include <sys/umtx.h>
#   define FUTEX_WAKE UMTX_OP_WAKE_PRIVATE
#   define FUTEX_WAIT UMTX_OP_WAIT_UINT_PRIVATE
#   define FUTEX(op,n) _umtx_op(this, FUTEX_ ## op, n, nullptr, nullptr)
# elif defined __DragonFly__
#   include <unistd.h>
#   define FUTEX_WAKE(a,n) umtx_wakeup(a,n)
#   define FUTEX_WAIT(a,n) umtx_sleep(a,n,0)
#   define FUTEX(op,n) FUTEX_ ## op((volatile int*) this, int(n))
# else
#  error "no C++20 nor futex support"
# endif
void atomic_shared_mutex::notify_one() noexcept { FUTEX(WAKE, 1); }
inline void atomic_shared_mutex::wait(uint32_t old) const noexcept
{ FUTEX(WAIT, old); }
#endif

void atomic_shared_mutex::lock_wait(uint32_t lk) noexcept
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

void atomic_shared_mutex::shared_lock_wait() noexcept
{
  for (;;)
  {
    ex.lock();
    bool acquired = try_lock_shared();
    ex.unlock();
    if (acquired)
      break;
  }
}

void atomic_shared_mutex::spin_shared_lock_wait() noexcept
{
  for (;;)
  {
    ex.spin_lock();
    bool acquired = try_lock_shared();
    ex.unlock();
    if (acquired)
      break;
  }
}
