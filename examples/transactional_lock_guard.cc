#include "transactional_lock_guard.h"
#ifdef NO_ELISION
#elif defined __powerpc64__
# ifdef __linux__
#  include <sys/auxv.h>

#  ifndef PPC_FEATURE2_HTM_NOSC
#   define PPC_FEATURE2_HTM_NOSC 0x01000000
#  endif
#  ifndef PPC_FEATURE2_HTM_NO_SUSPEND
#   define PPC_FEATURE2_HTM_NO_SUSPEND 0x00080000
#  endif

#  ifndef AT_HWCAP2
#   define AT_HWCAP2 26
#  endif
# endif

static bool can_elide()
{
# ifdef __linux__
  return getauxval(AT_HWCAP2) &
    (PPC_FEATURE2_HTM_NOSC | PPC_FEATURE2_HTM_NO_SUSPEND);
# endif
}

bool have_transactional_memory = can_elide();
#elif defined __aarch64__
/* FIXME: Implement a runtime check for Transactional Memory Extension (TME) */
#elif defined _MSC_VER && (defined _M_IX86 || defined _M_X64)
# include <intrin.h>

static bool can_elide()
{
  int regs[4];
  __cpuid(regs, 0);
  if (regs[0] < 7)
    return false;
  __cpuidex(regs, 7, 0);
  return regs[1] & 1U << 11; /* Restricted Transactional Memory (RTM) */
}

bool have_transactional_memory = can_elide();
#elif defined __GNUC__ && (defined __i386__ || defined __x86_64__)
# include <cpuid.h>

static bool can_elide()
{
  if (__get_cpuid_max(0, nullptr) < 7)
    return false;
  unsigned eax, ebx, ecx, edx;
  __cpuid_count(7, 0, eax, ebx, ecx, edx);
  return ebx & 1U << 11; /* Restricted Transactional Memory (RTM) */
}

bool have_transactional_memory = can_elide();
#endif
