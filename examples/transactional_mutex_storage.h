#pragma once

struct transactional_mutex_storage : mutex_storage<>
{
  bool is_locked_or_waiting() const noexcept
  { return this->load(std::memory_order_acquire) != 0; }
  bool is_locked() const noexcept
  { return this->load(std::memory_order_acquire) & this->HOLDER; }
};

struct transactional_shared_mutex_storage : shared_mutex_storage<>
{
  bool is_locked() const noexcept
  { return this->load(std::memory_order_acquire) == this->HOLDER; }
  bool is_locked_or_waiting() const noexcept
  { return ex.load(std::memory_order_acquire) || this->is_locked(); }
};
