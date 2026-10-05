import { invoke, onProgress, isShell } from "../lib/ipc";
import { el } from "../lib/format";
import { confirmDlg, notify, toast } from "../ui/shell";
import { recordRun, isPremium } from "../lib/session";
import { openPremiumPrompt } from "./auth";
import type { OpResult } from "../types";

let running = false;

async function aimBackupState(): Promise<boolean | null> {
  if (!isShell()) return null;
  try {
    const s = await invoke<{ hasBackup: boolean }>("aim.status");
    return s.hasBackup;
  } catch {
    return null;
  }
}

export function renderAIMOptimze(root: HTMLElement): void {
  root.innerHTML = "";

  // ── Card 1: AIM REG (your personal values) ──
  const mk = el("div", "card glass card-icon");
  mk.dataset.search = "aim reg mouse keyboard personal values fix";
  mk.innerHTML = `<div class="card-ihead">
      <span class="icon-tile"><i class="ph ph-crosshair"></i></span>
      <h3>AIM REG${
        isPremium() ? "" : `<span class="chip prem" title="Advanced Aim Engine — PRIMEx Premium">🔒 PREMIUM</span>`
      }</h3>
    </div>
    <p class="sub">Your personal registry values for competitive aim.</p>
    <div class="row">
      <button class="btn sm primary" id="mk-apply">Apply fix</button>
      <button class="btn sm ghost" id="mk-revert">Revert</button>
      <div class="spacer"></div>
      <span class="stat" id="mk-state" style="font-family:var(--mono);font-size:12px;color:var(--text-2)"></span>
    </div>
    <div style="margin-top:14px"><div class="pbar"><div id="mk-bar"></div></div>
    <p class="sub" id="mk-msg" style="margin-top:8px">Idle. Log off + back on after applying.</p></div>`;
  root.appendChild(mk);

  // ── Card 2: Raw input (disable accel + precision) ──
  const raw = el("div", "card glass card-icon");
  raw.dataset.search = "raw input disable acceleration precision mouse aimoptimze aim";
  raw.innerHTML = `<div class="card-ihead">
      <span class="icon-tile"><i class="ph ph-mouse"></i></span>
      <h3>Raw input</h3>
    </div>
    <p class="sub">Flat 1:1 curves and pointer precision off. Backed up first; log off to load.</p>
    <div style="display:flex;gap:22px;align-items:center;flex-wrap:wrap">
      <label style="display:flex;gap:10px;align-items:center;cursor:pointer">
        <span class="toggle" id="tg-accel" role="switch" tabindex="0" aria-checked="false" aria-label="Mouse acceleration"></span>
        <span style="font-weight:600">Acceleration</span>
      </label>
      <label style="display:flex;gap:10px;align-items:center;cursor:pointer">
        <span class="toggle" id="tg-prec" role="switch" tabindex="0" aria-checked="false" aria-label="Pointer precision"></span>
        <span style="font-weight:600">Precision</span>
      </label>
      <span class="stat" id="raw-state" style="font-family:var(--mono);font-size:12px;color:var(--text-2)"></span>
    </div>`;
  root.appendChild(raw);

  // ── Card 3: BOOSTER ──
  const bo = el("div", "card glass card-icon");
  bo.dataset.search = "booster advance trim clean deep game boost priority hd-player";
  bo.innerHTML = `<div class="card-ihead">
      <span class="icon-tile"><i class="ph ph-rocket-launch"></i></span>
      <h3>BOOSTER</h3>
    </div>
    <p class="sub">Deep clean, priority boost, and bloat kill in one pass.</p>
    <div class="row">
      <button class="btn sm primary" id="bo-run">Run booster</button>
      <button class="btn sm ghost" id="bo-prio">Boost running game only</button>
    </div>
    <div style="margin-top:14px"><div class="pbar"><div id="bo-bar"></div></div>
    <p class="sub" id="bo-msg" style="margin-top:8px">Idle.</p></div>`;
  root.appendChild(bo);

  const refreshMk = async () => {
    const st = mk.querySelector("#mk-state")!;
    const b = await aimBackupState();
    st.textContent =
      b === null ? (isShell() ? "status error" : "dev mode")
      : b ? "backup: saved" : "backup: none";
  };
  void refreshMk();

  const bar = (id: string, pct: number, msg: string) => {
    const b = root.querySelector(`#${id}`) as HTMLElement;
    if (b) b.style.width = `${pct}%`;
    const t = root.querySelector(`#${id.replace("-bar", "-msg")}`);
    if (t) t.textContent = msg;
  };
  onProgress((p) => {
    bar("mk-bar", p.pct, p.msg);
    bar("bo-bar", p.pct, p.msg);
  });

  const guard = (): boolean => {
    if (!isShell()) {
      toast({ title: "Dev mode", body: "Run inside the shell.", kind: "info" });
      return false;
    }
    if (running) {
      toast({ title: "Busy", body: "Wait for the current task.", kind: "info" });
      return false;
    }
    return true;
  };

  mk.querySelector("#mk-apply")!.addEventListener("click", async () => {
    if (!guard()) return;
    if (!isPremium()) {
      openPremiumPrompt();
      return;
    }
    running = true;
    try {
      const r = await invoke<OpResult>("aimreg.apply");
      recordRun("aimreg", 1);
      notify("AIM REG", r.message);
      toast({ title: "AIM REG", body: r.message, kind: "ok" });
      await refreshMk();
    } catch (e) {
      toast({ title: "AIM REG", body: String(e), kind: "err" });
    } finally {
      running = false;
    }
  });

  mk.querySelector("#mk-revert")!.addEventListener("click", async () => {
    if (!guard()) return;
    running = true;
    try {
      const r = await invoke<OpResult>("aimreg.revert");
      notify("AIM REG", r.message);
      toast({ title: "AIM REG", body: r.message, kind: "ok" });
      await refreshMk();
    } catch (e) {
      toast({ title: "AIM REG", body: String(e), kind: "err" });
    } finally {
      running = false;
    }
  });

  bo.querySelector("#bo-run")!.addEventListener("click", async () => {
    if (!guard()) return;
    const ok = await confirmDlg(
      "Run booster?",
      "Kills bloatware + background apps, deep-cleans temp, boosts running games. Continue?"
    );
    if (!ok) return;
    running = true;
    try {
      const r = await invoke<OpResult>("booster.run");
      recordRun("booster", 1);
      notify("BOOSTER", r.message);
      toast({ title: "BOOSTER", body: r.message, kind: "ok" });
    } catch (e) {
      toast({ title: "BOOSTER", body: String(e), kind: "err" });
    } finally {
      running = false;
    }
  });

  bo.querySelector("#bo-prio")!.addEventListener("click", async () => {
    if (!guard()) return;
    running = true;
    try {
      const r = await invoke<OpResult>("game.boost");
      toast({ title: "Game priority", body: r.message, kind: "ok" });
    } catch (e) {
      toast({ title: "Game priority", body: String(e), kind: "err" });
    } finally {
      running = false;
    }
  });

  // ── Raw input toggles ──
  const setTgl = (id: string, on: boolean) => {
    const t = raw.querySelector(`#${id}`)!;
    t.setAttribute("aria-checked", String(on));
  };
  const refreshRaw = async () => {
    if (!isShell()) {
      raw.querySelector("#raw-state")!.textContent = "dev mode";
      return;
    }
    try {
      const s = await invoke<{ accel: boolean; precision: boolean }>("mouse.state");
      setTgl("tg-accel", s.accel);
      setTgl("tg-prec", s.precision);
      raw.querySelector("#raw-state")!.textContent =
        `accel ${s.accel ? "on" : "off"} · precision ${s.precision ? "on" : "off"}`;
    } catch {
      raw.querySelector("#raw-state")!.textContent = "state error";
    }
  };
  const flipTgl = async (id: string, cmd: string) => {
    const t = raw.querySelector(`#${id}`)!;
    const on = t.getAttribute("aria-checked") !== "true";
    if (!guard()) return;
    try {
      const r = await invoke<OpResult>(cmd, { on });
      setTgl(id, on);
      toast({ title: "Raw input", body: r.message, kind: "ok" });
      await refreshRaw();
    } catch (e) {
      toast({ title: "Raw input", body: String(e), kind: "err" });
    }
  };
  const wireTgl = (id: string, cmd: string) => {
    const t = raw.querySelector(`#${id}`)!;
    t.addEventListener("click", () => void flipTgl(id, cmd));
    t.addEventListener("keydown", (e) => {
      if ((e as KeyboardEvent).key === "Enter" || (e as KeyboardEvent).key === " ") {
        e.preventDefault();
        void flipTgl(id, cmd);
      }
    });
  };
  wireTgl("tg-accel", "mouse.accel");
  wireTgl("tg-prec", "mouse.precision");
  void refreshRaw();
}
