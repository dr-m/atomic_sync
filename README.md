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
cmake ..
cmake --build .
test/test_atomic_sync
test/test/atomic_condition
test/test_mutex 4 10000
# Microsoft Windows:
test/Debug/test_atomic_sync
test/Debug/test_atomic_condition
test/Debug/test_mutex 4 10000
```
The output of the `test_atomic_sync` program should be like this:
```
atomic_mutex, atomic_shared_mutex, atomic_recursive_shared_mutex.
```

### Source code layout

The subdirectory `atomic_sync` is the core of this, containing the
implementation of `atomic_mutex` and `atomic_shared_mutex`.

The subdirectory `examples` contains some additional code that
demonstrates how `atomic_mutex` or `atomic_shared_mutex` can be used.

The subdirectory `test` contains test programs.

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

### Comparison with `std::mutex`

The program `test_mutex` compares the performance of `atomic_mutex`
with `std::mutex`. It expects two parameters: the number of threads,
and the number of iterations within each thread.  The relative
performance of the implementations may vary with the number of
concurrent threads.
