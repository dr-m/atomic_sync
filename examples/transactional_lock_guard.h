#pragma once

#ifndef WITH_ELISION
#elif defined __powerpc64__
#elif defined __s390__
#elif defined __aarch64__ && defined __GNUC__ && __GNUC__ >= 10
#elif defined __aarch64__ && defined __clang__ && __clang_major__ >= 10
#elif defined _MSC_VER && (defined _M_IX86 || defined _M_X64)
#elif defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#else
# error /* Transactional memory has not been implemented for this ISA */
#endif

#ifndef WITH_ELISION
# define TRANSACTIONAL_TARGET /* nothing */
# define TRANSACTIONAL_INLINE /* nothing */
#else
# if defined __i386__||defined __x86_64__||defined _M_IX86||defined _M_X64
extern bool have_transactional_memory;
#  include <immintrin.h>
#  ifdef __GNUC__
#   define TRANSACTIONAL_TARGET __attribute__((target("rtm")))
#   define TRANSACTIONAL_INLINE __attribute__((target("rtm"),always_inline))
#  else
#   define TRANSACTIONAL_TARGET /* nothing */
#   define TRANSACTIONAL_INLINE /* nothing */
#  endif

TRANSACTIONAL_INLINE static inline bool xbegin()
{ return have_transactional_memory && _xbegin() == _XBEGIN_STARTED; }
TRANSACTIONAL_INLINE static inline void xabort() { _xabort(0); }
TRANSACTIONAL_INLINE static inline void xend() { _xend(); }
# elif defined __powerpc64__ || defined __s390__
#  define TRANSACTIONAL_TARGET __attribute__((target("hot")))
#  define TRANSACTIONAL_INLINE __attribute__((target("hot"),always_inline))
extern bool have_transactional_memory;

bool xbegin();
bool xabort();
bool xend();
# elif defined __aarch64__
/* FIXME: No runtime detection of TME has been implemented! */
constexpr bool have_transactional_memory = true;

#  define TRANSACTIONAL_INLINE __attribute__((always_inline))
#  ifdef __clang__
#   define TRANSACTIONAL_TARGET __attribute__((target("tme")))
#  else
#   define TRANSACTIONAL_TARGET __attribute__((target("+tme")))
#  endif

TRANSACTIONAL_INLINE static inline bool xbegin()
{
  int ret;
  __asm__ __volatile__ ("tstart %x0" : "=r"(ret) :: "memory");
  return !ret;
}

TRANSACTIONAL_INLINE static inline void xabort()
{ __asm__ __volatile__ ("tcancel %x0" :: "n"(i) :: "memory"); }

TRANSACTIONAL_INLINE static inline void xend()
{ __asm__ volatile ("tcommit" ::: "memory"); }
# endif
#endif

template<class mutex>
class transactional_lock_guard
{
  mutex &m;

public:
  TRANSACTIONAL_INLINE transactional_lock_guard(mutex &m) : m(m)
  {
#ifdef WITH_ELISION
    if (xbegin())
    {
      if (was_elided())
        return;
      xabort();
    }
#endif
    m.lock();
  }
  transactional_lock_guard(const transactional_lock_guard &) = delete;
  TRANSACTIONAL_INLINE ~transactional_lock_guard() noexcept
  {
#ifdef WITH_ELISION
    if (was_elided()) xend(); else
#endif
    m.unlock();
  }

#ifdef WITH_ELISION
  bool was_elided() const noexcept
  { return !m.get_storage().is_locked_or_waiting(); }
#else
  bool was_elided() const noexcept { return false; }
#endif
};

template<class mutex>
class transactional_shared_lock_guard
{
  mutex &m;
#ifdef WITH_ELISION
  bool elided;
#else
  static constexpr bool elided = false;
#endif

public:
  TRANSACTIONAL_INLINE transactional_shared_lock_guard(mutex &m) : m(m)
  {
#ifdef WITH_ELISION
    if (xbegin())
    {
      if (!m.get_storage().is_locked())
      {
        elided = true;
        return;
      }
      xabort();
    }
    elided = false;
#endif
    m.lock_shared();
  }
  transactional_shared_lock_guard(const transactional_shared_lock_guard &) =
    delete;
  TRANSACTIONAL_INLINE ~transactional_shared_lock_guard() noexcept
  {
#ifdef WITH_ELISION
    if (was_elided()) xend(); else
#endif
    m.unlock_shared();
  }

  bool was_elided() const noexcept { return elided; }
};

template<class mutex>
class transactional_update_lock_guard
{
  mutex &m;

public:
  TRANSACTIONAL_INLINE transactional_update_lock_guard(mutex &m) : m(m)
  {
#ifdef WITH_ELISION
    if (xbegin())
    {
      if (was_elided())
        return;
      xabort();
    }
#endif
    m.lock_update();
  }
  transactional_update_lock_guard(const transactional_update_lock_guard &) =
    delete;
  TRANSACTIONAL_INLINE ~transactional_update_lock_guard() noexcept
  {
#ifdef WITH_ELISION
    if (was_elided()) xend(); else
#endif
    m.unlock_update();
  }

#ifdef WITH_ELISION
  bool was_elided() const noexcept
  { return !m.get_storage().is_locked_or_waiting(); }
#else
  bool was_elided() const noexcept { return false; }
#endif
};
