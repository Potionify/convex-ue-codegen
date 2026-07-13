import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  build: {
    rollupOptions: {
      // The Emscripten module dual-targets Node and the browser; its Node
      // branch does `await import("node:module")`, which is guarded by an
      // environment check and never executes in the browser. Mark it external
      // so Rollup does not try to resolve it for the browser bundle.
      external: ["node:module"],
    },
  },
});
