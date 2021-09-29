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

/*

Unfortunately, compilers targeting IA-32 or AMD64 currently cannot
translate the following single-bit operations into Intel 80386 instructions:

     m.fetch_or(1<<b) & 1<<b       LOCK BTS b, m
     m.fetch_and(~(1<<b)) & 1<<b   LOCK BTR b, m
     m.fetch_xor(1<<b) & 1<<b      LOCK BTC b, m

Hence, we will manually translate fetch_or() using GCC-style inline
assembler code or a MSVC intrinsic function.

*/
#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
# define IF_FETCH_OR_GOTO(mem, bit, label)				\
  __asm__ goto("lock btsl $" #bit ", %0\n\t"				\
               "jc %l1" : : "m" (mem) : "cc", "memory" : label);
# define IF_NOT_FETCH_OR_GOTO(mem, bit, label)				\
  __asm__ goto("lock btsl $" #bit ", %0\n\t"				\
               "jnc %l1" : : "m" (mem) : "cc", "memory" : label);
#elif defined _MSC_VER && (defined _M_IX86 || defined _M_IX64)
# define IF_FETCH_OR_GOTO(mem, bit, label)				\
  if (_interlockedbittestandset(reinterpret_cast<volatile long*>(&mem), bit)) \
    goto label;
# define IF_NOT_FETCH_OR_GOTO(mem, bit, label)				\
  if (!_interlockedbittestandset(reinterpret_cast<volatile long*>(&mem), bit))\
    goto label;
#endif

void atomic_mutex::wait_and_lock() noexcept
{
  for (uint32_t lk = 1 + fetch_add(1, std::memory_order_relaxed);;)
  {
    if (lk & HOLDER)
    {
      wait(lk);
#ifdef IF_FETCH_OR_GOTO
    reload:
#endif
      lk = load(std::memory_order_relaxed);
    }
#ifdef IF_FETCH_OR_GOTO
    else
    {
      static_assert(HOLDER == (1U << 31), "compatibility");
      IF_FETCH_OR_GOTO(*this, 31, reload);
      std::atomic_thread_fence(std::memory_order_acquire);
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
    else
    {
#ifdef IF_NOT_FETCH_OR_GOTO
      static_assert(HOLDER == (1U << 31), "compatibility");
      IF_NOT_FETCH_OR_GOTO(*this, 31, acquired);
      lk|= HOLDER;
#else
      if (!((lk = fetch_or(HOLDER, std::memory_order_relaxed)) & HOLDER))
        goto acquired;
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
#ifdef IF_FETCH_OR_GOTO
    reload:
#endif
      lk = load(std::memory_order_relaxed);
    }
    else
    {
#ifdef IF_FETCH_OR_GOTO
      static_assert(HOLDER == (1U << 31), "compatibility");
      IF_FETCH_OR_GOTO(*this, 31, reload);
#else
      if ((lk = fetch_or(HOLDER, std::memory_order_relaxed)) & HOLDER)
        continue;
      else
        assert(lk);
#endif
    acquired:
      std::atomic_thread_fence(std::memory_order_acquire);
      return;
    }
  }
}
#endif
