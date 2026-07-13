# convex-ue-codegen

A standalone C++ console tool that connects to a [Convex](https://convex.dev)
deployment, fetches its function specification, and emits typed
[Unreal Engine](https://www.unrealengine.com) C++ wrappers — both native and
Blueprint-callable — for every deployed function. It lets UE developers
regenerate their typed Convex API from a `.bat` file without opening the editor.

It builds on [convex-cpp](https://github.com/Potionify/convex-cpp) (HTTP client
+ desktop transport) and targets the [convex-ue](https://github.com/Potionify/convex-ue)
`ConvexClient` runtime the generated code calls into.

## What it generates

Given a deployment (or an offline spec file), it writes four files (default
prefix `ConvexApi`, overridable with `--prefix`):

| File | Contents |
|---|---|
| `<Prefix>.h` / `.cpp` | Native C++ wrappers, one function per Convex function, grouped into nested namespaces mirroring the module path (`admin/tools` → `ConvexApi::Admin::Tools`). Queries get a one-shot `Get(...)` **and** a `WatchGet(...)` subscription form; mutations/actions get the one-shot form. |
| `<Prefix>BP.h` / `.cpp` | `UBlueprintFunctionLibrary` subclasses (one per module) of `BlueprintCallable` `UFUNCTION`s, so every function is a node in the Blueprint graph. |

With `--emit-module <Name>` it additionally writes `<Name>.Build.cs` and
`<Name>Module.cpp` so the output folder drops into a UE project as a complete
module (depending on `ConvexClient` + `ConvexCore`).

Typed arguments come from each function's args validator: an `object` validator
becomes one C++ parameter per field (`string`→`FString`, `number`→`double`,
`int64`→`int64`, `id`→`FString` with an `/* Id<table> */` note, `array<string>`→
`TArray<FString>`, optional fields→`TOptional<T>`, objects/unions/records/`any`→
`FConvexValue`, …). A non-object args validator becomes a single pass-through
`TMap<FString, FConvexValue> Args`. Blueprint wrappers omit optional args (noted
in each node's tooltip); use the native wrapper for those.

Output is **deterministic**: identical input always yields byte-identical output
(sorted modules/functions, no timestamps), which the golden tests rely on.

## Building

Requirements: CMake ≥ 3.21, a C++20 compiler (MSVC 2022 first-class), and a
checkout of [convex-cpp](https://github.com/Potionify/convex-cpp).

```bash
cmake -S . -B build -DCONVEX_WITH_IXWEBSOCKET=ON
cmake --build build --config Release
```

The CLI ends up at `build/cli/Release/convex-ue-codegen.exe` (multi-config) or
`build/cli/convex-ue-codegen.exe` (single-config).

| CMake option | Default | Effect |
|---|---|---|
| `CONVEX_CPP_DIR` | `../convex-cpp` | Path to the convex-cpp source tree (configure errors clearly if missing). |
| `CONVEX_UE_CODEGEN_BUILD_TESTS` | ON (top-level) | Build the GoogleTest suite (fetched via FetchContent). |

convex-cpp is added via `add_subdirectory` and built with its bundled
IXWebSocket transport (`CONVEX_WITH_IXWEBSOCKET`). nlohmann/json is vendored
under `third_party/` and is a private, implementation-only dependency of the
core — no public header includes it, keeping the core WASM-portable.

### The `.bat` launcher

`Tools/convex-ue-codegen.bat` builds the CLI on first use (if needed) and then
runs it, forwarding all arguments:

```bat
Tools\convex-ue-codegen.bat --help
Tools\convex-ue-codegen.bat --out MyGame\Source\ConvexApi --emit-module ConvexApi
```

## Usage

```
convex-ue-codegen --out <dir> [--url <u>] [--deploy-key <k>] [--env-file <path>]
                  [--spec-json <file>] [--prefix ConvexApi] [--include-internal]
                  [--stamp <text>] [--emit-module <Name>]
```

- `--out <dir>` (required) — output directory, created if missing.
- `--spec-json <file>` — **offline mode**: read the apiSpec from a file instead
  of the network. Accepts either the raw value array or the full
  `{"status","value"}` envelope.
- `--url` / `--deploy-key` — connect explicitly (otherwise resolved from the
  environment, below).
- `--prefix` — generated API prefix (default `ConvexApi`).
- `--include-internal` — also emit internal-visibility functions.
- `--stamp <text>` — a verbatim provenance line written as the second line of
  every file (for reproducible output). Without it, the line is
  `// Source: <deployment url or spec file>`.
- `--emit-module <Name>` — also emit the `.Build.cs` + `Module.cpp` scaffolding.

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
and exits 0. Any failure — bad key, unreachable backend, unresolved deployment —
prints an `error: ...` message to stderr and exits non-zero.

### How it fetches

Codegen needs no WebSocket: it issues a single
`POST /api/query {"path":"_system/cli/modules:apiSpec","args":{}}` with an
`Authorization: Convex <deploy-key>` header (via convex-cpp's `http_client`),
which returns every deployed function with its args and return validators.
`HttpAction` routes and `_system/*` modules are skipped.

## Deployment resolution

When `--spec-json` is not given and `--url`/`--deploy-key` are not both passed,
the deployment is resolved like the Convex CLI. For each variable, the **first**
of these sources that defines it wins:

1. process environment
2. `--env-file <path>` (if given)
3. `./.env.local`
4. `./convex.env.local`
5. `./.env`

Variables consulted:

- **Deploy key**: `CONVEX_DEPLOY_KEY` (alias `CONVEX_DEPLOYMENT_TOKEN`;
  `CONVEX_DEPLOY_KEY` wins if both are set), or `CONVEX_SELF_HOSTED_ADMIN_KEY`.
- **URL**: `CONVEX_SELF_HOSTED_URL`, then `CONVEX_URL`. If neither is set, the
  URL is synthesized as `https://<name>.convex.cloud`, where `<name>` is the
  text between the first `:` and the `|` of the deploy key (e.g.
  `dev:happy-animal-123|...` → `happy-animal-123`). Self-hosted keys have no such
  name, so a URL must be provided for them.

`.env` files are parsed line-by-line (never shell-sourced), so admin keys
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

The generated files only compile inside a UE module (they include
`ConvexClient` headers and, for the Blueprint ones, are UHT-processed). Drop
them into a module that depends on `ConvexClient` + `ConvexCore` — or use
`--emit-module` to generate that module's scaffolding too.

## Testing

```bash
ctest --test-dir build -C Release --output-on-failure
```

The suite has unit tests (identifier canonicalization, name sanitization, each
validator→type mapping, env-resolution precedence, envelope-vs-array
acceptance), byte-exact **golden** tests, and a **live** test that runs against
the local self-hosted backend from convex-cpp's integration environment. The
live test auto-skips unless `http://127.0.0.1:3210/version` responds and
`../convex-cpp/integration/local.env` exists; when the backend is up, it fetches
and emits for real.

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

This rewrites `tests/expected/` from the current emitter (the test then reports
`SKIPPED`). Review the diff before committing.

## Web version

A static, serverless web app (Vite + React + TypeScript) that produces the
**exact same output as the CLI, byte for byte** — the same `core/` emission
library compiled to WebAssembly. Paste a deployment URL + deploy key (or paste
/ upload an apiSpec JSON), tweak the options, and copy the files or download a
.zip.

**Security model:** there is no server component. The deploy key is used only
for the direct `POST {url}/api/query` request from your browser to your own
deployment (`Authorization: Convex <key>`) and never leaves the browser. If
that fetch is blocked by CORS, run `npx convex function-spec` locally and paste
the output instead.

### Building the WASM module

The built artifacts (`web/src/generated/codegen.mjs` + `codegen.wasm`) are
committed, so the web app builds without the Emscripten SDK. To rebuild after
changing the core:

```bat
rem needs emcc on PATH (emsdk install latest / activate latest / emsdk_env.bat)
wasm\build-wasm.bat
```

or on any shell:

```bash
emcc -O2 -std=c++20 -fwasm-exceptions -Icore/include -Ithird_party \
  core/src/naming.cpp core/src/validator.cpp core/src/type_map.cpp \
  core/src/api_spec.cpp core/src/emit.cpp wasm/wrapper.cpp \
  -sMODULARIZE=1 -sEXPORT_ES6=1 \
  -sEXPORTED_FUNCTIONS=_convex_ue_codegen_generate,_convex_ue_codegen_free,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8 \
  -sALLOW_MEMORY_GROWTH=1 --no-entry -o web/src/generated/codegen.mjs
```

The C bridge is `wasm/wrapper.cpp`: `convex_ue_codegen_generate(spec_json,
options_json)` returns a malloc'd JSON string (`{"files": {...}}` or
`{"error": "..."}`) released via `convex_ue_codegen_free`; no exception ever
crosses the C boundary.

### Building and testing the web app

```bash
cd web
npm install
npm test        # golden parity: WASM output must be byte-identical to tests/expected/
npm run build   # type-check + bundle to web/dist
npm run dev     # local dev server
```

`npm test` runs `web/scripts/parity-test.mjs` in Node: it loads the WASM
module, feeds it `tests/fixtures/apispec_full.json` with the same fixed stamp
the C++ golden tests use, and byte-compares every produced file against
`tests/expected/`. Rebuild the WASM (and, if the emitter changed, regenerate
the C++ goldens) whenever the core changes, or this test will fail.

### Deploying to Cloudflare

`wrangler.toml` at the repo root serves `web/dist` as static assets
(assets-only Worker):

```bash
cd web && npm install && npm run build && cd ..
npx wrangler deploy
```

## Architecture

- **`core/`** — the pure emission library: apiSpec text in, a `filename →
  content` map out. No networking, no filesystem. The same core compiles to
  WebAssembly for the web version.
- **`cli/`** — the console executable: argument parsing and Convex-CLI-style
  env resolution (a small side-effect-free `cli_support` library, unit-tested),
  then the HTTP fetch via convex-cpp, then core emission, then file writes.
- **`wasm/`** — the extern "C" bridge + Emscripten build script; artifacts are
  committed under `web/src/generated/`.
- **`web/`** — the Vite + React web app and the Node golden-parity test.
- **`tests/`** — GoogleTest unit + golden + live tests.
- **`Tools/convex-ue-codegen.bat`** — build-and-run wrapper.

## License

Apache-2.0. Not an official Convex product.
