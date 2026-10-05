import { invoke, isShell } from "../lib/ipc";
import { el, esc } from "../lib/format";
import { toast } from "../ui/shell";
import { getRuns, onRunsChanged, totalCleaned } from "../lib/session";
import type { SysInfo } from "../types";

interface LiveStats {
  cpu?: number | null;
  cpuTemp?: number | null;
  gpu?: number | null;
  gpuTemp?: number | null;
  gpuName?: string | null;
  ramPct?: number | null;
  ramGb?: number | null;
  ramTotal?: number | null;
}

interface DiskInfo {
  total: number;
  free: number;
  label?: string;
}

let liveTimer: number | undefined;

function fmt(v: number | null | undefined, suffix = "", na = "n/a"): string {
  return v === null || v === undefined || Number.isNaN(v)
    ? na
    : `${Math.round(v)}${suffix}`;
}

function setStat(el: HTMLElement, text: string): void {
  if (el.textContent === text) return;
  el.textContent = text;
  el.classList.remove("tick");
  void el.offsetWidth;
  el.classList.add("tick");
}

function fmtBytes(n: number): string {
  if (n >= 1e12) return `${(n / 1e12).toFixed(1)} TB`;
  if (n >= 1e9) return `${(n / 1e9).toFixed(1)} GB`;
  if (n >= 1e6) return `${(n / 1e6).toFixed(1)} MB`;
  return `${n} B`;
}

/** Optimization score: base from cleaned items + admin + session activity. */
function computeScore(admin: boolean, cleaned: number, runs: number): number {
  let s = 420;
  if (admin) s += 180;
  s += Math.min(280, cleaned * 4);
  s += Math.min(120, runs * 30);
  return Math.min(1000, s);
}

