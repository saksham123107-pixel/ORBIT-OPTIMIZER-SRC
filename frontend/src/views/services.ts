import { invoke, onProgress, isShell } from "../lib/ipc";
import { el, esc } from "../lib/format";
import { confirmDlg, notify, toast } from "../ui/shell";
import { recordRun } from "../lib/session";
import { t } from "../i18n";
import type { OpResult } from "../types";

interface Svc {
  name: string;
  displayName: string;
  status: string;
  startType: string;
}

type Action = "start" | "stop" | "disable" | "manual" | "auto";

interface SvcPack {
  id: string;
  label: string;
  title: string;
  blurb: string;
}

const PACKS: SvcPack[] = [
  { id: "telemetry", label: "Telemetry", title: "Disable telemetry services", blurb: "DiagTrack, dmwappush, WER, PCA, diagnostics hub." },
  { id: "updates", label: "Updates", title: "Disable update services", blurb: "wuauserv, UsoSvc, WaaSMedic, DoSvc, BITS, Edge updater." },
  { id: "xbox", label: "Xbox", title: "Disable Xbox services", blurb: "XblAuthManager, XblGameSave, XboxNetApiSvc, XboxGipSvc." },
  { id: "store", label: "Store / cloud", title: "Disable Store + cloud services", blurb: "InstallService, PushToInstall, WSearch, MapsBroker, sync hosts." },
  { id: "print-sensor", label: "Print / sensors", title: "Disable print + sensor services", blurb: "Spooler, StiSvc, WbioSrvc, sensors, WIA." },
  { id: "remote", label: "Remote", title: "Disable remote access services", blurb: "RemoteRegistry, RDP session stack, WinRM, RAS." },
  { id: "misc", label: "Misc floor", title: "Disable misc background services", blurb: "SysMain, Fax, RetailDemo, VSS, SNMP, and other idle hogs." },
];

const STATUS_ORDER: Record<string, number> = {
  running: 0,
  starting: 1,
  stopping: 2,
  paused: 3,
  stopped: 4,
  other: 5,
};

let running = false;

function actionLabel(a: Action): string {
  switch (a) {
    case "start":
      return "Start";
    case "stop":
      return "Stop";
    case "disable":
      return "Disable";
    case "manual":
      return "Manual";
    case "auto":
      return "Automatic";
  }
}

