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
#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
    reload:
#endif
      lk = load(std::memory_order_relaxed);
    }
#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
    else
    {
      static_assert(HOLDER == (1U << 31), "compatibility");
      __asm__ goto("lock btsq $31, %0\n\t"
                   "jc %l1" : : "m" (*this) : "cc", "memory" : reload);
      std::atomic_thread_fence(std::memory_order_acquire);
      return;
    }
#elif defined _M_IX86||defined _M_X64||defined __i386__||defined __x86_64__
    else if (compare_exchange_weak(lk, lk | HOLDER, std::memory_order_acquire,
                                   std::memory_order_relaxed))
    {
      assert(lk);
      return;
    }
#else
    else if (!((lk = fetch_or(HOLDER, std::memory_order_relaxed)) & HOLDER))
    {
      assert(lk);
      std::atomic_thread_fence(std::memory_order_acquire);
      return;
    }
    else
      assert(~HOLDER & lk);
#endif
  }
}

#ifdef SPINLOOP
/** The count of 50 seems to yield the best NUMA performance on
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
#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#elif defined _M_IX86||defined _M_X64||defined __i386__||defined __x86_64__
    else if (compare_exchange_weak(lk, lk | HOLDER, std::memory_order_acquire,
                                   std::memory_order_relaxed))
      return;
#else
    else if (!((lk = fetch_or(HOLDER, std::memory_order_relaxed)) & HOLDER))
      goto acquired;
#endif
    else
    {
#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
      static_assert(HOLDER == (1U << 31), "compatibility");
      __asm__ goto("lock btsq $31, %0\n\t"
                   "jnc %l1" : : "m" (*this) : "cc", "memory" : acquired);
      lk|= HOLDER;
#endif
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
#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
    reload:
#endif
      lk = load(std::memory_order_relaxed);
    }
#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
    else
    {
      static_assert(HOLDER == (1U << 31), "compatibility");
      __asm__ goto("lock btsq $31, %0\n\t"
                   "jc %l1" : : "m" (*this) : "cc", "memory" : reload);
    acquired:
      std::atomic_thread_fence(std::memory_order_acquire);
      return;
    }
#elif defined _M_IX86||defined _M_X64||defined __i386__||defined __x86_64__
    else if (compare_exchange_weak(lk, lk | HOLDER, std::memory_order_acquire,
                                   std::memory_order_relaxed))
      return;
#else
    else if (!((lk = fetch_or(HOLDER, std::memory_order_relaxed)) & HOLDER))
    {
    acquired:
      assert(lk);
      std::atomic_thread_fence(std::memory_order_acquire);
      return;
    }
    else
      assert(~HOLDER & lk);
#endif
  }
}
#endif
