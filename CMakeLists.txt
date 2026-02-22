cmake_minimum_required(VERSION 3.16)
project(BFCompiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(common)
add_subdirectory(interpreter)
add_subdirectory(transpiler)
add_subdirectory(compiler)
