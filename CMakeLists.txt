cmake_minimum_required(VERSION 3.20)

project(PclCeHookLauncher VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook")
if(NOT EXISTS "${MINHOOK_DIR}/include/MinHook.h")
    message(FATAL_ERROR "MinHook sources missing at ${MINHOOK_DIR}.")
endif()

# MinHook 上游 CMakeLists 声明 cmake_minimum_required(3.0...3.5)，CMake 4.x 拒绝该策略范围，
# 因此这里只编译其源码，而不 add_subdirectory。
add_library(minhook STATIC
    "${MINHOOK_DIR}/src/buffer.c"
    "${MINHOOK_DIR}/src/hook.c"
    "${MINHOOK_DIR}/src/trampoline.c"
)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_sources(minhook PRIVATE "${MINHOOK_DIR}/src/hde/hde64.c")
else()
    target_sources(minhook PRIVATE "${MINHOOK_DIR}/src/hde/hde32.c")
endif()
target_include_directories(minhook
    PUBLIC "${MINHOOK_DIR}/include"
    PRIVATE "${MINHOOK_DIR}/src" "${MINHOOK_DIR}/src/hde"
)

add_library(PCLCEHook SHARED
    src/hook/dllmain.cpp
    src/hook/hooks.cpp
    src/common/log.cpp
)
target_compile_definitions(PCLCEHook PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(PCLCEHook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(PCLCEHook PRIVATE minhook)
set_target_properties(PCLCEHook PROPERTIES PREFIX "" OUTPUT_NAME "PCLCEHook")

add_executable(launcher
    src/launcher/main.cpp
    src/launcher/inject.cpp
    src/launcher/target.cpp
    src/common/log.cpp
)
target_compile_definitions(launcher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(launcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(launcher PROPERTIES PREFIX "")

if(MSVC)
    target_compile_options(PCLCEHook PRIVATE /utf-8 /W4 /permissive-)
    target_compile_options(launcher PRIVATE /utf-8 /W4 /permissive-)
    target_compile_options(minhook PRIVATE /W3)
    target_compile_options(PCLCEHook PRIVATE /EHsc)
    target_compile_options(launcher PRIVATE /EHsc)
else()
    target_compile_options(PCLCEHook PRIVATE -Wall -Wextra)
    target_compile_options(launcher PRIVATE -Wall -Wextra)
endif()

# DLL 与启动器必须相邻，启动器默认按自身目录解析 hook DLL。
add_dependencies(launcher PCLCEHook)
