cmake_minimum_required(VERSION 3.16)
project(Logana LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SANITIZER "none" CACHE STRING "Sanitizer to enable")

enable_testing()

add_subdirectory(core)
add_subdirectory(test)