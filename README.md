## atomic_sync: Slim mutex and rw-lock using C++ `std::atomic`

C++11 (ISO/IEC 14882:2011) introduced `std::atomic`, which provides
clearly defined semantics for concurrent memory access. The 2020
version of the standard (C++20) extended it further with `wait()` and
`notify_one()`, which allows the implementation of blocking
operations, such as mutex acquisition.

In environments where the `futex` system call is available,
`std::atomic::wait()` and `std::atomic::notify_one()` can be
implemented by `futex`. Examples for Linux and OpenBSD are included.

This project defines the following synchronization primitives:
* `atomic_mutex`: A non-recursive mutex in 4 bytes that supports the
transfer of lock ownership (`lock()` and `unlock()` in different threads)
* `atomic_shared_mutex`: A non-recursive rw-lock or
(shared,update,exclusive) lock in 4+4 bytes that supports the transfer
of lock ownership.
* `atomic_recursive_shared_mutex`: A variant of `atomic_shared_mutex`
that supports re-entrant acquisition of U or X locks.

You can try it out as follows:
```sh
mkdir build
cd build
cmake -DCMAKE_CXX_FLAGS=-DSPINLOOP=125 ..
cmake --build .
test/test_atomic_sync
test/Debug/test_atomic_sync # Microsoft Windows
```
The output of the test program should be like this:
```
atomic_spin_mutex, atomic_spin_shared_lock, atomic_spin_recursive_shared_lock.
```
Note: `-DSPINLOOP` enables the use of a spin loop. If conflicts are
not expected to be resolved quickly, it is advisable to not use spinloops and
instead let threads immediately proceed to `wait()` inside a system call.
When compiled without that compile-time option, the output of the test program
should be like this:
```
atomic_mutex, atomic_shared_lock, atomic_recursive_shared_lock.
```

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
* GCC 11.2.0 on GNU/Linux
* clang++-12, clang++-13 using libstdc++-11-dev on Debian GNU/Linux

The implementation with C++11 `std::atomic` and `futex` is expected
to work with GCC 4.8.5 to GCC 10 on Linux on OpenBSD.
It has been tested with:
* GCC 10.2.1 on GNU/Linux
* clang++-12, clang++-13 on GNU/Linux when libstdc++-11-dev is not available
* Intel C++ Compiler based on clang++-12

The following operating systems seem to define something similar to a `futex`
system call, but we have not implemented it yet:
* FreeBSD: `_umtx_op()` (`UMTX_OP_WAIT_UINT_PRIVATE`, `UMTX_OP_WAKE_PRIVATE`)
* DragonflyBSD: `umtx_sleep()`, `umtx_wakeup()`
* Apple macOS: `__ulock_wait()`, `__ulock_wake()` (undocumented)

The following operating systems do not appear to define a `futex` system call:
* NetBSD
* IBM AIX

The C++20 `std::atomic::wait()` and `std::atomic::notify_one()` would
seem to deliver a portable `futex` interface. Unfortunately, it does
not appear to be available yet on any system that lacks the system calls.
For example, Apple XCode based on clang++-12 explicitly declares
`std::atomic::wait()` and `std::atomic::notify_one()` unavailable via
```c++
#define _LIBCPP_AVAILABILITY_SYNC __attribute__((unavailable))
```

### NUMA notes

I have tested the `atomic_mutex::wait_and_lock()` implementation on a
dual Intel Xeon E5-2630 v4 (2×10 threads each, Haswell microarchitecture)
as follows:
```sh
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-DSPINLOOP=125 ..
cmake --build .
time test/test_atomic_sync
time numactl --cpunodebind 1 --localalloc test/test_atomic_sync
```
The `numactl` command would bind the process to one NUMA node (CPU package)
in order to avoid shipping cache lines between NUMA nodes.
The smallest difference between plain and `numactl` that I achieved was
with `-DSPINLOOP=125`. For more stable times, I temporarily changed the
value of `N_ROUNDS` to 500 in the source code. The durations below are
the fastest of several attempts with GCC 11.2.0 and `N_ROUNDS = 100`.
| invocation                  | real   | user    | system  |
| ----------                  | -----: | ------: | ------: |
| plain                       | 2.369s | 49.495s |  8.493s |
| `numactl`                   | 1.459s | 20.874s |  5.296s |
| `-DSPINLOOP=125`            | 2.312s | 48.785s |  7.560s |
| `-DSPINLOOP=125`,`numactl`  | 1.472s | 21.244s |  5.068s |

The execution times without `numactl` vary a lot; a much longer run
(with a larger value of `N_ROUNDS`) is advisable for performance tests.

On the Intel Skylake microarchitecture, the `PAUSE` instruction
latency was made about 10× it was on Haswell. Later microarchitectures
reduced the latency again. That latency may affect the optimal
spinloop count, but it is only one of many factors.

September 26, 2021
Marko Mäkelä
