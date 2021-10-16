ADD_EXECUTABLE (test_atomic_sync test_atomic_sync.cc)
FIND_PACKAGE (Threads)

TARGET_LINK_LIBRARIES (test_atomic_sync LINK_PUBLIC
  atomic_mutex atomic_shared_mutex
  atomic_recursive_shared_mutex
  ${ELISION_LIBRARY}
  Threads::Threads)
