#include "transactional_lock_guard.h"
#ifndef NO_ELISION
# ifdef _MSC_VER
#  include <intrin.h>

static bool can_elide()
{
  int regs[4];
  __cpuid(regs, 0);
  if (regs[0] < 7)
    return false;
  __cpuidex(regs, 7, 0);
  return regs[1] & 1U << 11; /* Restricted Transactional Memory (RTM) */
}

# elif defined __i386__||defined __x86_64__
#  include <cpuid.h>

static bool can_elide()
{
  if (__get_cpuid_max(0, nullptr) < 7)
    return false;
  unsigned eax, ebx, ecx, edx;
  __cpuid_count(7, 0, eax, ebx, ecx, edx);
  return ebx & 1U << 11; /* Restricted Transactional Memory (RTM) */
}
# endif

bool transactional_lock_guard_can_elide = can_elide();
#endif
