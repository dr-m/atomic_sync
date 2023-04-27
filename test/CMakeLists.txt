ADD_EXECUTABLE (test_atomic_sync test_atomic_sync.cc)
ADD_EXECUTABLE (test_atomic_condition test_atomic_condition.cc)
ADD_EXECUTABLE (test_mutex test_mutex.cc)
FIND_PACKAGE (Threads)

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
