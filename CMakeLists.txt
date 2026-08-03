cmake_minimum_required(VERSION 3.20)
project(nql CXX C) # c needed for llvm libedit probe

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# macos: cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix llvm)
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")

# libnql is everything except main.cpp, embedded into other languages via cgo
add_library(nqlcore STATIC
  src/lexer.cpp
  src/parser.cpp
  src/reflect.cpp
  src/sema.cpp
  src/planner.cpp
  src/interp.cpp
  src/codegen.cpp
  src/jit.cpp
  src/rt.cpp
  src/data.cpp
  src/output.cpp
  src/capi.cpp
)
set_target_properties(nqlcore PROPERTIES OUTPUT_NAME nql ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(nqlcore PUBLIC include)
target_include_directories(nqlcore SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(nqlcore PRIVATE ${LLVM_DEFINITIONS_LIST})
target_compile_options(nqlcore PRIVATE -Wall -Wextra)

# link combined libLLVM dylib instead of many static archives
# matches how homebrew llvm tools link and keeps linking from outside
# cmake simple, needed for go via cgo and pkg config
find_library(NQL_LLVM_DYLIB LLVM PATHS ${LLVM_LIBRARY_DIRS} REQUIRED)
target_link_libraries(nqlcore PUBLIC ${NQL_LLVM_DYLIB})

# nql cli, linked against libnql
add_executable(nql src/main.cpp)
target_include_directories(nql SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(nql PRIVATE ${LLVM_DEFINITIONS_LIST})
target_compile_options(nql PRIVATE -Wall -Wextra)
target_link_libraries(nql PRIVATE nqlcore)

# nql.pc lets go find libnql and llvm via cgo pkg config
# point PKG_CONFIG_PATH at this build dir
if(APPLE)
  set(NQL_CXX_RUNTIME_LIB "-lc++")
else()
  set(NQL_CXX_RUNTIME_LIB "-lstdc++")
endif()

get_filename_component(NQL_LLVM_LIB_DIR ${NQL_LLVM_DYLIB} DIRECTORY)
set(NQL_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(NQL_LIB_DIR ${CMAKE_BINARY_DIR})
set(NQL_LINK_LIBS "-L${NQL_LLVM_LIB_DIR} -Wl,-rpath,${NQL_LLVM_LIB_DIR} -lLLVM ${NQL_CXX_RUNTIME_LIB}")
configure_file(${CMAKE_SOURCE_DIR}/cmake/nql.pc.in ${CMAKE_BINARY_DIR}/nql.pc @ONLY)
