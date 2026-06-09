cmake_minimum_required(VERSION 3.16)
project(nql CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_executable(nql
  src/lexer.cpp
  src/parser.cpp
  src/reflect.cpp
  src/sema.cpp
  src/main.cpp
)

target_compile_options(nql PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
