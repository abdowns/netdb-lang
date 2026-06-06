cmake_minimum_required(VERSION 3.16)
project(nql CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NQL_SOURCES
  src/lexer.cpp
  src/main.cpp
)

add_executable(nql ${NQL_SOURCES})
target_compile_options(nql PRIVATE -Wall -Wextra -Wpedantic)
