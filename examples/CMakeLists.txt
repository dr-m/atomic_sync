ADD_LIBRARY (atomic_recursive_shared_mutex INTERFACE)
TARGET_INCLUDE_DIRECTORIES (atomic_recursive_shared_mutex
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

ADD_LIBRARY (atomic_condition_variable INTERFACE)
TARGET_INCLUDE_DIRECTORIES (atomic_condition_variable
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

OPTION (WITH_ELISION "Implement lock elision with memory transactions" OFF)
IF (WITH_ELISION)
  SET (ELISION_LIBRARY transactional_lock_guard PARENT_SCOPE)
  ADD_LIBRARY (transactional_lock_guard transactional_lock_guard.cc)
  TARGET_COMPILE_DEFINITIONS (transactional_lock_guard PUBLIC -DWITH_ELISION)
  TARGET_INCLUDE_DIRECTORIES (transactional_lock_guard
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
ELSE()
  SET (ELISION_LIBRARY "" PARENT_SCOPE)
ENDIF()
