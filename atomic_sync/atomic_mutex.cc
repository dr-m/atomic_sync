#include "atomic_shared_mutex.h"

#if !defined _WIN32 && __cplusplus < 202002L
/* Emulate the C++20 primitives */
# include <climits>
# if defined __linux__
#  include <linux/futex.h>
#  include <unistd.h>
#  include <sys/syscall.h>
#  define FUTEX(op,m,n)                                                 \
   syscall(SYS_futex, m, FUTEX_ ## op ## _PRIVATE, n, nullptr, nullptr, 0)
# elif defined __OpenBSD__
#  include <sys/time.h>
#  include <sys/futex.h>
#  define FUTEX(op,m,n)                                                 \
   futex((volatile uint32_t*) m, FUTEX_ ## op, n, nullptr, nullptr)
# elif defined __FreeBSD__
#   include <sys/types.h>
#   include <sys/umtx.h>
#   define FUTEX_WAKE UMTX_OP_WAKE_PRIVATE
#   define FUTEX_WAIT UMTX_OP_WAIT_UINT_PRIVATE
#   define FUTEX(op,m,n) _umtx_op(m, FUTEX_ ## op, n, nullptr, nullptr)
# elif defined __DragonFly__
#   include <unistd.h>
#   define FUTEX_WAKE(m,n) umtx_wakeup(m,n)
#   define FUTEX_WAIT(m,n) umtx_sleep(m,n,0)
#   define FUTEX(op,m,n) FUTEX_ ## op((volatile int*) m, int(n))
# else
#  error "no C++20 nor futex support"
# endif
template<>
void mutex_storage<uint32_t>::notify_one() noexcept {FUTEX(WAKE, &m, 1);}
template<>
inline void mutex_storage<uint32_t>::wait(uint32_t old) const noexcept
{FUTEX(WAIT, &m, old);}
#endif

/*

Unfortunately, compilers targeting IA-32 or AMD64 currently cannot
translate the following single-bit operations into Intel 80386 instructions:

     m.fetch_or(1<<b) & 1<<b       LOCK BTS b, m
     m.fetch_and(~(1<<b)) & 1<<b   LOCK BTR b, m
     m.fetch_xor(1<<b) & 1<<b      LOCK BTC b, m

In g++-12 and clang++-15 this actually works, except for b==31:
https://gcc.gnu.org/bugzilla/show_bug.cgi?id=102566
https://github.com/llvm/llvm-project/issues/37322

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

template<typename T>
void mutex_storage<T>::lock_wait() noexcept
{
  T lk = WAITER + m.fetch_add(WAITER, std::memory_order_relaxed);
  for (;;)
  {
    if (lk & HOLDER)
    {
#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
      this->wait(lk);
#else
      m.wait(lk);
#endif
#if defined __i386__||defined __x86_64__||defined _M_IX86||defined _M_IX64
    reload:
#endif
      lk = m.load(std::memory_order_relaxed);
    }
#if defined __i386__||defined __x86_64__||defined _M_IX86||defined _M_IX64
    else
    {
# ifdef IF_FETCH_OR_GOTO
      static_assert(HOLDER == (1U << 31), "compatibility");
      IF_FETCH_OR_GOTO(*this, 31, reload);
# else
      if (m.fetch_or(HOLDER, std::memory_order_relaxed) & HOLDER)
        goto reload;
# endif
      std::atomic_thread_fence(std::memory_order_acquire);
      return;
    }
#else
    else if (!((lk = m.fetch_or(HOLDER, std::memory_order_relaxed)) &
               HOLDER))
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

#ifdef _WIN32
# include <windows.h>
#endif

#ifdef __GNUC__
__attribute__((noinline))
#elif defined _MSC_VER
__declspec(noinline)
#endif

/** Back off from the memory bus for a while. */
static void spin_pause()
{
  /* Note: the optimal value may be ISA implementation dependent. */
  for (int rounds = 5; rounds--; )
  {
#ifdef _WIN32
    YieldProcessor();
#elif defined __GNUC__
# ifdef _ARCH_PWR8
    __builtin_ppc_get_timebase();
# elif defined __i386__ || defined __x86_64__
    __asm__ __volatile__ ("pause");
# else
    __asm__ __volatile__ ("":::"memory");
# endif
#endif
  }
}

template<typename T>
void mutex_storage<T>::spin_lock_wait(unsigned spin_rounds) noexcept
{
  T lk = WAITER + m.fetch_add(WAITER, std::memory_order_relaxed);

  /* We hope to avoid system calls when the conflict is resolved quickly. */
  for (auto spin = spin_rounds;; spin_pause())
  {
    assert(~HOLDER & lk);
    lk = m.load(std::memory_order_relaxed);
    if (!(lk & HOLDER))
    {
#ifdef IF_NOT_FETCH_OR_GOTO
      static_assert(HOLDER == (1U << 31), "compatibility");
      IF_NOT_FETCH_OR_GOTO(*this, 31, acquired);
      lk|= HOLDER;
#else
      if (!((lk = m.fetch_or(HOLDER, std::memory_order_relaxed)) & HOLDER))
        goto acquired;
#endif
    }
    if (!--spin)
      break;
  }

  for (;;)
  {
    assert(~HOLDER & lk);
    if (lk & HOLDER)
    {
#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
      this->wait(lk);
#else
      m.wait(lk);
#endif
#ifdef IF_FETCH_OR_GOTO
    reload:
#endif
      lk = m.load(std::memory_order_relaxed);
    }
    else
    {
#ifdef IF_FETCH_OR_GOTO
      static_assert(HOLDER == (1U << 31), "compatibility");
      IF_FETCH_OR_GOTO(*this, 31, reload);
#else
      if ((lk = m.fetch_or(HOLDER, std::memory_order_relaxed)) & HOLDER)
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

template void mutex_storage<uint32_t>::lock_wait() noexcept;
template void mutex_storage<uint32_t>::spin_lock_wait(unsigned) noexcept;

template<typename T>
void shared_mutex_storage<T>::lock_inner_wait(T lk) noexcept
{
  assert(lk < X);
  lk |= X;

  do
  {
    assert(lk > X);
#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
    FUTEX(WAIT, &inner, lk);
#else
    inner.wait(lk);
#endif
    lk = inner.load(std::memory_order_acquire);
  }
  while (lk != X);
}

template
void shared_mutex_storage<uint32_t>::lock_inner_wait(uint32_t) noexcept;

template<typename T>
void shared_mutex_storage<T>::shared_lock_wait() noexcept
{
  lock_outer();
#ifndef NDEBUG
  type lk =
#endif
    inner.fetch_add(WAITER, std::memory_order_acquire);
  unlock_outer();
  assert(!(lk & X));
}

template
void shared_mutex_storage<uint32_t>::shared_lock_wait() noexcept;

template<typename T>
void shared_mutex_storage<T>::spin_shared_lock_wait(unsigned spin_rounds)
  noexcept
{
  /* We hope to avoid system calls when the conflict is resolved quickly. */
  for (auto spin = spin_rounds;; spin_pause())
  {
    if (shared_lock_inner())
      return;
    if (--spin)
      break;
  }

  shared_lock_wait();
}

template
void shared_mutex_storage<uint32_t>::spin_shared_lock_wait(unsigned) noexcept;

#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
template<typename T>
void shared_mutex_storage<T>::shared_unlock_inner_notify() noexcept
{FUTEX(WAKE, &inner, 1);}
template
void shared_mutex_storage<uint32_t>::shared_unlock_inner_notify() noexcept;
#endif
