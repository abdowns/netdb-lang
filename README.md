# nql

A small JIT-compiled query language for filtering and querying structured records
(packets, logs, table rows). Predicates are compiled to native code with
LLVM at runtime. Implemented in C++ and LLVM.

Examples are in the examples/ directory (packets.nql, weblog.nql).

## Building

Requires CMake 3.20+ and LLVM.

```sh
brew install llvm cmake ninja
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix llvm)
cmake --build build
```

## Running

```sh
./build/nql run examples/packets.nql --n 200000
./build/nql run examples/weblog.nql --data examples/data/weblog.csv
```