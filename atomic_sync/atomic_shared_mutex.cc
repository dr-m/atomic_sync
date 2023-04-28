#include "atomic_shared_mutex.h"

void atomic_shared_mutex::lock_inner_wait(unsigned lk) noexcept
{
  lk |= X;
  do {
    inner.wait(lk);
    lk = inner.load(std::memory_order_acquire);
  }
  while (lk != X);
}
