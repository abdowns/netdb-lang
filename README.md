# nql

A small JIT-compiled query language for filtering and querying structured records
(packets, logs, table rows). Predicates are compiled to native code with
LLVM at runtime. Implemented in C++ and LLVM.

Examples are in the examples/ directory (packets.nql, weblog.nql).

Build with CMake:

    cmake -B build && cmake --build build
    ./build/nql run examples/weblog.nql --data examples/data/weblog.csv
