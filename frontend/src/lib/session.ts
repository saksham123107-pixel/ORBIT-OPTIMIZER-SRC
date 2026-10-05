// Session-scoped real data: operation results feed the dashboard chart.
import type { AuthSession } from "../types";

export interface RunRecord {
  label: string;
  value: number;
  time: string;
}

let session: AuthSession | null = null;

export function setSession(s: AuthSession): void {
  session = s;
}
export function getSession(): AuthSession | null {
  return session;
}
export function isPremium(): boolean {
  return !!session && session.tier === "Lifetime" && !!(session.valid || session.grace);
}

const runs: RunRecord[] = [];
const listeners = new Set<() => void>();

export function recordRun(label: string, value: number): void {
  runs.unshift({
    label,
    value,
    time: new Date().toLocaleTimeString("en-GB"),
  });
  listeners.forEach((cb) => cb());
}

export function getRuns(): RunRecord[] {
  return [...runs];
}

export function totalCleaned(): number {
  return runs.reduce((a, r) => a + r.value, 0);
}

export function onRunsChanged(cb: () => void): () => void {
  listeners.add(cb);
  return () => listeners.delete(cb);
}
