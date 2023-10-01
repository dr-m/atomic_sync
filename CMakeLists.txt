CMAKE_MINIMUM_REQUIRED (VERSION 3.1...3.5)

PROJECT (atomic_sync
  LANGUAGES CXX
  DESCRIPTION "memory transaction friendly std::atomic based locks")

SET (CMAKE_CXX_STANDARD 11)

ADD_SUBDIRECTORY (atomic_sync)
ADD_SUBDIRECTORY (examples)
ADD_SUBDIRECTORY (test)
