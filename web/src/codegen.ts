// Thin TypeScript facade over the WASM build of the emission core. Mirrors
// the CLI exactly: the same core produces the same bytes.

import createModule, { type CodegenModule } from "./generated/codegen.mjs";

export interface GenerateOptions {
  prefix?: string;
  includeInternal?: boolean;
  emitModule?: string;
  /** Also emit <prefix>.as, AngelScript wrappers for the Hazelight UE fork. */
  emitScript?: boolean;
  stamp?: string;
  sourceLabel?: string;
}

export type GeneratedFiles = Record<string, string>;

let modulePromise: Promise<CodegenModule> | null = null;

function loadModule(): Promise<CodegenModule> {
  modulePromise ??= createModule();
  return modulePromise;
}

function allocUtf8(module: CodegenModule, text: string): number {
  const size = module.lengthBytesUTF8(text) + 1;
  const ptr = module._malloc(size);
  module.stringToUTF8(text, ptr, size);
  return ptr;
}

/**
 * Generate the UE wrapper files for an apiSpec document (bare value array or
 * the {"status","value"} envelope). Throws Error with the core's message on
 * malformed input.
 */
export async function generate(
  specJson: string,
  options: GenerateOptions = {},
): Promise<GeneratedFiles> {
  const module = await loadModule();
  const specPtr = allocUtf8(module, specJson);
  const optionsPtr = allocUtf8(module, JSON.stringify(options));
  let resultPtr = 0;
  try {
    resultPtr = module._convex_ue_codegen_generate(specPtr, optionsPtr);
    if (resultPtr === 0) throw new Error("codegen: out of memory");
    const result = JSON.parse(module.UTF8ToString(resultPtr)) as
      | { files: GeneratedFiles }
      | { error: string };
    if ("error" in result) throw new Error(result.error);
    return result.files;
  } finally {
    module._free(specPtr);
    module._free(optionsPtr);
    if (resultPtr !== 0) module._convex_ue_codegen_free(resultPtr);
  }
}
