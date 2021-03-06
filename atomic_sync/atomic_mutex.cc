#include "atomic_mutex.h"

#if !defined _WIN32 && __cplusplus < 202002L
/* Emulate the C++20 primitives */
# include <climits>
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
#if defined __clang_major__ && __clang_major__ < 10
/* Only clang-10 introduced support for asm goto */
#elif defined __GNUC__ && (defined __i386__ || defined __x86_64__)
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

#ifndef SPINLOOP
void atomic_mutex::spin_wait_and_lock() noexcept { wait_and_lock(); }
#else
/** The count of 50 seems to yield the best NUMA performance on
Intel Xeon E5-2630 v4 (Haswell microarchitecture) */
unsigned atomic_mutex::spin_rounds = SPINLOOP;
# ifdef _WIN32
#  include <windows.h>
# endif

void atomic_mutex::spin_wait_and_lock() noexcept
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
# elif defined __GNUC__ && defined _ARCH_PWR8
      __builtin_ppc_get_timebase();
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
