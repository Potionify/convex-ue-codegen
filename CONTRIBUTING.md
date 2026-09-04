# Contributing

Thanks for your interest in contributing to convex-ue-codegen.

convex-ue-codegen is a community code generator for [Convex](https://convex.dev)
and Unreal Engine, maintained by Potionify. It is not an official Convex
product. For questions about the Convex platform itself, the
[Convex Discord Community](https://convex.dev/community) is the right place.

## Questions and feature requests

Please open a GitHub issue on this repository.

## Building and testing

Requirements: CMake 3.21 or newer, a C++20 compiler (MSVC 2022, clang, gcc),
and a checkout of [convex-cpp](https://github.com/Potionify/convex-cpp) next
to this repository (or `-DCONVEX_CPP_DIR=<path>`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The web app has its own suite. `npm test` in `web/` checks that the
committed WebAssembly build produces the same bytes as the C++ goldens, and
`npm run build` type-checks and bundles it. CI runs both on every pull
request.

## Pull requests

Community PRs are welcome. A few things to be aware of:

- Small, focused PRs (bug fixes, documentation, test coverage) are the
  easiest to review and integrate.
- For anything larger, open an issue first to check the direction before you
  put in too much work.
- Output is deterministic and covered by byte-exact golden tests. A change
  to the emitter has to regenerate `tests/expected/` (see the README) and
  the diff of the goldens is the first thing a reviewer reads.
- A core change also needs the WebAssembly build refreshed with
  `wasm/build-wasm.bat`, or the parity test fails. If you cannot install
  Emscripten, say so in the PR and a maintainer will rebuild it.
- New validator or type mappings should come with a unit test.
- There is no enforced formatter. Match the style of the surrounding code.
