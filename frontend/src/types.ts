// Shared contracts. Mirror of C# response shapes in shell/Ipc.cs.

export interface IpcOk<T> {
  id: number;
  ok: true;
  data: T;
}

export interface IpcFail {
  id: number;
  ok: false;
  error: string;
}

export type IpcResponse<T> = IpcOk<T> | IpcFail;

export interface IpcEvent {
  type: "event";
  name: string;
  payload?: Record<string, unknown>;
}

export interface ProgressPayload {
  pct: number;
  msg: string;
}

export interface SysInfo {
  width: number;
  height: number;
  admin: boolean;
}

export interface AimStatus {
  hasBackup: boolean;
}

export interface OpResult {
  message: string;
}

export type ViewId =
  | "dashboard"
  | "aimoptimze"
  | "optimizer"
  | "debloat"
  | "services"
  | "settings";

export interface ToastMsg {
  title: string;
  body?: string;
  kind: "ok" | "err" | "info";
}

export interface NotifyItem {
  id: number;
  title: string;
  body: string;
  time: string;
  read: boolean;
}

export interface AuthSession {
  valid: boolean;
  username: string;
  avatar?: string;
  tier: string;
  tierLabel: string;
  timeLeft: string;
  expiry: number;
  grace: boolean;
  source?: "discord" | "keyauth";
}

export interface AuthStatus extends AuthSession {
  pending: boolean;
  initOk: boolean;
  offline: boolean;
  versionMismatch: boolean;
  initError: string;
}

declare global {
  interface Window {
    chrome?: {
      webview?: {
        postMessage(msg: unknown): void;
        addEventListener(
          type: "message",
          listener: (e: { data: unknown }) => void
        ): void;
      };
    };
  }
}
