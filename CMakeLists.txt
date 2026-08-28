cmake_minimum_required(VERSION 3.20)
project(PreyHeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "PreyHeadTracking targets Windows only.")
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Strip default /EHsc; we use /EHa per-target for SEH + C++ in the camera detour.
if(MSVC)
    foreach(flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        string(REGEX REPLACE "/EHsc" "" ${flag_var} "${${flag_var}}")
        string(REGEX REPLACE "/EHs" "" ${flag_var} "${${flag_var}}")
    endforeach()
endif()

set(PREY_CAMERAUNLOCK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp"
    CACHE PATH "Path to cameraunlock-core/cpp")
set(PREY_MINHOOK_DIR      "${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook"
    CACHE PATH "Path to minhook source tree")

if(NOT EXISTS "${PREY_CAMERAUNLOCK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "cameraunlock-core not found at ${PREY_CAMERAUNLOCK_DIR}. "
        "Run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${PREY_MINHOOK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "MinHook not found at ${PREY_MINHOOK_DIR}. "
        "Run: git submodule update --init --recursive")
endif()

add_subdirectory(${PREY_MINHOOK_DIR} ${CMAKE_BINARY_DIR}/_deps/minhook EXCLUDE_FROM_ALL)
if(NOT TARGET minhook AND TARGET MinHook)
    add_library(minhook ALIAS MinHook)
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON  CACHE BOOL "" FORCE)
add_subdirectory(${PREY_CAMERAUNLOCK_DIR} ${CMAKE_BINARY_DIR}/_deps/cameraunlock EXCLUDE_FROM_ALL)

add_subdirectory(src/PreyHeadTracking)

option(PREY_BUILD_TESTS "Build the pure-math unit tests" ON)
if(PREY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
