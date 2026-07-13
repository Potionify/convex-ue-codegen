// Hand-written declaration for the Emscripten-generated codegen.mjs (which
// wasm/build-wasm.bat regenerates; this file is stable and committed).

export interface CodegenModule {
  _convex_ue_codegen_generate(specPtr: number, optionsPtr: number): number;
  _convex_ue_codegen_free(ptr: number): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
  UTF8ToString(ptr: number): string;
  stringToUTF8(text: string, ptr: number, maxBytes: number): number;
  lengthBytesUTF8(text: string): number;
}

declare function createModule(): Promise<CodegenModule>;
export default createModule;
