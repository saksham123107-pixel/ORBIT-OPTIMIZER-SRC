import type { IpcEvent, ProgressPayload } from "../types";

// Typed bridge to the C# backend.
// Request:  window.chrome.webview.postMessage({ id, cmd, args })
// Response: { id, ok, data? | error? }  (matched by id)
// Events:   { type: "event", name, payload } (progress, win.state, ...)

type Resolver = {
  resolve: (v: unknown) => void;
  reject: (e: Error) => void;
  timer: number;
};

const pending = new Map<number, Resolver>();
const eventSubs = new Map<string, Set<(p: Record<string, unknown>) => void>>();
let nextId = 1;
let wired = false;

const INVOKE_TIMEOUT_MS = 120000;

function wv() {
  const w = window.chrome?.webview;
  if (!w) throw new Error("WebView2 bridge unavailable (not running in shell?)");
  return w;
}

function ensureWired() {
  if (wired) return;
  wired = true;
  wv().addEventListener("message", (e: { data: unknown }) => {
    const msg = e.data as Record<string, unknown>;
    if (!msg || typeof msg !== "object") return;
    if (msg["type"] === "event") {
      const ev = msg as unknown as IpcEvent;
      eventSubs.get(ev.name)?.forEach((cb) =>
        cb((ev.payload ?? {}) as Record<string, unknown>)
      );
      return;
    }
    const id = msg["id"] as number;
    const r = pending.get(id);
    if (!r) return;
    pending.delete(id);
    window.clearTimeout(r.timer);
    if (msg["ok"] === true) r.resolve((msg as { data?: unknown }).data);
    else {
      const err = (msg as { error?: unknown }).error;
      r.reject(new Error(String(err ?? "backend error")));
    }
  });
}

/** Typed invoke. Rejects on backend error or timeout. */
export function invoke<T>(cmd: string, args?: unknown): Promise<T> {
  ensureWired();
  const id = nextId++;
  return new Promise<T>((resolve, reject) => {
    const timer = window.setTimeout(() => {
      pending.delete(id);
      reject(new Error(`Timed out waiting for '${cmd}'`));
    }, INVOKE_TIMEOUT_MS);
    pending.set(id, {
      resolve: resolve as (v: unknown) => void,
      reject,
      timer,
    });
    try {
      wv().postMessage({ id, cmd, args: args ?? {} });
    } catch (err) {
      pending.delete(id);
      window.clearTimeout(timer);
      reject(err instanceof Error ? err : new Error(String(err)));
    }
  });
}

/** Subscribe to backend push events (progress, win.state, notify). */
export function onEvent(
  name: string,
  cb: (p: Record<string, unknown>) => void
): () => void {
  let set = eventSubs.get(name);
  if (!set) {
    set = new Set();
    eventSubs.set(name, set);
  }
  set.add(cb);
  return () => set!.delete(cb);
}

export function onProgress(cb: (p: ProgressPayload) => void): () => void {
  return onEvent("progress", (p) =>
    cb({ pct: Number(p["pct"] ?? 0), msg: String(p["msg"] ?? "") })
  );
}

/** True when running outside the WPF shell (plain browser dev). */
export function isShell(): boolean {
  return !!window.chrome?.webview;
}
