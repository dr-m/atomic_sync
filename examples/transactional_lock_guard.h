#pragma once

#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#else
# define NO_ELISION /* skip Hardware Lock Elision */
#endif

#ifndef NO_ELISION
extern bool transactional_lock_guard_can_elide;

static inline bool xbegin()
{
  if (!transactional_lock_guard_can_elide)
    return false;
  unsigned ret = 0;
  __asm__ __volatile__ ("xbegin .+6" : "+a" (ret) :: "memory");
  return ret == ~0U;
}

template<uint8_t i>
static inline void xabort()
{
  __asm__ __volatile__ ("xabort %0" :: "K" (i) : "memory");
}

static inline void xend() { __asm__ __volatile__ ("xend" ::: "memory"); }
#endif

template<class mutex>
class transactional_lock_guard
{
  mutex &m;

public:
  transactional_lock_guard(mutex &m) : m(m)
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
  ~transactional_lock_guard()
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
  transactional_shared_lock_guard(mutex &m) : m(m)
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
  ~transactional_shared_lock_guard()
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
  transactional_update_lock_guard(mutex &m) : m(m)
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
  ~transactional_update_lock_guard()
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