export function renderDashboard(root: HTMLElement): void {
  if (liveTimer !== undefined) {
    window.clearInterval(liveTimer);
    liveTimer = undefined;
  }
  root.innerHTML = "";

  // ── Top summary / score bar ──
  const scoreBar = el("div", "scorebar glass");
  scoreBar.innerHTML = `
    <div class="scorebar-segs">
      <div class="scorebar-seg">
        <span class="lab">Optimization</span>
        <span class="val teal" id="sb-score">—/1,000</span>
        <span class="hint">System health score</span>
      </div>
      <div class="scorebar-seg">
        <span class="lab">Cleaned</span>
        <span class="val" id="sb-cleaned">0</span>
        <span class="hint">items this session</span>
      </div>
      <div class="scorebar-seg">
        <span class="lab">Uptime</span>
        <span class="val" id="sb-uptime">—</span>
        <span class="hint">since boot</span>
      </div>
      <div class="scorebar-seg action">
        <button class="btn sm score" id="sb-review" type="button">Review</button>
      </div>
    </div>
    <div class="scorebar-prog"><div id="sb-prog"></div></div>`;
  root.appendChild(scoreBar);

  const sbScore = scoreBar.querySelector<HTMLElement>("#sb-score")!;
  const sbCleaned = scoreBar.querySelector<HTMLElement>("#sb-cleaned")!;
  const sbUptime = scoreBar.querySelector<HTMLElement>("#sb-uptime")!;
  const sbProg = scoreBar.querySelector<HTMLElement>("#sb-prog")!;

  let isAdmin = false;
  const refreshScore = () => {
    const cleaned = totalCleaned();
    const runs = getRuns().length;
    const score = computeScore(isAdmin, cleaned, runs);
    setStat(sbScore, `${score.toLocaleString()}/1,000`);
    setStat(sbCleaned, cleaned.toLocaleString());
    sbProg.style.width = `${score / 10}%`;
  };
  scoreBar.querySelector("#sb-review")!.addEventListener("click", () => {
    toast({
      title: "Review",
      body: "Open Optimizer to review applied tweaks and revert if needed.",
      kind: "info",
    });
    // soft-navigate via hash so main.ts navigation stays intact
    document.querySelector<HTMLElement>('.nav button[data-view="optimizer"]')?.click();
  });

  // uptime ticker
  const tickUptime = () => {
    if (!isShell()) {
      setStat(sbUptime, "dev");
      return;
    }
    const now = Date.now();
    // performance.timeOrigin ≈ process/page start; use it as session proxy
    const ms = now - performance.timeOrigin;
    const h = Math.floor(ms / 3600000);
    const m = Math.floor((ms % 3600000) / 60000);
    setStat(sbUptime, h > 0 ? `${h}h ${m}m` : `${m}m`);
  };
  tickUptime();
  const uptimeTimer = window.setInterval(tickUptime, 30000);
  (root as unknown as { __uptime?: number }).__uptime = uptimeTimer;

  // ── live hardware stat cards ──
  const grid = el("div", "stats stagger");
  const cards: { k: string; id: string; sub: string; icon: string }[] = [
    { k: "CPU", id: "live-cpu", sub: "temp", icon: "ph ph-cpu" },
    { k: "GPU", id: "live-gpu", sub: "temp", icon: "ph ph-cards" },
    { k: "RAM", id: "live-ram", sub: "used", icon: "ph ph-memory" },
    { k: "Display", id: "st-res", sub: "current mode", icon: "ph ph-monitor" },
  ];
  const vals: Record<string, HTMLElement> = {};
  const subs: Record<string, HTMLElement> = {};
  const rings: Record<string, SVGCircleElement> = {};
  const R = 28;
  const CIRC = 2 * Math.PI * R;
  cards.forEach((c, i) => {
    const d = el("div", "stat-card glass has-ring");
    d.style.animationDelay = `${i * 55}ms`;
    d.innerHTML = `
      <div class="sc-top">
        <span class="icon-tile" aria-hidden="true"><i class="${c.icon}"></i></span>
        <button class="sc-pill" type="button" data-detail="${c.id}">View Details</button>
      </div>
      <div class="sc-body">
        <div class="sc-meta">
          <div class="v" id="${c.id}">—</div>
          <div class="k">${c.k}</div>
          <div class="d" id="${c.id}-sub">${c.sub}</div>
        </div>
        <div class="ring" aria-hidden="true">
          <svg viewBox="0 0 72 72" width="64" height="64">
            <circle class="ring-bg" cx="36" cy="36" r="${R}" fill="none" stroke-width="6"/>
            <circle class="ring-fg" id="${c.id}-ring" cx="36" cy="36" r="${R}" fill="none" stroke-width="6"
              stroke-linecap="round" stroke-dasharray="${CIRC.toFixed(1)}"
              stroke-dashoffset="${CIRC.toFixed(1)}" transform="rotate(-90 36 36)"/>
          </svg>
        </div>
      </div>`;
    grid.appendChild(d);
    vals[c.id] = d.querySelector(".v")!;
    subs[c.id] = d.querySelector(".d")!;
    rings[c.id] = d.querySelector(".ring-fg") as SVGCircleElement;
    d.querySelector<HTMLElement>("[data-detail]")?.addEventListener("click", () => {
      toast({
        title: `${c.k} details`,
        body: `${c.k} is live-polled every 1.5s while the shell is running.`,
        kind: "info",
      });
    });
  });
  root.appendChild(grid);

  const setRing = (key: string, pct: number | null | undefined): void => {
    const c = rings[key];
    if (!c) return;
    const p =
      pct === null || pct === undefined || Number.isNaN(pct)
        ? 0
        : Math.max(0, Math.min(100, pct));
    c.style.strokeDashoffset = String(CIRC * (1 - p / 100));
  };

  // ── Temperature / live monitor ──
  const liveCard = el("div", "card glass mon-card");
  liveCard.innerHTML = `
    <div class="mon-head">
      <h3>Temperature</h3>
      <select class="mon-sel" id="mon-sel" aria-label="Sensor">
        <option value="cpu">CPU</option>
        <option value="gpu">GPU</option>
        <option value="ram">RAM</option>
      </select>
    </div>
    <p class="sub">Live load %, last 90 seconds.
      <span style="margin-left:10px"><span style="color:var(--accent)">— CPU</span> ·
      <span style="color:var(--text-2)">— GPU</span> ·
      <span style="color:var(--text-3)">— RAM</span></span></p>
    <div class="chart-wrap" id="livechart"><div class="chart-tip" id="chart-tip"></div></div>`;
  root.appendChild(liveCard);

  const dashRow = el("div", "dash-row");

  const chartCard = el("div", "card glass");
  chartCard.innerHTML = `<h3>Session activity</h3><p class="sub">Items cleaned per operation, live.</p><div id="chart"></div>`;
  dashRow.appendChild(chartCard);

  // ── Disk card ──
  const diskCard = el("div", "disk-card glass");
  diskCard.innerHTML = `
    <div class="disk-head">
      <span class="icon-tile" aria-hidden="true"><i class="ph ph-hard-drive"></i></span>
      <h3>Disk</h3>
    </div>
    <div class="disk-status" id="disk-status">Reading drive usage…</div>
    <div class="disk-bar"><div id="disk-bar" style="width:0%"></div></div>
    <div class="row" style="margin-top:12px">
      <div class="spacer"></div>
      <button class="btn sm" id="disk-clean" type="button">Clean up files</button>
    </div>`;
  dashRow.appendChild(diskCard);
  root.appendChild(dashRow);

  const logCard = el("div", "card glass");
  logCard.innerHTML = `<h3>Recent activity</h3><p class="sub">Latest operations this session.</p><div id="runs"></div>`;
  root.appendChild(logCard);

  // disk load
  const loadDisk = async () => {
    const status = diskCard.querySelector<HTMLElement>("#disk-status")!;
    const bar = diskCard.querySelector<HTMLElement>("#disk-bar")!;
    if (!isShell()) {
      status.innerHTML = `<b>dev mode</b> — disk stats need the shell.`;
      return;
    }
    try {
      const d = await invoke<DiskInfo>("sys.disk");
      const used = d.total - d.free;
      const pct = d.total > 0 ? (used / d.total) * 100 : 0;
      status.innerHTML = `<b>${fmtBytes(d.free)}</b> free of ${fmtBytes(d.total)} (${Math.round(pct)}% used)`;
      bar.style.width = `${pct}%`;
      bar.classList.toggle("ok", pct < 70);
      bar.classList.toggle("warn", pct >= 90);
    } catch {
      status.innerHTML = `<b>n/a</b> — disk info unavailable on this host.`;
    }
  };
  void loadDisk();
  diskCard.querySelector("#disk-clean")!.addEventListener("click", async () => {
    if (!isShell()) {
      toast({ title: "Dev mode", body: "Run inside the shell.", kind: "info" });
      return;
    }
    try {
      const r = await invoke<{ message: string }>("clean.deep");
      toast({ title: "Disk cleanup", body: r.message, kind: "ok" });
      void loadDisk();
      refreshScore();
    } catch (e) {
      toast({ title: "Disk cleanup", body: String(e), kind: "err" });
    }
  });

  // ── rolling buffers ──
  const N = 60;
  const cpuH: number[] = [];
  const gpuH: number[] = [];
  const ramH: number[] = [];
  let sensor: "cpu" | "gpu" | "ram" = "cpu";
  liveCard.querySelector<HTMLSelectElement>("#mon-sel")?.addEventListener("change", (e) => {
    sensor = (e.target as HTMLSelectElement).value as "cpu" | "gpu" | "ram";
    drawLive();
  });

  const push = (arr: number[], v: number | null | undefined) => {
    arr.push(v ?? -1);
    if (arr.length > N) arr.shift();
  };

  const drawLive = () => {
    const box = liveCard.querySelector<HTMLElement>("#livechart")!;
    const tip = liveCard.querySelector<HTMLElement>("#chart-tip")!;
    const W = 560, H = 140, pad = 24;
    const active =
      sensor === "cpu" ? cpuH : sensor === "gpu" ? gpuH : ramH;
    const area = (arr: number[]): string => {
      if (arr.length < 2) return "";
      const pts = arr.map((v, i) => {
        const x = pad + (i * (W - pad * 2)) / (N - 1);
        const vv = v < 0 ? 0 : Math.min(100, v);
        const y = H - pad - (vv / 100) * (H - pad * 2);
        return `${x.toFixed(1)},${y.toFixed(1)}`;
      });
      const first = pts[0].split(",");
      const last = pts[pts.length - 1].split(",");
      return `${pad},${H - pad} ${pts.join(" ")} ${last[0]},${H - pad} ${first[0]},${H - pad}`;
    };
    const line = (arr: number[], color: string, width = 2) => {
      if (arr.length < 2) return "";
      const pts = arr
        .map((v, i) => {
          const x = pad + (i * (W - pad * 2)) / (N - 1);
          const vv = v < 0 ? 0 : Math.min(100, v);
          const y = H - pad - (vv / 100) * (H - pad * 2);
          return `${x.toFixed(1)},${y.toFixed(1)}`;
        })
        .join(" ");
      return `<polyline points="${pts}" fill="none" stroke="${color}" stroke-width="${width}" stroke-linejoin="round"/>`;
    };
    const last = active.length ? active[active.length - 1] : -1;
    const lastLabel = last < 0 ? "n/a" : `${Math.round(last)}%`;
    box.innerHTML = `
      <svg viewBox="0 0 ${W} ${H}" width="100%" role="img" aria-label="Live load graph">
        <defs>
          <linearGradient id="cpu-fill" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color="#e74c3c" stop-opacity="0.4"/>
            <stop offset="100%" stop-color="#e74c3c" stop-opacity="0"/>
          </linearGradient>
        </defs>
        ${[25, 50, 75].map((g) => `<line x1="${pad}" y1="${H - pad - (g / 100) * (H - pad * 2)}" x2="${W - pad}" y2="${H - pad - (g / 100) * (H - pad * 2)}" stroke="var(--border-faint)" stroke-width="1"/><text x="2" y="${H - pad - (g / 100) * (H - pad * 2) + 4}" fill="var(--text-3)" font-size="9">${g}</text>`).join("")}
        ${active.length > 1 ? `<polygon points="${area(active)}" fill="url(#cpu-fill)"/>` : ""}
        ${sensor !== "cpu" ? line(cpuH, "rgba(231,76,60,0.35)", 1.5) : ""}
        ${line(active, "#e74c3c", 2.5)}
      </svg>
      <div class="chart-tip show" style="left:70%;top:30%">
        <span class="tt">${sensor.toUpperCase()}</span>
        <span class="tv">${lastLabel}</span>
      </div>`;
    void tip;
  };

  const poll = async () => {
    if (!document.getElementById("live-cpu")) {
      if (liveTimer !== undefined) {
        window.clearInterval(liveTimer);
        liveTimer = undefined;
      }
      return;
    }
    if (!isShell()) return;
    try {
      const s = await invoke<LiveStats>("sys.stats");
      setStat(vals["live-cpu"], fmt(s.cpu, "%"));
      setRing("live-cpu", s.cpu);
      setStat(
        subs["live-cpu"],
        s.cpuTemp != null ? `${Math.round(s.cpuTemp)}°C` : "temp n/a"
      );
      setStat(vals["live-gpu"], fmt(s.gpu, "%"));
      setRing("live-gpu", s.gpu);
      const gpuBits: string[] = [];
      if (s.gpuName) gpuBits.push(esc(s.gpuName).slice(0, 22));
      if (s.gpuTemp != null) gpuBits.push(`${Math.round(s.gpuTemp)}°C`);
      setStat(subs["live-gpu"], gpuBits.length ? gpuBits.join(" · ") : "temp n/a");
      setStat(vals["live-ram"], fmt(s.ramPct, "%"));
      setRing("live-ram", s.ramPct);
      setStat(
        subs["live-ram"],
        s.ramGb != null
          ? `${s.ramGb.toFixed(1)} GB used${s.ramTotal ? ` / ${s.ramTotal.toFixed(0)}` : ""}`
          : "used n/a"
      );
      push(cpuH, s.cpu);
      push(gpuH, s.gpu);
      push(ramH, s.ramPct);
      drawLive();
    } catch {
      /* keep last values */
    }
  };

  if (isShell()) {
    void poll();
    liveTimer = window.setInterval(() => void poll(), 1500);
  } else {
    setStat(vals["live-cpu"], "dev");
    setStat(vals["live-gpu"], "dev");
    setStat(vals["live-ram"], "dev");
    drawLive();
  }

  // ---- session chart + runs ----
  const drawChart = () => {
    const box = chartCard.querySelector("#chart")!;
    const runs = getRuns().slice(0, 8).reverse();
    if (runs.length === 0) {
      box.innerHTML = `<div class="empty"><div class="big">📊</div>No operations yet — run the BOOSTER.</div>`;
      return;
    }
    const max = Math.max(...runs.map((r) => r.value), 1);
    const W = 560, H = 150, pad = 26;
    const bw = (W - pad * 2) / runs.length;
    let bars = "";
    runs.forEach((r, i) => {
      const h = Math.max(4, ((H - pad * 2) * r.value) / max);
      const x = pad + i * bw + 4;
      const y = H - pad - h;
      bars += `<g class="bar" style="animation: riseIn 320ms var(--ease-enter) ${i * 55}ms both">
        <rect x="${x.toFixed(1)}" y="${y.toFixed(1)}" width="${(bw - 8).toFixed(1)}" height="${h.toFixed(1)}" rx="4" fill="url(#g)"/>
        <text x="${(x + bw / 2 - 4).toFixed(1)}" y="${(H - 8).toFixed(1)}" fill="var(--text-3)" font-size="10">${esc(r.label.slice(0, 10))}</text>
        <text x="${(x + bw / 2 - 4).toFixed(1)}" y="${(y - 5).toFixed(1)}" fill="var(--text-1)" font-size="11" font-weight="700">${r.value}</text>
      </g>`;
    });
    box.innerHTML = `<svg viewBox="0 0 ${W} ${H}" width="100%" role="img" aria-label="Cleaned items chart">
      <defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0" class="chart-s1"/><stop offset="1" class="chart-s2"/>
      </linearGradient></defs>${bars}</svg>`;
  };

  const drawRuns = () => {
    const box = logCard.querySelector("#runs")!;
    const runs = getRuns().slice(0, 5);
    box.innerHTML =
      runs.length === 0
        ? `<div class="empty"><div class="big">🧹</div>Nothing here yet.</div>`
        : runs
            .map(
              (r) =>
                `<div class="act"><div class="grow"><b>${esc(r.label)}</b><small>${esc(r.time)} — ${r.value} items</small></div></div>`
            )
            .join("");
    refreshScore();
  };

  const refresh = () => {
    drawChart();
    drawRuns();
  };
  const off = onRunsChanged(refresh);
  (root as unknown as { __off?: () => void }).__off = off;
  refresh();

  if (!isShell()) {
    vals["st-res"].textContent = "1920×1080";
    subs["st-res"].textContent = "dev mode";
    return;
  }
  invoke<SysInfo>("sys.getInfo")
    .then((s) => {
      vals["st-res"].textContent = `${s.width}×${s.height}`;
      subs["st-res"].textContent = s.admin ? "admin shell" : "not elevated";
      isAdmin = s.admin;
      refreshScore();
      if (!s.admin) {
        toast({
          title: "Not elevated",
          body: "Restart as admin for registry + deep clean.",
          kind: "info",
        });
      }
    })
    .catch((e: Error) => {
      vals["st-res"].textContent = "n/a";
      toast({ title: "Sys info failed", body: e.message, kind: "err" });
    });
}
