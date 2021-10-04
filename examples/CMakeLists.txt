ADD_LIBRARY (atomic_recursive_shared_mutex INTERFACE)
TARGET_INCLUDE_DIRECTORIES (atomic_recursive_shared_mutex
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

ADD_LIBRARY (transactional_lock_guard transactional_lock_guard.cc)
TARGET_INCLUDE_DIRECTORIES (transactional_lock_guard
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
