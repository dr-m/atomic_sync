#include "atomic_shared_mutex.h"

#if defined __linux__ || (!defined _WIN32 && __cplusplus < 202002L)
/* Emulate the C++20 primitives */
# include <climits>
# if defined __linux__
#  include <linux/futex.h>
#  include <unistd.h>
#  include <sys/syscall.h>
#  define FUTEX(op,m,n)                                                \
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

# ifdef __linux__
#  ifndef __NR_futex_wake
#   define __NR_futex_wake 452
#   define __NR_futex_wait 453
#   define FUTEX2_SIZE_U8 0
#   define FUTEX2_SIZE_U32 2
#   define FUTEX2_NUMA 4
#   define FUTEX2_PRIVATE FUTEX_PRIVATE_FLAG
#  endif
#  define FUTEX2_WAKE(m,n)                                      \
  syscall(__NR_futex_wake, m, unsigned(~0U), n,                 \
          FUTEX2_SIZE_U32 | FUTEX2_NUMA | FUTEX2_PRIVATE)
#  define FUTEX2_WAIT(m,n)                                      \
  syscall(__NR_futex_wait, m, unsigned(~0U), n,                 \
          FUTEX2_SIZE_U32 | FUTEX2_NUMA | FUTEX2_PRIVATE,   \
          0, 0/*CLOCK_REALTIME*/)

static void futex1_wake(const uint32_t *m) { FUTEX(WAKE, &m, 1); }
static void futex1_wait(const uint32_t *m, uint32_t old)
{ FUTEX(WAIT, &m, old); }

static void futex2_wake(const uint32_t *m)
{ FUTEX2_WAKE(m, 1); }
static void futex2_wait(const uint32_t *m, uint32_t old)
{ FUTEX2_WAIT(m, old); }

#include <errno.h>
extern "C"
{
static void (*resolve_futex_wake(void))(const uint32_t*)
{
  syscall(__NR_futex_wake, nullptr, 0xffff, 1, FUTEX2_SIZE_U8);
  return errno == ENOSYS ? futex1_wake : futex2_wake;
}

static void (*resolve_futex_wait(void))(const uint32_t*, uint32_t)
{
  syscall(__NR_futex_wake, nullptr, 0xffff, 1, FUTEX2_SIZE_U8);
  return errno == ENOSYS ? futex1_wait : futex2_wait;
}
}

static void futex_wake(const uint32_t *m)
__attribute__ ((ifunc ("resolve_futex_wake")));
static void futex_wait(const uint32_t *m, uint32_t old)
__attribute__ ((ifunc ("resolve_futex_wait")));

template<>
void mutex_storage<uint32_t>::notify_one() noexcept
{futex_wake(reinterpret_cast<uint32_t*>(&m));}
template<>
inline void mutex_storage<uint32_t>::wait(uint32_t old) const noexcept
{futex_wait(reinterpret_cast<const uint32_t*>(&m), old);}
# else
template<>
void mutex_storage<uint32_t>::notify_one() noexcept {FUTEX(WAKE, &m, 1);}
template<>
inline void mutex_storage<uint32_t>::wait(uint32_t old) const noexcept
{FUTEX(WAIT, &m, old);}
# endif
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

#ifdef __linux__
template<typename T>
void mutex_storage<T>::lock_wait() noexcept
{
  uint64_t old_lk = m_and_node.load(std::memory_order_relaxed);
  constexpr uint64_t first_holder_waiter = HOLDER | WAITER | ~0ULL << 32;
  uint64_t waiter = WAITER;
  for (;;) {
    assert(T(old_lk) || waiter);
    uint64_t lk = T(old_lk) ? (old_lk + waiter) | HOLDER : first_holder_waiter;
    if (!m_and_node.compare_exchange_strong(old_lk, lk,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed));
    else if (!(old_lk & HOLDER))
      return;
    else {
      wait(T(lk));
      waiter = 0;
    }
  }
}

template<typename T>
void mutex_storage<T>::spin_lock_wait(unsigned spin_rounds) noexcept
{
  uint64_t old_lk = m_and_node.load(std::memory_order_relaxed);
  constexpr uint64_t first_holder_waiter = HOLDER | WAITER | ~0ULL << 32;
  uint64_t waiter = WAITER;

  /* We hope to avoid system calls when the conflict is resolved quickly. */
  for (auto spin = spin_rounds;;) {
    assert(T(old_lk) || waiter);
    uint64_t lk = T(old_lk) ? (old_lk + waiter) | HOLDER : first_holder_waiter;
    if (!m_and_node.compare_exchange_strong(old_lk, lk,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed));
    else if (!(old_lk & HOLDER))
      return;
    else {
      waiter = 0;
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

  for (;;) {
    assert(T(old_lk) || waiter);
    uint64_t lk = T(old_lk) ? (old_lk + waiter) | HOLDER : first_holder_waiter;
    if (!m_and_node.compare_exchange_strong(old_lk, lk,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed));
    else if (!(old_lk & HOLDER))
      return;
    else {
      wait(T(lk));
      waiter = 0;
    }
  }
}
#else
template<typename T>
void mutex_storage<T>::lock_wait() noexcept
{
  T lk = WAITER + m.fetch_add(WAITER, std::memory_order_relaxed);
  for (;;)
  {
    if (lk & HOLDER)
    {
      wait(lk);
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

template<typename T>
void mutex_storage<T>::spin_lock_wait(unsigned spin_rounds) noexcept
{
  T lk = WAITER + m.fetch_add(WAITER, std::memory_order_relaxed);

  /* We hope to avoid system calls when the conflict is resolved quickly. */
  for (auto spin = spin_rounds;;)
  {
    assert(~HOLDER & lk);
    if (lk & HOLDER)
      lk = m.load(std::memory_order_relaxed);
    else
    {
#ifdef IF_NOT_FETCH_OR_GOTO
      static_assert(HOLDER == (1U << 31), "compatibility");
      IF_NOT_FETCH_OR_GOTO(*this, 31, acquired);
      lk|= HOLDER;
#else
      if (!((lk = m.fetch_or(HOLDER, std::memory_order_relaxed)) & HOLDER))
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
#endif

template<typename T>
void shared_mutex_storage<T>::lock_inner_wait(T lk) noexcept
{
  assert(lk < X);
  lk |= X;

  do
  {
    assert(lk > X);
#ifdef __linux__
    futex_wait(reinterpret_cast<const uint32_t*>(&inner), T(lk));
#elif !defined _WIN32 && __cplusplus < 202002L
    FUTEX(WAIT, &inner, lk);
#else
    inner.wait(lk);
#endif
    lk = inner.load(std::memory_order_acquire);
  }
  while (lk != X);
}

template void mutex_storage<uint32_t>::lock_wait() noexcept;
template void mutex_storage<uint32_t>::spin_lock_wait(unsigned) noexcept;

template
void shared_mutex_storage<uint32_t>::lock_inner_wait(uint32_t) noexcept;

#if defined __linux__ || (!defined _WIN32 && __cplusplus < 202002L)
template<typename T>
void shared_mutex_storage<T>::shared_unlock_inner_notify() noexcept
{
#ifdef __linux__
  futex_wake(reinterpret_cast<const uint32_t*>(&inner));
#else
  FUTEX(WAKE, &inner, 1);
#endif
}
template
void shared_mutex_storage<uint32_t>::shared_unlock_inner_notify() noexcept;
#endif