export function renderServices(root: HTMLElement): void {
  root.innerHTML = "";
  const card = el("div", "card glass card-icon");
  card.dataset.search = "services windows safe disable sc config list start stop";
  card.innerHTML = `<div class="card-ihead">
      <img class="card-logo" src="logo.png" alt="ORBIT" />
      <h3>${esc(t("svc.title"))}</h3>
    </div>
    <p class="sub">${t("svc.sub")}</p>
    <div class="row" style="margin-bottom:8px">
      <button class="btn sm" id="svc-refresh"><i class="ph ph-arrow-clockwise"></i> ${esc(t("svc.refresh"))}</button>
      <button class="btn sm primary" id="svc-safe"><i class="ph ph-shield-check"></i> ${esc(t("svc.safe"))}</button>
      <div class="spacer"></div>
      <span class="stat" id="svc-count">loading…</span>
    </div>
    <div class="row" style="margin-bottom:12px" id="svc-packs"></div>
    <div class="row" style="margin-bottom:10px">
      <input type="text" id="svc-q" class="input" placeholder="${esc(t("svc.filter"))}"
             aria-label="${esc(t("svc.filter"))}"
             style="flex:1;min-width:180px;padding:8px 10px;border-radius:var(--r-sm);border:1px solid var(--border-glass);background:var(--bg-layer-1);color:var(--text-1)" />
    </div>
    <div id="svc-list" class="stagger" style="max-height:460px;overflow-y:auto"></div>
    <div style="margin-top:14px"><div class="pbar"><div id="svc-bar"></div></div>
    <p class="sub" id="svc-msg" style="margin-top:8px">Idle. Loading services…</p></div>`;
  root.appendChild(card);

  const list = card.querySelector("#svc-list") as HTMLElement;
  const bar = card.querySelector("#svc-bar") as HTMLElement;
  const msg = card.querySelector("#svc-msg")!;
  const countEl = card.querySelector("#svc-count")!;
  const filter = card.querySelector("#svc-q") as HTMLInputElement;

  let services: Svc[] = [];

  const applyFilter = () => {
    const q = filter.value.trim().toLowerCase();
    list.querySelectorAll<HTMLElement>(".twk-row").forEach((row) => {
      const s = row.dataset.search || "";
      row.style.display = !q || s.includes(q) ? "" : "none";
    });
    const shown = [...list.querySelectorAll<HTMLElement>(".twk-row")].filter(
      (r) => r.style.display !== "none"
    ).length;
    countEl.textContent = `${shown} shown / ${services.length} services`;
  };

  const paint = () => {
    list.innerHTML = "";
    services = (Array.isArray(services) ? services : []).filter(
      (s): s is Svc => !!s && typeof (s.name || s.displayName) === "string"
    );
    if (services.length === 0) {
      list.innerHTML = `<div class="empty"><div class="big"><i class="ph ph-gears"></i></div>${
        isShell() ? "No services found." : "Dev mode — run inside the shell."
      }</div>`;
      countEl.textContent = "0 services";
      return;
    }
    const frag = document.createDocumentFragment();
    const label = (s: Svc) => String(s.displayName || s.name || "");
    services
      .slice()
      .sort((a, b) => {
        const sa = STATUS_ORDER[a.status] ?? 9;
        const sb = STATUS_ORDER[b.status] ?? 9;
        if (sa !== sb) return sa - sb;
        return label(a).localeCompare(label(b));
      })
      .forEach((svc, i) => {
        const row = el("div", "twk-row");
        row.style.animationDelay = `${Math.min(i, 40) * 12}ms`;
        row.dataset.search = `${svc.displayName || ""} ${svc.name || ""} ${svc.status || ""} ${svc.startType || ""}`.toLowerCase();
        const canStart = svc.status !== "running" && svc.status !== "starting";
        const canStop = svc.status === "running" || svc.status === "starting";
        row.innerHTML = `
          <span class="twk-meta" style="flex:1;min-width:0">
            <span class="twk-name">${esc(svc.displayName || svc.name || "Unknown")}</span>
            <span class="twk-desc">${esc(svc.name || "")}</span>
          </span>
          <span class="stat">${esc(svc.status || "other")}</span>
          <span class="stat">${esc(svc.startType || "")}</span>
          <span class="svc-actions" style="display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end">
            ${canStart ? `<button class="btn sm" data-act="start" type="button">Start</button>` : ""}
            ${canStop ? `<button class="btn sm ghost" data-act="stop" type="button">Stop</button>` : ""}
            ${
              svc.startType === "disabled"
                ? `<button class="btn sm ghost" data-act="auto" type="button">Auto</button>
                   <button class="btn sm ghost" data-act="manual" type="button">Manual</button>`
                : `<button class="btn sm ghost" data-act="disable" type="button">Disable</button>
                   ${
                     svc.startType !== "manual"
                       ? `<button class="btn sm ghost" data-act="manual" type="button">Manual</button>`
                       : `<button class="btn sm ghost" data-act="auto" type="button">Auto</button>`
                   }`
            }
          </span>`;
        row.querySelectorAll<HTMLButtonElement>("button[data-act]").forEach((b) => {
          b.addEventListener("click", async (ev) => {
            ev.preventDefault();
            ev.stopPropagation();
            if (running) return;
            const action = b.dataset.act as Action;
            const ok = await confirmDlg(
              `${actionLabel(action)} ${svc.name}?`,
              `${actionLabel(action)} "${svc.displayName || svc.name}"? Administrator rights may be required.`
            );
            if (!ok) return;
            running = true;
            b.disabled = true;
            try {
              const r = await invoke<OpResult>("services.set", {
                name: svc.name,
                action,
              });
              notify("Services", r.message);
              toast({ title: "Services", body: r.message, kind: "ok" });
              msg.textContent = r.message;
              await refresh();
            } catch (e) {
              toast({ title: "Services", body: String(e), kind: "err" });
              msg.textContent = String(e);
            } finally {
              running = false;
              b.disabled = false;
            }
          });
        });
        frag.appendChild(row);
      });
    list.appendChild(frag);
    applyFilter();
  };

  const refresh = async () => {
    if (!isShell()) {
      msg.textContent = "Dev mode — run inside the shell.";
      countEl.textContent = "0 services";
      return;
    }
    msg.textContent = "Loading services…";
    try {
      services = await invoke<Svc[]>("services.list");
      paint();
      msg.textContent = `${services.length} Win32 services. Start/stop or change start type; Safe Disable only sets start=disabled.`;
    } catch (e) {
      msg.textContent = "Load failed.";
      toast({ title: "Services", body: String(e), kind: "err" });
    }
    applyFilter();
  };

  paint();
  void refresh();

  filter.addEventListener("input", applyFilter);
  card.querySelector("#svc-refresh")!.addEventListener("click", () => void refresh());

  onProgress((p) => {
    bar.style.width = `${p.pct}%`;
    msg.textContent = p.msg;
  });

  const packBox = card.querySelector("#svc-packs")!;
  for (const p of PACKS) {
    const b = el("button", "btn sm ghost");
    b.type = "button";
    b.textContent = p.label;
    b.title = `${p.title}: ${p.blurb}`;
    b.addEventListener("click", async () => {
      if (running || !isShell()) return;
      const ok = await confirmDlg(p.title, `${p.blurb} Sets start=disabled only (running services stay up until reboot). Continue?`);
      if (!ok) return;
      running = true;
      b.disabled = true;
      try {
        const r = await invoke<OpResult & { okCount?: number; failCount?: number }>(
          "services.disablePack",
          { pack: p.id }
        );
        recordRun(`services:pack:${p.id}`, r.okCount ?? 0);
        notify("Services", r.message);
        toast({ title: p.label, body: r.message, kind: "ok" });
        msg.textContent = r.message;
        bar.style.width = "100%";
        await refresh();
      } catch (e) {
        toast({ title: p.label, body: String(e), kind: "err" });
        msg.textContent = String(e);
      } finally {
        running = false;
        b.disabled = false;
      }
    });
    packBox.appendChild(b);
  }

  card.querySelector("#svc-safe")!.addEventListener("click", async () => {
    if (running || !isShell()) return;
    const ok = await confirmDlg(
      "Run Safe Disable?",
      "Sets a curated list of services to start=disabled (sc config start=disabled). Running services are not force-stopped. Continue?"
    );
    if (!ok) return;
    running = true;
    const btn = card.querySelector("#svc-safe") as HTMLButtonElement;
    btn.disabled = true;
    try {
      const r = await invoke<OpResult & { okCount?: number; failCount?: number }>(
        "services.safeDisable"
      );
      recordRun("services:safeDisable", r.okCount ?? 0);
      notify("Services", r.message);
      toast({ title: "Safe Disable", body: r.message, kind: "ok" });
      msg.textContent = r.message;
      bar.style.width = "100%";
      await refresh();
    } catch (e) {
      toast({ title: "Safe Disable", body: String(e), kind: "err" });
      msg.textContent = String(e);
    } finally {
      running = false;
      btn.disabled = false;
    }
  });
}
