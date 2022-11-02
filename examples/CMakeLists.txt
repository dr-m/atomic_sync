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
  IF(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc64|powerpc64|s390x")
    SET_SOURCE_FILES_PROPERTIES(
      transactional_lock_guard.cc PROPERTIES COMPILE_FLAGS "-mhtm"
    )
  ENDIF()
ELSE()
  SET (ELISION_LIBRARY "" PARENT_SCOPE)
ENDIF()

FIND_PACKAGE (Threads)

ADD_LIBRARY (atomic_recursive_shared_mutex INTERFACE)
TARGET_INCLUDE_DIRECTORIES (atomic_recursive_shared_mutex
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
TARGET_LINK_LIBRARIES (atomic_recursive_shared_mutex INTERFACE
  atomic_mutex Threads::Threads)
