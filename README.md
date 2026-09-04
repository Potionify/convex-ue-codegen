# convex-ue-codegen

[![CI](https://github.com/Potionify/convex-ue-codegen/actions/workflows/ci.yml/badge.svg)](https://github.com/Potionify/convex-ue-codegen/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/Potionify/convex-ue-codegen)](LICENSE)

A community code generator for [Convex](https://convex.dev) and
[Unreal Engine](https://www.unrealengine.com), maintained by Potionify. Not an
official Convex product. It fetches a deployment's function specification and
emits typed wrappers for every deployed function: native C++,
Blueprint-callable, and optionally AngelScript for the
[Hazelight UnrealEngine-Angelscript](https://angelscript.hazelight.se) fork.
The generated code calls into the [convex-ue](https://github.com/Potionify/convex-ue)
plugin's `ConvexClient` runtime.

It comes in three forms that share one emission core and produce
byte-identical output: a console tool built on
[convex-cpp](https://github.com/Potionify/convex-cpp), a web app at
[ue-codegen.potionify.com](https://ue-codegen.potionify.com) that runs the
same core compiled to WebAssembly, and a copy vendored inside convex-ue
behind its Generate API button. This repository holds the first two.

## What it generates

Given a deployment (or an offline spec file), it writes four files (default
prefix `ConvexApi`, overridable with `--prefix`):

| File | Contents |
|---|---|
| `<Prefix>.h` / `.cpp` | Native C++ wrappers, one function per Convex function, grouped into nested namespaces mirroring the module path (`admin/tools` becomes `ConvexApi::Admin::Tools`). Queries get a one-shot `Get(...)` and a `WatchGet(...)` subscription form. Mutations and actions get the one-shot form. |
| `<Prefix>BP.h` / `.cpp` | `UBlueprintFunctionLibrary` subclasses (one per module) of `BlueprintCallable` `UFUNCTION`s, so every function is a node in the Blueprint graph. |

With `--emit-module <Name>` it also writes `<Name>.Build.cs` and
`<Name>Module.cpp`, so the output folder drops into a UE project as a complete
module depending on `ConvexClient` and `ConvexCore`.

With `--script-out <dir>` it also writes `<Prefix>.as`, AngelScript wrappers
for the Hazelight fork, into `<dir>` (your project's `Script/` folder). The
script wrappers need no C++ build. The fork hot-reloads them in the running
editor. See [AngelScript output](#angelscript-output).

Typed arguments come from each function's args validator. An `object`
validator becomes one C++ parameter per field: `string` is `FString`,
`number` is `double`, `int64` is `int64`, `id` is `FString` with an
`/* Id<table> */` note, `array<string>` is `TArray<FString>`, optional fields
are `TOptional<T>`, and objects, unions, records and `any` are `FConvexValue`.
A non-object args validator becomes a single pass-through
`TMap<FString, FConvexValue> Args`. Blueprint wrappers omit optional args
(noted in each node's tooltip); use the native wrapper for those.

Output is deterministic. Identical input always yields byte-identical output
(sorted modules and functions, no timestamps), which the golden tests rely on.

## Building

Requirements: CMake 3.21 or newer, a C++20 compiler (MSVC 2022, clang, gcc),
and a checkout of [convex-cpp](https://github.com/Potionify/convex-cpp).

```bash
cmake -S . -B build -DCONVEX_WITH_IXWEBSOCKET=ON
cmake --build build --config Release
```

The CLI ends up at `build/cli/Release/convex-ue-codegen.exe` (multi-config) or
`build/cli/convex-ue-codegen.exe` (single-config).

| CMake option | Default | Effect |
|---|---|---|
| `CONVEX_CPP_DIR` | `../convex-cpp` | Path to the convex-cpp source tree. Configure fails with a clear message if it is missing. |
| `CONVEX_UE_CODEGEN_BUILD_TESTS` | ON (top-level) | Build the GoogleTest suite (fetched via FetchContent). |

convex-cpp is added via `add_subdirectory` and built with its bundled
IXWebSocket transport (`CONVEX_WITH_IXWEBSOCKET`). nlohmann/json is vendored
under `third_party/` as a private, implementation-only dependency of the core.
No public header includes it, which keeps the core portable to WebAssembly.

### The `.bat` launcher

`Tools/convex-ue-codegen.bat` builds the CLI on first use and then runs it,
forwarding all arguments:

```bat
Tools\convex-ue-codegen.bat --help
Tools\convex-ue-codegen.bat --out MyGame\Source\ConvexApi --emit-module ConvexApi
```

## Usage

```
convex-ue-codegen --out <dir> [--url <u>] [--deploy-key <k>] [--env-file <path>]
                  [--spec-json <file>] [--prefix ConvexApi] [--include-internal]
                  [--stamp <text>] [--emit-module <Name>] [--script-out <dir>]
```

- `--out <dir>` (required): output directory, created if missing.
- `--spec-json <file>`: offline mode. Reads the apiSpec from a file instead
  of the network. Accepts either the raw value array or the full
  `{"status","value"}` envelope.
- `--url` / `--deploy-key`: connect explicitly. Otherwise the deployment is
  resolved from the environment (below).
- `--prefix`: generated API prefix (default `ConvexApi`).
- `--include-internal`: also emit internal-visibility functions.
- `--stamp <text>`: a verbatim provenance line written as the second line of
  every file, for reproducible output. Without it, the line is
  `// Source: <deployment url or spec file>`.
- `--emit-module <Name>`: also emit the `.Build.cs` and `Module.cpp`
  scaffolding.
- `--script-out <dir>`: also emit `<Prefix>.as` (AngelScript wrappers) into
  `<dir>`. Created if missing.

Examples:

```bash
# Offline, from a saved spec:
convex-ue-codegen --spec-json api.json --out Source/ConvexApi

# Live, explicit deployment:
convex-ue-codegen --out Source/ConvexApi \
  --url https://happy-animal-123.convex.cloud --deploy-key "prod:happy-animal-123|..."

# Live, resolved from the environment / .env files:
convex-ue-codegen --out Source/ConvexApi
```

On success it prints a one-line summary (`N function(s), M file(s) -> <dir>`)
and exits 0. Any failure, whether a bad key, an unreachable backend, or an
unresolved deployment, prints an `error: ...` message to stderr and exits
non-zero.

### How it fetches

Codegen needs no WebSocket. It issues a single
`POST /api/query {"path":"_system/cli/modules:apiSpec","args":{}}` with an
`Authorization: Convex <deploy-key>` header through convex-cpp's
`http_client`, which returns every deployed function with its args and return
validators. `HttpAction` routes and `_system/*` modules are skipped.

## Deployment resolution

When `--spec-json` is not given and `--url` and `--deploy-key` are not both
passed, the deployment is resolved like the Convex CLI. For each variable,
the first of these sources that defines it wins:

1. process environment
2. `--env-file <path>` (if given)
3. `./.env.local`
4. `./convex.env.local`
5. `./.env`

Variables consulted:

- Deploy key: `CONVEX_DEPLOY_KEY` (alias `CONVEX_DEPLOYMENT_TOKEN`;
  `CONVEX_DEPLOY_KEY` wins if both are set), or `CONVEX_SELF_HOSTED_ADMIN_KEY`.
- URL: `CONVEX_SELF_HOSTED_URL`, then `CONVEX_URL`. If neither is set, the
  URL is synthesized as `https://<name>.convex.cloud`, where `<name>` is the
  text between the first `:` and the `|` of the deploy key
  (`dev:happy-animal-123|...` gives `happy-animal-123`). Self-hosted keys have
  no such name, so a URL must be provided for them.

`.env` files are parsed line by line, never shell-sourced, so admin keys
containing `|` survive verbatim.

## Output shape

```
<out>/
  ConvexApi.h        native wrappers (declarations, nested namespaces)
  ConvexApi.cpp      native wrapper bodies
  ConvexApiBP.h      Blueprint function libraries (one UCLASS per module)
  ConvexApiBP.cpp    Blueprint wrapper bodies
  <Name>.Build.cs    (only with --emit-module) module rules
  <Name>Module.cpp   (only with --emit-module) IMPLEMENT_MODULE boilerplate
```

The generated files only compile inside a UE module. They include
`ConvexClient` headers and, for the Blueprint ones, are UHT-processed. Drop
them into a module that depends on `ConvexClient` and `ConvexCore`, or use
`--emit-module` to generate that module's scaffolding too.

### AngelScript output

`--script-out <dir>` adds one more file, written to `<dir>` instead of `<out>`:

```
<script-out>/
  ConvexApi.as       AngelScript wrappers (one namespace per module)
```

It targets the [Hazelight UnrealEngine-Angelscript](https://angelscript.hazelight.se)
fork of UE, which loads scripts from the project's `Script/` folder and
hot-reloads them, so regenerating the API never needs a C++ build. The
wrappers call the Convex plugin's script-callable client methods, build
arguments with its value library, and read results through its script
mixins. The plugin must be enabled (0.2.0 or later), but no generated C++ is
required.

The file has four parts, in order:

1. Structs. Every object shape the deployed functions declare, in
   `args` or `returns`, becomes a script `struct`. Shapes are deduplicated,
   so a document returned by several functions is one struct, named after
   the first function that uses it in sorted order: `messages:list`
   returning `array<doc>` gives `FConvexApiMessagesListElement`; an object
   return gives `<Base>Result`; a nested object appends its field name. An
   optional field gets a `bHas<Field>` companion, because the fork's
   `TOptional` cannot hold containers.
2. `ConvexApi::Types`. `Decode<Name>(FConvexValue)` and
   `Encode(<struct>)` for every struct.
3. Delegates and adapters. A function with a declared return gets a
   typed delegate, `delegate void FConvexApiCountersGetDelegate(float Value,
   FConvexResult Result)`, and a small adapter class that decodes the raw
   result before executing it. The raw `FConvexResult` is always the second
   parameter, so errors and the undecoded value stay reachable.
4. Wrappers. One namespace per module.

Shape, for `counters:get` returning `number` and `counters:increment` with a
required `name`, an optional `by`, and no declared return:

```angelscript
namespace ConvexApi::Counters
{
    void Get(UConvexClient Client, FString Name, FConvexApiCountersGetDelegate OnResult);
    UConvexSubscription WatchGet(UConvexClient Client, FString Name, FConvexApiCountersGetDelegate OnUpdate);
    void Increment(UConvexClient Client, FString Name, FConvexResultDelegate OnResult);
    void Increment(UConvexClient Client, FString Name, float By, FConvexResultDelegate OnResult);
}
```

Script types follow the fork: `number` is `float` (64-bit there), `int64` is
`int64`, `bytes` is `TArray<uint8>`, declared objects are structs, and
unions, records, `any`, and nested containers are `FConvexValue`. Optional
arguments become a second overload that takes every argument. Named
arguments read well:

```angelscript
FConvexResultDelegate Handler;
Handler.BindUFunction(this, n"OnIncremented");
ConvexApi::Counters::Increment(Client, Name = "hits", By = 2.0, OnResult = Handler);
```

Paginated queries get a `Watch<Name>Paginated` wrapper returning
`UConvexPaginatedSubscription` and taking `int InitialNumItems`. When the
query declares the `PaginationResult` shape, the wrapper takes a typed page
delegate, `(TArray<Element> Results, FConvexPaginatedSnapshot Snapshot)`;
otherwise an `FConvexPaginatedSnapshotDelegate`.

Adapter lifetime is the plugin's job: the client keeps a one-shot adapter
alive until its callback fires, and a watch wrapper attaches its adapter to
the returned subscription. The `.as` file is covered by the same byte-exact
golden test as the C++ output.

## Testing

```bash
ctest --test-dir build -C Release --output-on-failure
```

The suite has unit tests (identifier canonicalization, name sanitization,
each validator-to-type mapping, env-resolution precedence, envelope versus
array acceptance), byte-exact golden tests, and a live test that runs against
the local self-hosted backend from convex-cpp's integration environment. The
live test skips itself unless `http://127.0.0.1:3210/version` responds and
`../convex-cpp/integration/local.env` exists. When the backend is up, it
fetches and emits for real.

### Regenerating golden files

The golden expected output lives in `tests/expected/` and is compared
byte-for-byte against a fixed provenance stamp
(`// Source: apispec_full.json (golden fixture)`). After an intentional change
to the emitter, regenerate it by running the test binary with
`CODEGEN_REGENERATE=1`:

```bash
CODEGEN_REGENERATE=1 build/tests/Release/convex_ue_codegen_tests.exe \
  --gtest_filter=Golden.ByteExactAgainstExpected
```

This rewrites `tests/expected/` from the current emitter, and the test then
reports `SKIPPED`. Review the diff before committing.

## Web version

[ue-codegen.potionify.com](https://ue-codegen.potionify.com) is a static
web app (Vite, React, TypeScript) that produces the same output as the CLI,
byte for byte, from the same `core/` emission library compiled to
WebAssembly. Paste a deployment URL and deploy key, or paste or upload an
apiSpec JSON, set the options, and copy the files or download a zip.

There is no server component. The deploy key is used only for the direct
`POST {url}/api/query` request from your browser to your own deployment
(`Authorization: Convex <key>`) and never leaves the browser. If CORS blocks
that fetch, run `npx convex function-spec` locally and paste the output
instead.

### Building the WASM module

The built artifacts (`web/src/generated/codegen.mjs` and `codegen.wasm`) are
committed, so the web app builds without the Emscripten SDK. To rebuild after
changing the core:

```bat
rem needs em++ on PATH (emsdk install latest / activate latest / emsdk_env.bat)
wasm\build-wasm.bat
```

or on any shell:

```bash
em++ -O2 -std=c++20 -fwasm-exceptions -Icore/include -Ithird_party \
  core/src/naming.cpp core/src/validator.cpp core/src/type_map.cpp \
  core/src/api_spec.cpp core/src/emit.cpp wasm/wrapper.cpp \
  -sMODULARIZE=1 -sEXPORT_ES6=1 \
  -sEXPORTED_FUNCTIONS=_convex_ue_codegen_generate,_convex_ue_codegen_free,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8 \
  -sALLOW_MEMORY_GROWTH=1 --no-entry -o web/src/generated/codegen.mjs
```

The C bridge is `wasm/wrapper.cpp`. `convex_ue_codegen_generate(spec_json,
options_json)` returns a malloc'd JSON string (`{"files": {...}}` or
`{"error": "..."}`) released via `convex_ue_codegen_free`. No exception ever
crosses the C boundary.

### Building and testing the web app

```bash
cd web
npm install
npm test        # golden parity: WASM output must be byte-identical to tests/expected/
npm run build   # type-check + bundle to web/dist
npm run dev     # local dev server
```

`npm test` runs `web/scripts/parity-test.mjs` in Node. It loads the WASM
module, feeds it `tests/fixtures/apispec_full.json` with the same fixed stamp
the C++ golden tests use, and byte-compares every produced file against
`tests/expected/`. Rebuild the WASM (and, if the emitter changed, regenerate
the C++ goldens) whenever the core changes, or this test fails.

### Deploying to Cloudflare

`wrangler.toml` at the repo root serves `web/dist` as static assets from an
assets-only Worker and claims the `ue-codegen.potionify.com` custom domain:

```bash
cd web && npm install && npm run build && cd ..
npx wrangler deploy
```

## Architecture

- `core/`: the pure emission library. apiSpec text in, a filename-to-content
  map out. No networking, no filesystem. The same core compiles to
  WebAssembly for the web version.
- `cli/`: the console executable. Argument parsing and Convex-CLI-style env
  resolution live in a small side-effect-free `cli_support` library with
  unit tests; then the HTTP fetch via convex-cpp, core emission, and file
  writes.
- `wasm/`: the extern "C" bridge and the Emscripten build script. Artifacts
  are committed under `web/src/generated/`.
- `web/`: the Vite and React web app and the Node golden-parity test.
- `tests/`: GoogleTest unit, golden and live tests.
- `Tools/convex-ue-codegen.bat`: build-and-run wrapper.

## License

Apache-2.0. The vendored [nlohmann/json](third_party/nlohmann/json.hpp) is
MIT, and its license sits next to it.
