CMAKE_MINIMUM_REQUIRED (VERSION 3.12)

PROJECT (atomic_sync
  LANGUAGES CXX
  DESCRIPTION "memory transaction friendly std::atomic based locks")

SET (CMAKE_CXX_STANDARD 20)

ADD_SUBDIRECTORY (atomic_sync)
ADD_SUBDIRECTORY (examples)
ADD_SUBDIRECTORY (test)
