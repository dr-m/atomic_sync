ADD_LIBRARY (atomic_mutex atomic_mutex.cc)
TARGET_INCLUDE_DIRECTORIES (atomic_mutex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

FIND_PACKAGE (Threads)

ADD_LIBRARY (atomic_shared_mutex atomic_shared_mutex.cc)
TARGET_INCLUDE_DIRECTORIES (atomic_shared_mutex PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})
TARGET_LINK_LIBRARIES (atomic_shared_mutex LINK_PUBLIC
  atomic_mutex Threads::Threads)

TARGET_COMPILE_FEATURES (atomic_mutex PUBLIC cxx_std_11)
TARGET_COMPILE_FEATURES (atomic_shared_mutex PUBLIC cxx_std_11)
