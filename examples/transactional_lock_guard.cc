#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#include <cpuid.h>

static bool can_elide()
{
  if (__get_cpuid_max(0, nullptr) < 7)
    return false;
  unsigned eax, ebx, ecx, edx;
  __cpuid_count(7, 0, eax, ebx, ecx, edx);
  return ebx & 1U << 11; /* Restricted Transactional Memory (RTM) */
}

bool transactional_lock_guard_can_elide = can_elide();
#endif
