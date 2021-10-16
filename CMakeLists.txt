CMAKE_MINIMUM_REQUIRED (VERSION 3.1)

PROJECT (atomic_sync)

SET (CMAKE_CXX_STANDARD 20)

IF (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # On Debian GNU/Linux, Clang only supports std::atomic::wait()
  # if it is defined in the GCC libstdc++ headers, that is,
  # if libstdc++-11-dev or g++-11 or a later version is installed.
  INCLUDE (CMakePushCheckState)
  INCLUDE (CheckCXXSourceCompiles)
  CMAKE_PUSH_CHECK_STATE(RESET)
  IF (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10.0)
    SET(CMAKE_REQUIRED_FLAGS "-std=c++2a")
  ELSE()
    SET(CMAKE_REQUIRED_FLAGS "-std=c++20")
  ENDIF()
  CHECK_CXX_SOURCE_COMPILES("
    #include <atomic>
    std::atomic<int> a; int main() { a.wait(0); return 0; }"
    HAVE_ATOMIC_WAIT)
  CMAKE_POP_CHECK_STATE()
  IF (NOT HAVE_ATOMIC_WAIT)
    SET (CMAKE_CXX_STANDARD 11)
  ENDIF()
ELSE()
ENDIF()

ADD_SUBDIRECTORY (atomic_sync)
ADD_SUBDIRECTORY (examples)
ADD_SUBDIRECTORY (test)
