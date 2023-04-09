ADD_EXECUTABLE (test_atomic_sync test_atomic_sync.cc)
ADD_EXECUTABLE (test_atomic_condition test_atomic_condition.cc)
ADD_EXECUTABLE (test_mutex test_mutex.cc)
ADD_EXECUTABLE (test_native_mutex test_native_mutex.cc)
FIND_PACKAGE (Threads)

OPTION (WITH_SPINLOOP "Test atomic_spin_mutex, atomic_spin_shared_mutex." OFF)
IF (WITH_SPINLOOP)
  TARGET_COMPILE_DEFINITIONS(test_atomic_sync PRIVATE WITH_SPINLOOP)
  TARGET_COMPILE_DEFINITIONS(test_mutex PRIVATE WITH_SPINLOOP)
ENDIF ()

SET (SPINLOOP 50 CACHE STRING "Spinloop count (0 to disable)")
IF (${SPINLOOP} GREATER 0)
  TARGET_COMPILE_DEFINITIONS(test_atomic_sync PRIVATE SPINLOOP=${SPINLOOP})
  TARGET_COMPILE_DEFINITIONS(test_mutex PRIVATE SPINLOOP=${SPINLOOP})
ENDIF()

TARGET_LINK_LIBRARIES (test_atomic_sync LINK_PUBLIC
  atomic_mutex
  atomic_recursive_shared_mutex
  ${ELISION_LIBRARY}
  Threads::Threads)

TARGET_LINK_LIBRARIES (test_atomic_condition LINK_PUBLIC
  atomic_mutex
  atomic_condition_variable
  ${ELISION_LIBRARY}
  Threads::Threads)

TARGET_LINK_LIBRARIES (test_mutex LINK_PUBLIC atomic_mutex Threads::Threads)
TARGET_LINK_LIBRARIES (test_native_mutex LINK_PUBLIC atomic_mutex Threads::Threads)
