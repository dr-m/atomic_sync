## atomic_sync: Slim `mutex` and `shared_mutex` using C++ `std::atomic`

C++11 (ISO/IEC 14882:2011) introduced `std::atomic`, which provides
clearly defined semantics for concurrent memory access. The 2020
version of the standard (C++20) extended it further with `wait()` and
`notify_one()`, which allows the implementation of blocking
operations, such as lock acquisition.

In environments where a system call like the Linux `futex` is available,
`std::atomic::wait()` and `std::atomic::notify_one()` can be implemented by it.

Our alternatives to the standard mutexes are non-recursive and support
the transfer of lock ownership (`unlock()` in a different thread than
`lock()`):
* `atomic_mutex`: Compatible with `std::mutex`.
* `atomic_shared_mutex`: Extends `std::shared_mutex` with `lock_update()`,
which is compatible with `lock_shared()`. This mode can be used for
exclusively locking part of a resource while other parts can be safely
accessed by shared lock holders.

For maximal flexibility, a template parameter can be specified. We
provide an interface `mutex_storage` and a reference implementation
based on C++11 or C++20 `std::atomic` (default: 4 bytes).

Some examples of extending or using the primitives are provided:
* `atomic_condition_variable`: A condition variable in 4 bytes that
goes with (`atomic_mutex` or `atomic_shared_mutex`).
Unlike the potentially larger `std::condition_variable_any`,
this supports `wait_shared()` and `is_waiting()` (for lock elision).
* `atomic_recursive_shared_mutex`: A variant of `atomic_shared_mutex`
that supports re-entrant `lock()` and `lock_update()`.
* `transactional_lock_guard`, `transactional_shared_lock_guard`:
Similar to `std::lock_guard` and `std::shared_lock_guard`, but with
optional support for lock elision using transactional memory.

You can try it out as follows:
```sh
mkdir build
cd build
cmake -DWITH_SPINLOOP=ON ..
cmake --build .
test/test_atomic_sync
test/test_atomic_condition
test/test_mutex 4 10000
test/test_native_mutex 4 10000
# Microsoft Windows:
test/Debug/test_atomic_sync
test/Debug/test_atomic_condition
test/Debug/test_mutex 4 10000
test/Debug/test_native_mutex 4 10000
```
The output of the `test_atomic_sync` program should be like this:
```
atomic_spin_mutex, atomic_spin_shared_mutex, atomic_spin_recursive_shared_mutex.
```
Note: `-DSPINLOOP=0` (or anything else than a positive integer)
disables any use of spin loops in the implementation.

The build option `-DWITH_SPINLOOP=ON` makes `test_atomic_sync` request
that spinloops be used. If conflicts are not expected to be resolved
quickly, it is advisable to not use spinloops and instead let threads
immediately proceed to `wait()` inside a system call. When compiled
`-DWITH_SPINLOOP=OFF`, the output of the `test_atomic_sync`
should be like this:
```
atomic_mutex, atomic_shared_mutex, atomic_recursive_shared_mutex.
```
There is a static member function declaration `default_spin_rounds()`
in the `mutex_storage` and `shared_mutex_storage` classes, but it has
not been defined. The idea is that it would provide a default argument
for `spin_lock()` and friends.

