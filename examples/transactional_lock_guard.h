#pragma once

#if defined _MSC_VER && (defined _M_IX86 || defined _M_X64)
#elif defined __GNUC__ && (defined __i386__ || defined __x86_64__)
# if __GNUC__ >= 8
# elif defined __clang__ && (__clang_major__ > 3 || (__clang_major__ == 3 && __clang__minor >= 8))
# else
#  define NO_ELISION
# endif
#else /* Transactional memory has not been implemented for this ISA */
# define NO_ELISION
#endif

#ifdef NO_ELISION
# define TRANSACTIONAL_TARGET /* nothing */
#else
extern bool transactional_lock_guard_can_elide;

# if defined __i386__||defined __x86_64__||defined _M_IX86||defined _M_X64
#  include <immintrin.h>
#  ifdef __GNUC__
#   define TRANSACTIONAL_TARGET __attribute__((target("rtm")))
#   define INLINE __attribute__((always_inline))
#  else
#   define TRANSACTIONAL_TARGET /* nothing */
#   define INLINE /* nothing */
#  endif

TRANSACTIONAL_TARGET INLINE static inline bool xbegin()
{
  return transactional_lock_guard_can_elide && _xbegin() == _XBEGIN_STARTED;
}

template<unsigned char i>
TRANSACTIONAL_TARGET INLINE static inline void xabort() { _xabort(i); }

TRANSACTIONAL_TARGET INLINE static inline void xend() { _xend(); }
# endif
#endif

template<class mutex>
class transactional_lock_guard
{
  mutex &m;

public:
  TRANSACTIONAL_TARGET INLINE transactional_lock_guard(mutex &m) : m(m)
  {
#ifndef NO_ELISION
    if (xbegin())
    {
      if (was_elided())
        return;
      xabort<0xff>();
    }
#endif
    m.lock();
  }
  transactional_lock_guard(const transactional_lock_guard &) = delete;
  TRANSACTIONAL_TARGET INLINE ~transactional_lock_guard()
  {
#ifndef NO_ELISION
    if (was_elided()) xend(); else
#endif
    m.unlock();
  }

#ifndef NO_ELISION
  bool was_elided() const noexcept { return !m.is_locked_or_waiting(); }
#else
  bool was_elided() const noexcept { return false; }
#endif
};

template<class mutex>
class transactional_shared_lock_guard
{
  mutex &m;
#ifndef NO_ELISION
  bool elided;
#else
  static constexpr bool elided = false;
#endif

public:
  TRANSACTIONAL_TARGET INLINE transactional_shared_lock_guard(mutex &m) : m(m)
  {
#ifndef NO_ELISION
    if (xbegin())
    {
      if (!m.is_locked())
      {
        elided = true;
        return;
      }
      xabort<0xff>();
    }
    elided = false;
#endif
    m.lock_shared();
  }
  transactional_shared_lock_guard(const transactional_shared_lock_guard &) =
    delete;
  TRANSACTIONAL_TARGET INLINE ~transactional_shared_lock_guard()
  {
#ifndef NO_ELISION
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
  TRANSACTIONAL_TARGET INLINE transactional_update_lock_guard(mutex &m) : m(m)
  {
#ifndef NO_ELISION
    if (xbegin())
    {
      if (was_elided())
        return;
      xabort<0xff>();
    }
#endif
    m.lock_update();
  }
  transactional_update_lock_guard(const transactional_update_lock_guard &) =
    delete;
  TRANSACTIONAL_TARGET INLINE ~transactional_update_lock_guard()
  {
#ifndef NO_ELISION
    if (was_elided()) xend(); else
#endif
    m.unlock_update();
  }

#ifdef NO_ELISION
  bool was_elided() const noexcept { return !m.is_locked_or_waiting(); }
#else
  bool was_elided() const noexcept { return false; }
#endif
};