import "./styles/tokens.css";
import "./styles/base.css";
import "./styles/components.css";
import "./styles/oled.css";
import "@phosphor-icons/web/regular";

import { invoke } from "./lib/ipc";
import { mountShell, setView, renderUserCard, clearUserCard, toast, injectPageHead } from "./ui/shell";
import { splash } from "./ui/splash";
import { renderDashboard } from "./views/dashboard";
import { renderAIMOptimze } from "./views/aimoptimze";
import { renderOptimizer } from "./views/optimizer";
import { renderDebloat } from "./views/debloat";
import { renderServices } from "./views/services";
import { renderSettings } from "./views/settings";
import { renderAuth, renderAuthLoading } from "./views/auth";
import { setSession } from "./lib/session";
import type { ViewId, AuthSession, AuthStatus } from "./types";

const renderers: Record<ViewId, (root: HTMLElement) => void> = {
  dashboard: renderDashboard,
  aimoptimze: renderAIMOptimze,
  optimizer: renderOptimizer,
  debloat: renderDebloat,
  services: renderServices,
  settings: renderSettings,
};

function navigate(v: ViewId): void {
  const root = document.getElementById("view")!;
  if (!root) return;
  root.dataset.view = v;
  root.classList.remove("view-enter");
  void root.offsetWidth;
  renderers[v](root);
  injectPageHead(v, root);
  root.classList.add("view-enter");
}

// theme + motion prefs before paint
// Optional ?host=native: OLED red/black skin (shell-style glass is default).
const params = new URLSearchParams(location.search);
if (params.get("host") === "native") {
  document.documentElement.dataset.host = "native";
}
const saved = localStorage.getItem("px-theme");
document.documentElement.dataset.theme = saved === "light" ? "light" : "dark";
if (localStorage.getItem("px-motion") === "off") {
  document.documentElement.style.setProperty("--dur-micro", "0.01ms");
  document.documentElement.style.setProperty("--dur-page", "0.01ms");
}

let revalTimer: number | undefined;

function stopReval(): void {
  if (revalTimer !== undefined) {
    window.clearInterval(revalTimer);
    revalTimer = undefined;
  }
}

function startReval(): void {
  stopReval();
  revalTimer = window.setInterval(async () => {
    try {
      const s = await invoke<AuthSession>("auth:revalidate");
      if (!s.valid) {
        stopReval();
        // try saved-session re-login first; only show login if that fails too
        try {
          const auto = await invoke<AuthSession & { ok: boolean; note: string }>(
            "auth:autoLogin"
          );
          if (auto.ok && auto.valid) {
            setSession(auto);
            renderUserCard(auto, accountMenu());
            startReval();
            return;
          }
        } catch {
          /* fall through to login screen */
        }
        boot(true);
      } else {
        setSession(s);
        renderUserCard(s, accountMenu());
      }
    } catch {
      /* offline blip — next tick retries */
    }
  }, 10 * 60 * 1000);
}

function accountMenu() {
  return {
    onSignOut: async () => {
      try {
        await invoke("auth:logout");
      } catch {
        /* ignore */
      }
      stopReval();
      clearUserCard();
      toast({ title: "Signed out", kind: "info" });
      boot(true);
    },
  };
}

function enterApp(session: AuthSession): void {
  setSession(session);
  document.getElementById("app")!.innerHTML = "";
  mountShell(navigate);
  renderUserCard(session, accountMenu());
  setView("dashboard");
  startReval();
}

async function boot(forceAuthScreen = false): Promise<void> {
  stopReval();
  clearUserCard();
  splash(async () => {
    const app = document.getElementById("app")!;
    // wait for KeyAuth init (window shows instantly; usage stays gated)
    let status: AuthStatus | null = null;
    renderAuthLoading(app);
    for (let i = 0; i < 16; i++) {
      try {
        const s = await invoke<AuthStatus>("auth:getStatus");
        if (!s.pending) {
          status = s;
          break;
        }
      } catch (e) {
        status = {
          initOk: false,
          offline: true,
          versionMismatch: false,
          initError: e instanceof Error ? e.message : String(e),
          valid: false,
          username: "",
          tier: "None",
          tierLabel: "",
          timeLeft: "",
          expiry: 0,
          grace: false,
          pending: false,
        };
        break;
      }
      await new Promise((r) => setTimeout(r, 2000));
    }
    if (!status) {
      status = {
        initOk: false,
        offline: true,
        versionMismatch: false,
        initError: "Auth servers timed out.",
        valid: false,
        username: "",
        tier: "None",
        tierLabel: "",
        timeLeft: "",
        expiry: 0,
        grace: false,
        pending: false,
      };
    }

    if (status.versionMismatch) {
      renderAuth(app, { status, onAuthed: enterApp });
      return;
    }
    if (status.valid && !forceAuthScreen) {
      enterApp(status);
      if (status.grace) {
        toast({
          title: "Offline mode",
          body: "Showing last known license (24h grace).",
          kind: "info",
        });
      }
      return;
    }
    // silent auto-login (shimmer inside auth view while it runs)
    if (!forceAuthScreen) {
      try {
        const auto = await invoke<
          AuthSession & { ok: boolean; note: string }
        >("auth:autoLogin");
        if (auto.ok && auto.valid) {
          enterApp(auto);
          if (auto.grace) {
            toast({
              title: "Offline mode",
              body: "Showing last known license (24h grace).",
              kind: "info",
            });
          }
          return;
        }
      } catch {
        /* fall through to login screen */
      }
    }
    renderAuth(app, { status, onAuthed: enterApp });
  });
}

void boot();