This is based on my implementation of InnoDB rw-locks in
[MariaDB Server](https://github.com/MariaDB/server/) 10.6.
The main motivation of publishing this separately is:
* To help compiler writers implement `wait()` and `notify_one()`.
* To eventually include kind of synchronization primitives in
a future version of the C++ standard library.
* To provide space efficient synchronization primitives, for example
to implement a hash table with one mutex per cache line
(such as the `lock_sys_t::hash_table` in MariaDB Server 10.6).

The implementation with C++20 `std::atomic` has been tested with:
* Microsoft Visual Studio 2019
* GCC 11.2.0, GCC 12.2.0 on GNU/Linux
* clang++-12, clang++-13 using libstdc++-11-dev on Debian GNU/Linux

The implementation with C++11 `std::atomic` and `futex` is expected
to work with GCC 4.8.5 to GCC 10 on Linux, OpenBSD, FreeBSD, DragonFly BSD.
It has been tested with:
* GCC 10.2.1 on GNU/Linux
* GCC 8.3 on DragonFly BSD
* clang 11.0.1 on FreeBSD 13.0
* clang++-12, clang++-13 on GNU/Linux when libstdc++-11-dev is not available
* Intel C++ Compiler based on clang++-12

The following operating systems seem to define something similar to a `futex`
system call, but we have not implemented it yet:
* Apple macOS: `__ulock_wait()`, `__ulock_wake()` (undocumented)

The following operating systems do not appear to define a `futex` system call:
* NetBSD
* Solaris (Illumos)
* HP-UX
* AIX

The C++20 `std::atomic::wait()` and `std::atomic::notify_one()` would
seem to deliver a portable `futex` interface. Unfortunately, it does
not appear to be available yet on any system that lacks the system calls.
For example, Apple XCode based on clang++-12 explicitly declares
`std::atomic::wait()` and `std::atomic::notify_one()` unavailable via
```c++
#define _LIBCPP_AVAILABILITY_SYNC __attribute__((unavailable))
```

### Source code layout

The subdirectory `atomic_sync` is the core of this, containing the
implementation of `atomic_mutex` and `atomic_shared_mutex`.

The subdirectory `examples` contains some additional code that
demonstrates how `atomic_mutex` or `atomic_shared_mutex` can be used.

The subdirectory `test` contains test programs.

### ThreadSanitizer notes

The clang and GCC option `-fsanitize=thread` currently requires some
extra instrumentation in order to recognize that `atomic_mutex` and
`atomic_shared_mutex` actually implement locks. This instrumentation
could also offer the benefit of catching lock-order-inversion
(potential deadlock).

The `atomic_shared_mutex` is instrumented as two locks, corresponding
to its internal implementation. An exclusive `lock()` internally
acquires a lock on both the data members `outer` and `inner`.
A `lock_shared()` operates on the `inner` lock. If the `mutex_storage`
is write-locked, then `lock_shared()` will momentarily acquire and
release `outer` in order to wait for `unlock()`. The third lock mode
`lock_update()` locks `outer` and acquires a shared lock on `inner`.

For `atomic_shared_mutex`, the ThreadSanitizer instrumentation
includes a flag `__tsan_mutex_try_read_lock` that is not available in
GCC 11.

The instrumentation depends on some features that are not available in
C++11. It has been tested with clang++-14, clang++-15, and GCC 12. The
program `test_mutex`, which does not use `atomic_shared_mutex`, has
been tested with GCC 11 `-fsanitize=thread`.

The program `test_native_mutex` demonstrates how a user-defined
`mutex_storage` (based on POSIX `pthread_mutex_t` or Microsoft Windows
`SRWLOCK`) can be used with `atomic_mutex`. That program will report
bogus ThreadSanitizer data race warnings when built with GCC 12 for
GNU/Linux, presumably because the built-in instrumentation for
`pthread_mutex_t` interferes with the additional instrumentation in
`atomic_mutex`.

### Lock elision

The `transactional_lock_guard` is like `std::lock_guard` but designed
for supporting transactional memory around `atomic_mutex` or
`atomic_shared_mutex`. If support is not detected at compilation or
run time, it should be equivalent to `std::lock_guard`.

The `transactional_shared_lock_guard` and `transactional_update_lock_guard`
are for the two non-exclusive modes of `atomic_shared_mutex`.

Lock elision may be enabled by specifying `cmake -DWITH_ELISION=ON`.
If transactional memory is supported, `test_atomic_sync` and
`test_atomic_condition` will output the following:
```
transactional atomic_mutex, atomic_shared_mutex, atomic_recursive_shared_mutex.
condition variables with transactional atomic_mutex, atomic_shared_mutex.
```
If support for transaction memory was not detected, the output will
say `non-transactional` instead of `transactional`.

#### Intel TSX-NI, or Restricted Transactional Memory (RTM)

Intel Transactional Synchronization Extensions New Instructions (TSX-NI)
for IA-32 and AMD64 enable access to Restricted Transactional Memory (RTM)
operations. The instructions are is available on some higher-end Intel
processors that implement the IA-32 or AMD64 ISA. RTM is currently not
available on processors manufactured by AMD.

You may want to check some performance statistics.
```sh
perf record -g -e tx-abort test/test_atomic_sync
perf record -g -e tx-abort test/test_atomic_condition
```
In `test_atomic_sync`, lock elision is detrimental for performance, because
lock conflicts make transaction aborts and re-execution extremely common.

The elision is very simple, not even implementing any retry mechanism.
If the lock cannot be elided on the first attempt, we will fall back
to acquiring the lock.

#### POWER v2.07 Hardware Transactional Memory (HTM)

This has been successfully tested on Debian GNU/Linux Buster with GCC
7.5.0.

Inside QEMU virtual machines that I tested, the availability was not
detected and an attempt to override the runtime detection (which is
only implemented for Linux) caused the `tbegin.` instruction to throw
`SIGILL` (illegal instruction).

When compiling the code with earlier GCC than version 5, you will have
to specify `-DCMAKE_CXX_FLAGS=-mhtm` or equivalent.

#### ARMv8 Transactional Memory Extension (TME)

No interface for run-time detection appears to have been published
yet. Only an inline assembler interface appears to be available,
starting with GCC 10 and clang 10. It is not known yet which
implementations of ARMv8 or ARMv9 would support this.

### NUMA notes

I have tested the `atomic_mutex::wait_and_lock()` implementation on a
dual Intel Xeon E5-2630 v4 (2×10 threads each, Haswell microarchitecture)
as follows:
```sh
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++-13 ..
cmake --build .
time test/test_atomic_sync
time numactl --cpunodebind 1 --localalloc test/test_atomic_sync
```
The `numactl` command would bind the process to one NUMA node (CPU package)
in order to avoid shipping cache lines between NUMA nodes.
The smallest difference between plain and `numactl` that I achieved
at one point was with `-DSPINLOOP=50` (which is the default).
For more stable times, I temporarily changed the
value of `N_ROUNDS` to 500 in the source code. The durations below are
the fastest of several attempts with clang++-13 and `N_ROUNDS = 100`.
| build                 | invocation | real   | user    | system  |
| --------------------- | ---------- | -----: | ------: | ------: |
| `-DWITH_SPINLOOP=OFF` | plain      | 1.763s | 40.973s |  5.382s |
| `-DWITH_SPINLOOP=OFF` | `numactl`  | 1.169s | 15.563s |  4.060s |
| `-DWITH_SPINLOOP=ON`  | plain      | 1.798s | 42.000s |  5.191s |
| `-DWITH_SPINLOOP=ON`  | `numactl`  | 1.168s | 15.810s |  4.089s |

The execution times without `numactl` vary a lot; a much longer run
(with a larger value of `N_ROUNDS`) is advisable for performance tests.

On the Intel Skylake microarchitecture, the `PAUSE` instruction
latency was made about 10× it was on Haswell. Later microarchitectures
reduced the latency again. That latency may affect the optimal
spinloop count, but it is only one of many factors.

### Comparison with `std::mutex`

The program `test_mutex` compares the performance of `atomic_mutex`
and `atomic_spin_mutex` with `std::mutex`. It expects two parameters:
the number of threads, and the number of iterations within each
thread.  The relative performance of the implementations may vary with
the number of concurrent threads. On a system with 4 execution cores,
the output of a `CMAKE_BUILD_TYPE=RelWithDebInfo` build that was
invoked as `test_mutex 4 100000` might look something like this:
```
atomic_mutex: 0.036838s, atomic_spin_mutex: 0.059827s, mutex: 0.073922s
```
On a Raspberry Pi 2 (4 execution cores that implement the ARMv7 ISA),
the output of `test_mutex 8 100000` nicely illustrates the usefulness of
spinloops:
```
atomic_mutex: 0.411457s, atomic_spin_mutex: 0.218578s, mutex: 0.435057s
```
