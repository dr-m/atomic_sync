#include "transactional_lock_guard.h"
#ifndef NO_ELISION
# if defined __powerpc64__ || defined __s390x__ || defined __s390__
# elif defined _MSC_VER && (defined _M_IX86 || defined _M_X64)
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

bool transactional_lock_guard_can_elide = can_elide();
# elif defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#  include <cpuid.h>

static bool can_elide()
{
  if (__get_cpuid_max(0, nullptr) < 7)
    return false;
  unsigned eax, ebx, ecx, edx;
  __cpuid_count(7, 0, eax, ebx, ecx, edx);
  return ebx & 1U << 11; /* Restricted Transactional Memory (RTM) */
}

bool transactional_lock_guard_can_elide = can_elide();
# endif
#endif
