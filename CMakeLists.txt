CMAKE_MINIMUM_REQUIRED (VERSION 3.1)

PROJECT (atomic_sync)

IF (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND
    CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14.0)
  #clang++-12 and clang++-13 do not support std::atomic::notify_one()
  #despite allowing -std=c++20
  SET (CMAKE_CXX_STANDARD 11)
ELSE()
  SET (CMAKE_CXX_STANDARD 20)
ENDIF()

ADD_SUBDIRECTORY (atomic_sync)
ADD_SUBDIRECTORY (test)
