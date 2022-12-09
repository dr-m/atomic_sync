#pragma once

#ifndef __has_feature
# define __has_feature(x) 0
#endif
#if __has_feature(thread_sanitizer)
# define __SANITIZE_THREAD__ 1
#endif

#ifdef __SANITIZE_THREAD__
# include <sanitizer/tsan_interface.h>
#else
# define __tsan_mutex_pre_lock(addr, flags) while (false)
# define __tsan_mutex_post_lock(addr, flags, recursion) while (false)
# define __tsan_mutex_pre_unlock(addr, flags) while (false)
# define __tsan_mutex_post_unlock(addr, flags) while (false)
# define __tsan_mutex_pre_signal(addr, flags) while (false)
# define __tsan_mutex_post_signal(addr, flags) while (false)
#endif
