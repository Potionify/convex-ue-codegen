// Golden parity test for the WASM build: feeds the same fixture and fixed
// stamp the C++ golden tests use through the WebAssembly module and
// byte-compares every produced file against tests/expected/. The WASM build
// must be EXACTLY the CLI's output. Run via `npm test` (or `node
// scripts/parity-test.mjs`) from web/.

import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";
import process from "node:process";

import createModule from "../src/generated/codegen.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, "..", "..");
const fixturePath = path.join(repoRoot, "tests", "fixtures", "apispec_full.json");
const expectedDir = path.join(repoRoot, "tests", "expected");

// Must match kStamp in tests/golden_test.cpp.
const STAMP = "// Source: apispec_full.json (golden fixture)";

/** Call the C bridge with JS strings, returning the parsed result object. */
function generate(module, specJson, options) {
  const alloc = (text) => {
    const size = module.lengthBytesUTF8(text) + 1;
    const ptr = module._malloc(size);
    module.stringToUTF8(text, ptr, size);
    return ptr;
  };
  const specPtr = alloc(specJson);
  const optionsPtr = alloc(JSON.stringify(options));
  let resultPtr = 0;
  try {
    resultPtr = module._convex_ue_codegen_generate(specPtr, optionsPtr);
    if (resultPtr === 0) throw new Error("generate returned null");
    return JSON.parse(module.UTF8ToString(resultPtr));
  } finally {
    module._free(specPtr);
    module._free(optionsPtr);
    if (resultPtr !== 0) module._convex_ue_codegen_free(resultPtr);
  }
}

const module = await createModule();
const spec = await readFile(fixturePath, "utf8");
const result = generate(module, spec, { prefix: "ConvexApi", stamp: STAMP });

if (result.error) {
  console.error(`FAIL: wasm generate returned error: ${result.error}`);
  process.exit(1);
}

const expectedNames = (await readdir(expectedDir)).sort();
const producedNames = Object.keys(result.files).sort();

let failures = 0;
const fail = (message) => {
  failures += 1;
  console.error(`FAIL: ${message}`);
};

if (JSON.stringify(expectedNames) !== JSON.stringify(producedNames)) {
  fail(`file set mismatch\n  expected: ${expectedNames.join(", ")}\n  produced: ${producedNames.join(", ")}`);
}

for (const name of expectedNames) {
  if (!(name in result.files)) continue;
  const expected = await readFile(path.join(expectedDir, name)); // Buffer, exact bytes
  const produced = Buffer.from(result.files[name], "utf8");
  if (!expected.equals(produced)) {
    fail(`${name}: content differs (expected ${expected.length} bytes, produced ${produced.length} bytes)`);
    // Point at the first differing byte to ease debugging.
    const n = Math.min(expected.length, produced.length);
    for (let i = 0; i < n; i += 1) {
      if (expected[i] !== produced[i]) {
        console.error(
          `  first difference at byte ${i}:\n` +
            `  expected ...${JSON.stringify(expected.subarray(Math.max(0, i - 40), i + 40).toString("utf8"))}\n` +
            `  produced ...${JSON.stringify(produced.subarray(Math.max(0, i - 40), i + 40).toString("utf8"))}`,
        );
        break;
      }
    }
  } else {
    console.log(`ok: ${name} (${expected.length} bytes, byte-identical)`);
  }
}

// Also verify the error path returns the {"error"} form instead of throwing.
const errorResult = generate(module, "{not json", {});
if (typeof errorResult.error !== "string" || errorResult.error.length === 0) {
  fail("malformed spec did not produce the {\"error\"} form");
} else {
  console.log(`ok: malformed spec -> error form (${JSON.stringify(errorResult.error)})`);
}

if (failures > 0) {
  console.error(`\n${failures} failure(s)`);
  process.exit(1);
}
console.log(`\nPASS: ${expectedNames.length} file(s) byte-identical to tests/expected/`);
