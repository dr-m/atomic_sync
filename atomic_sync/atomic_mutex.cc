#include "atomic_mutex.h"

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
void atomic_mutex::notify_one() noexcept {FUTEX(WAKE, 1);}
inline void atomic_mutex::wait(uint32_t old) const noexcept {FUTEX(WAIT, old);}
#endif

void atomic_mutex::wait_and_lock() noexcept
{
  for (uint32_t lk = 1 + fetch_add(1, std::memory_order_relaxed);;)
  {
    if (lk & HOLDER)
    {
      wait(lk);
      lk = load(std::memory_order_relaxed);
    }
    else if (compare_exchange_weak(lk, lk | HOLDER, std::memory_order_acquire,
                                   std::memory_order_relaxed))
    {
      assert(lk);
      return;
    }
  }
}

#ifdef SPINLOOP
/** The count of 125 seems to yield the best NUMA performance on
Intel Xeon E5-2630 v4 (Haswell microarchitecture) */
unsigned atomic_spin_mutex::spin_rounds = SPINLOOP;
# ifdef _WIN32
#  include <windows.h>
# endif

void atomic_spin_mutex::wait_and_lock() noexcept
{
  uint32_t lk = 1 + fetch_add(1, std::memory_order_relaxed);

  /* We hope to avoid system calls when the conflict is resolved quickly. */
  for (auto spin = spin_rounds;;)
  {
    assert(~HOLDER & lk);
    if (lk & HOLDER)
      lk = load(std::memory_order_relaxed);
    else if (compare_exchange_weak(lk, lk | HOLDER, std::memory_order_acquire,
                                   std::memory_order_relaxed))
      return;
    else
    {
# ifdef _WIN32
      YieldProcessor();
# elif defined(_ARCH_PWR8)
      __ppc_get_timebase();
# elif defined __GNUC__ && defined __i386__ || defined __x86_64__
      __asm__ __volatile__ ("pause");
# endif
    }
    if (!--spin)
      break;
  }

  for (;;)
  {
    assert(~HOLDER & lk);
    if (lk & HOLDER)
    {
      wait(lk);
      lk = load(std::memory_order_relaxed);
    }
    else if (compare_exchange_weak(lk, lk | HOLDER, std::memory_order_acquire,
				   std::memory_order_relaxed))
      break;
  }
}
#endif
