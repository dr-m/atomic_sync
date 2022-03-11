#ifndef WITH_ELISION
# error /* This module is for the runtime detection of transactional memory */
#endif

#include "transactional_lock_guard.h"
#if defined __powerpc64__ || defined __s390__
# include <htmxlintrin.h>
# include <setjmp.h>
# include <signal.h>
/**
  SIGILL based detection based on openssl source
  https://github.com/openssl/openssl/blob/1c0eede9827b0962f1d752fa4ab5d436fa039da4/crypto/s390xcap.c#L104
*/
static sigjmp_buf ill_jmp;
static void ill_handler(int sig)
{
  siglongjmp(ill_jmp, sig);
}
/**
  Here we are testing we can do a transaction without SIGILL
  and a 1 instruction store can succeed.
*/
__attribute__((noinline))
static void test_tm(bool *r)
{
  if (__TM_simple_begin() == _HTM_TBEGIN_STARTED)
  {
    *r= true;
    __TM_end();
  }
}

static bool can_elide()
{
  bool r= false;
  sigset_t oset;
  struct sigaction ill_act, oact_ill;

  memset(&ill_act, 0, sizeof(ill_act));
  ill_act.sa_handler = ill_handler;
  sigfillset(&ill_act.sa_mask);
  sigdelset(&ill_act.sa_mask, SIGILL);

  sigprocmask(SIG_SETMASK, &ill_act.sa_mask, &oset);
  sigaction(SIGILL, &ill_act, &oact_ill);
  if (sigsetjmp(ill_jmp, 1) == 0)
  {
    test_tm(&r);
  }
  sigaction(SIGILL, &oact_ill, NULL);
  sigprocmask(SIG_SETMASK, &oset, NULL);
  return r;
}

bool have_transactional_memory = can_elide();

__attribute__((target("hot","htm")))
bool xbegin()
{
  return have_transactional_memory &&
    __TM_simple_begin() == _HTM_TBEGIN_STARTED;
}

__attribute__((target("hot","htm")))
void xabort() { __TM_abort(); }

__attribute__((target("hot","htm")))
void xend() { __TM_end(); }

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
