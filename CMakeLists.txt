cmake_minimum_required(VERSION 3.22)

# These must be set before the first project() call to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version the plugin runs on")
# Link the MSVC runtime statically so the plugin loads on machines without the
# Visual C++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime library")

project(Pressto VERSION 0.2.0 LANGUAGES C CXX)

enable_testing()

add_subdirectory(pressto)
