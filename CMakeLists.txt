cmake_minimum_required(VERSION 3.20)
project(nql CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# macos: cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix llvm)
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")

add_executable(nql
  src/lexer.cpp
  src/parser.cpp
  src/reflect.cpp
  src/sema.cpp
  src/interp.cpp
  src/codegen.cpp
  src/jit.cpp
  src/rt.cpp
  src/data.cpp
  src/output.cpp
  src/main.cpp
)

target_include_directories(nql SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(nql PRIVATE ${LLVM_DEFINITIONS_LIST})
target_compile_options(nql PRIVATE -Wall -Wextra)

llvm_map_components_to_libnames(llvm_libs core support orcjit native)
target_link_libraries(nql PRIVATE ${llvm_libs})
