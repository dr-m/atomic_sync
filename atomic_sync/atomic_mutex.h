#pragma once
#include <atomic>

typedef std::atomic<unsigned> atomic_unsigned_lock_free;

class atomic_mutex
{
  atomic_unsigned_lock_free a{0};
public:
  bool is_locked() const { return a; }
  bool is_locked_or_waiting() const { return a; }
  bool try_lock() { return !a.exchange(1); }
  void lock() { while(!try_lock()) a.wait(1); }
  void unlock() { a = 0; a.notify_one(); }
};
