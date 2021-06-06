#include "atomic_mutex.h"

#ifdef SPINLOOP
unsigned atomic_mutex::spin_rounds;
# ifdef _WIN32
#  include <windows.h>
# endif
#endif

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
  uint32_t lk = 1 + fetch_add(1, std::memory_order_relaxed);
#ifdef SPINLOOP
  /* We hope to avoid system calls when the conflict is resolved quickly. */
  for (auto spin = spin_rounds; spin; spin--)
  {
    lk &= ~HOLDER;
    assert(lk);
    while (!compare_exchange_weak(lk, HOLDER | (lk - 1),
                                  std::memory_order_acquire,
                                  std::memory_order_relaxed))
      if (lk & HOLDER)
        goto occupied;
    return;
occupied:
# ifdef _WIN32
    YieldProcessor();
# elif defined(_ARCH_PWR8)
    __ppc_get_timebase();
# elif defined __GNUC__ && defined __i386__ || defined __x86_64__
    __asm__ __volatile__ ("pause");
# endif
  }
  lk = load(std::memory_order_relaxed);
#endif
  for (;;)
  {
    while (!(lk & HOLDER))
    {
      assert(lk);
      if (compare_exchange_weak(lk, HOLDER | (lk - 1),
                                std::memory_order_acquire,
                                std::memory_order_relaxed))
        return;
    }
    assert(lk > HOLDER);
    wait(lk);
    lk = load(std::memory_order_relaxed);
  }
}
