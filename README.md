# Logana

A privacy-first, local-storage messaging platform built with HFT-grade C++ engineering. Messages are end-to-end encrypted, all history lives on-device, and the relay server is a stateless pass-through that never stores or reads your data.

At the end of development, Logana will be what Discord could have been if it respected you: communities, tags, bots, and customization, minus the data harvesting and AI training on your messages.

## Building

Requires CMake 3.16+ and a C++20 compiler.

```cmake
cmake -B build
cmake --build build
```

Or use the convenience scripts:

```sh
./scripts/build.sh          # default build
./scripts/build.sh asan     # address sanitizer
./scripts/build.sh tsan     # thread sanitizer
./scripts/build.sh ubsan    # undefined behavior sanitizer
```

## Testing

```sh
./scripts/test.sh           # build and run tests
./scripts/test.sh tsan      # build and run with thread sanitizer
```

Or manually:

```cmake
cmake -B build -DSANITIZER=none
cmake --build build
ctest --test-dir build --output-on-failure
```

## Project Structure

```dir
logana/
├── core/                   # Engine library (header-only for now)
│   └── include/logana/
│       ├── types.hpp       # Message types, flags, headers
│       └── chunked_deque.hpp   # Lock-free SPSC chunked deque
├── test/                   # Test suite
├── design/                 # Design documents per commit
└── scripts/                # Build convenience scripts
```

## Design

See `design/` for detailed design documents explaining architectural decisions, tradeoffs, and rationale. Each commit has a corresponding design document.

## License

TBD.
