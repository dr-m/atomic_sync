#include "atomic_shared_mutex.h"

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

#if !defined _WIN32 && __cplusplus < 202002L /* Emulate the C++20 primitives */
template<typename T>
void shared_mutex_storage<T>::shared_unlock_inner_notify() noexcept
{FUTEX(WAKE, &inner, 1);}
template
void shared_mutex_storage<uint32_t>::shared_unlock_inner_notify() noexcept;
#endif
