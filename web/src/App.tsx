import { useCallback, useMemo, useRef, useState } from "react";
import { zipSync, strToU8 } from "fflate";

import { generate, type GeneratedFiles } from "./codegen";

/** Order files the way a developer reads them: API, BP, module scaffolding. */
function sortedFileNames(files: GeneratedFiles): string[] {
  return Object.keys(files).sort((a, b) => a.localeCompare(b));
}

function normalizeUrl(url: string): string {
  return url.trim().replace(/\/+$/, "");
}

export default function App() {
  // --- spec input ---
  const [deploymentUrl, setDeploymentUrl] = useState("");
  const [deployKey, setDeployKey] = useState("");
  const [specText, setSpecText] = useState("");
  const [specSource, setSpecSource] = useState("pasted apiSpec");
  const [fetchState, setFetchState] = useState<
    { kind: "idle" } | { kind: "busy" } | { kind: "error"; message: string; maybeCors: boolean } | { kind: "ok"; count: number }
  >({ kind: "idle" });

  // --- options ---
  const [prefix, setPrefix] = useState("ConvexApi");
  const [includeInternal, setIncludeInternal] = useState(false);
  const [emitModule, setEmitModule] = useState("");

  // --- output ---
  const [files, setFiles] = useState<GeneratedFiles | null>(null);
  const [generateError, setGenerateError] = useState<string | null>(null);
  const [activeFile, setActiveFile] = useState<string | null>(null);
  const [copied, setCopied] = useState<string | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const fileNames = useMemo(() => (files ? sortedFileNames(files) : []), [files]);
  const active = activeFile && files && activeFile in files ? activeFile : fileNames[0] ?? null;

  const handleFetch = useCallback(async () => {
    const url = normalizeUrl(deploymentUrl);
    if (!url) {
      setFetchState({ kind: "error", message: "Enter a deployment URL first.", maybeCors: false });
      return;
    }
    setFetchState({ kind: "busy" });
    try {
      const response = await fetch(`${url}/api/query`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Convex ${deployKey.trim()}`,
        },
        body: JSON.stringify({ path: "_system/cli/modules:apiSpec", args: {}, format: "json" }),
      });
      const text = await response.text();
      let parsed: unknown;
      try {
        parsed = JSON.parse(text);
      } catch {
        setFetchState({
          kind: "error",
          message: `The deployment returned HTTP ${response.status} with a non-JSON body.`,
          maybeCors: false,
        });
        return;
      }
      const envelope = parsed as { status?: string; value?: unknown[]; errorMessage?: string; message?: string };
      if (envelope.status === "success" && Array.isArray(envelope.value)) {
        setSpecText(text);
        setSpecSource(url);
        setFetchState({ kind: "ok", count: envelope.value.length });
      } else {
        setFetchState({
          kind: "error",
          message:
            envelope.errorMessage ??
            envelope.message ??
            `The deployment returned HTTP ${response.status} without a success envelope. Check the deploy key.`,
          maybeCors: false,
        });
      }
    } catch (error) {
      // fetch() rejects with TypeError on network *and* CORS failures; the
      // browser hides which. Point the user at the offline path.
      setFetchState({
        kind: "error",
        message: error instanceof Error ? error.message : String(error),
        maybeCors: true,
      });
    }
  }, [deploymentUrl, deployKey]);

  const handleFile = useCallback(async (file: File | undefined) => {
    if (!file) return;
    const text = await file.text();
    setSpecText(text);
    setSpecSource(file.name);
    setFetchState({ kind: "idle" });
  }, []);

  const handleGenerate = useCallback(async () => {
    setGenerateError(null);
    setFiles(null);
    try {
      const produced = await generate(specText, {
        prefix: prefix.trim() || "ConvexApi",
        includeInternal,
        emitModule: emitModule.trim() || undefined,
        sourceLabel: specSource,
      });
      setFiles(produced);
      setActiveFile(sortedFileNames(produced)[0] ?? null);
    } catch (error) {
      setGenerateError(error instanceof Error ? error.message : String(error));
    }
  }, [specText, prefix, includeInternal, emitModule, specSource]);

  const handleCopy = useCallback(
    async (name: string) => {
      if (!files) return;
      await navigator.clipboard.writeText(files[name]);
      setCopied(name);
      window.setTimeout(() => setCopied((current) => (current === name ? null : current)), 1500);
    },
    [files],
  );

  const handleDownloadZip = useCallback(() => {
    if (!files) return;
    const entries: Record<string, Uint8Array> = {};
    for (const [name, contents] of Object.entries(files)) entries[name] = strToU8(contents);
    const zipped = zipSync(entries);
    const blob = new Blob([zipped as BlobPart], { type: "application/zip" });
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = `${prefix.trim() || "ConvexApi"}.zip`;
    link.click();
    URL.revokeObjectURL(link.href);
  }, [files, prefix]);

  return (
    <div className="app">
      <header>
        <h1>convex-ue-codegen</h1>
        <p className="subtitle">
          Generate typed Unreal Engine C++ and Blueprint wrappers for your{" "}
          <a href="https://convex.dev" target="_blank" rel="noreferrer">Convex</a> functions —
          entirely in your browser, powered by the same core as the CLI, compiled to WebAssembly.
        </p>
        <p className="security-note">
          Your deploy key is used only for the direct request to your deployment and never
          leaves this browser. This page has no server component.
        </p>
      </header>

      <section className="panel">
        <h2>1. Get your API spec</h2>
        <div className="columns">
          <div className="column">
            <h3>Fetch from a deployment</h3>
            <label>
              Deployment URL
              <input
                type="url"
                placeholder="https://your-deployment.convex.cloud"
                value={deploymentUrl}
                onChange={(e) => setDeploymentUrl(e.target.value)}
              />
            </label>
            <label>
              Deploy key
              <input
                type="password"
                placeholder="prod:your-deployment|..."
                value={deployKey}
                onChange={(e) => setDeployKey(e.target.value)}
                autoComplete="off"
              />
            </label>
            <button onClick={() => void handleFetch()} disabled={fetchState.kind === "busy"}>
              {fetchState.kind === "busy" ? "Fetching..." : "Fetch spec"}
            </button>
            {fetchState.kind === "ok" && (
              <p className="status ok">Fetched {fetchState.count} function spec entries.</p>
            )}
            {fetchState.kind === "error" && (
              <div className="status error">
                <p>{fetchState.message}</p>
                {fetchState.maybeCors && (
                  <p>
                    This is often a CORS restriction (the browser blocks cross-origin requests
                    the deployment does not allow). Run{" "}
                    <code>npx convex function-spec</code> in your Convex project instead and
                    paste its output on the right.
                  </p>
                )}
              </div>
            )}
          </div>
          <div className="column">
            <h3>...or paste / upload it</h3>
            <label>
              apiSpec JSON (the <code>{"{\"status\",\"value\"}"}</code> envelope or a bare array)
              <textarea
                rows={8}
                spellCheck={false}
                placeholder='[{"identifier":"counters.js:get","functionType":"Query",...}]'
                value={specText}
                onChange={(e) => {
                  setSpecText(e.target.value);
                  setSpecSource("pasted apiSpec");
                }}
              />
            </label>
            <button onClick={() => fileInputRef.current?.click()}>Upload .json file</button>
            <input
              ref={fileInputRef}
              type="file"
              accept=".json,application/json"
              style={{ display: "none" }}
              onChange={(e) => void handleFile(e.target.files?.[0])}
            />
          </div>
        </div>
      </section>

      <section className="panel">
        <h2>2. Options</h2>
        <div className="options-row">
          <label>
            Prefix
            <input type="text" value={prefix} onChange={(e) => setPrefix(e.target.value)} />
          </label>
          <label>
            UE module name (optional; emits .Build.cs + Module.cpp)
            <input
              type="text"
              placeholder="e.g. ConvexApi"
              value={emitModule}
              onChange={(e) => setEmitModule(e.target.value)}
            />
          </label>
          <label className="checkbox">
            <input
              type="checkbox"
              checked={includeInternal}
              onChange={(e) => setIncludeInternal(e.target.checked)}
            />
            Include internal functions
          </label>
        </div>
        <button className="primary" onClick={() => void handleGenerate()} disabled={!specText.trim()}>
          Generate
        </button>
        {generateError && (
          <div className="status error">
            <p>{generateError}</p>
          </div>
        )}
      </section>

      {files && active && (
        <section className="panel">
          <div className="output-header">
            <h2>3. Generated files</h2>
            <button className="primary" onClick={handleDownloadZip}>Download .zip</button>
          </div>
          <div className="tabs" role="tablist">
            {fileNames.map((name) => (
              <button
                key={name}
                role="tab"
                aria-selected={name === active}
                className={name === active ? "tab active" : "tab"}
                onClick={() => setActiveFile(name)}
              >
                {name}
              </button>
            ))}
          </div>
          <div className="file-view">
            <div className="file-toolbar">
              <span className="file-meta">
                {files[active].length.toLocaleString()} chars
              </span>
              <button onClick={() => void handleCopy(active)}>
                {copied === active ? "Copied!" : "Copy"}
              </button>
            </div>
            <pre>{files[active]}</pre>
          </div>
        </section>
      )}

      <footer>
        <p>
          Same output as the CLI, byte for byte —{" "}
          <a href="https://github.com/Potionify/convex-ue-codegen" target="_blank" rel="noreferrer">
            Potionify/convex-ue-codegen
          </a>
          . Not an official Convex product.
        </p>
      </footer>
    </div>
  );
}
