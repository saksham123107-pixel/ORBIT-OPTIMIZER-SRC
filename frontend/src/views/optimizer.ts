import { invoke, onProgress, isShell } from "../lib/ipc";
import { el, esc } from "../lib/format";
import { confirmDlg, notify, toast } from "../ui/shell";
import { recordRun, isPremium } from "../lib/session";
import { openPremiumPrompt } from "./auth";
import type { OpResult } from "../types";

export interface TweakGroup {
  id: string;
  title: string;
  desc: string;
  danger: boolean;
  count: number;
  dynamic: string | null;
  backup: boolean;
  premium: boolean;
  category?: string | null;
  /** "command" | "powershell" for action groups (no registry entries) */
  action?: string;
}

export interface StatusRow {
  id: string;
  applied: boolean;
  matched: number;
  total: number;
}

export interface RegEntry {
  hive: string;
  key: string;
  name: string | null;
  type: string;
  value: unknown;
  cat: string;
  display: string;
}

const CAT_ORDER = [
  "System Tweaks",
  "RAM & Memory",
  "CPU",
  "GPU & Display",
  "Storage",
  "Network",
  "Privacy",
  "Gaming",
  "Power",
  "Input",
  "Windows Update",
  "Security",
  "Bloat",
];

let running = false;
// per-group disabled entry indexes (persist while the view is alive)
const disabled = new Map<string, Set<number>>();

// last full group list from paint() (for per-card Apply)
let allGroups: TweakGroup[] = [];

/** Topic-based card bucket: groups every tweak into one of the topic cards. */
function catKey(g: TweakGroup): string {
  const hay = `${g.title} ${g.category ?? ""} ${g.id}`.toLowerCase();
  if (/\bgame|dvr|fullscreen|esports|anti-cheat/.test(hay)) return "Gaming";
  if (/\bmouse\b|\bkeyboard\b|scroll|pointer|touchpad|\binput\b|\bdpi\b|\bhid\b/.test(hay))
    return "Input";
  if (/\bdisk\b|\bdrive\b|ssd|trim|defrag|storage|temp|prefetch|component store|delivery optimization|cleanup/.test(hay))
    return "Storage";
  if (/network|ipv6|dns|winsock|arp|adapter|tcp|internet|bandwidth|qos|mtu|nagle|lease/.test(hay))
    return "Network";
  if (/privacy|telemetry|tracking|activity|advertis|\bads\b|cortana|location|diagnostic|camera|microphone|block|feedback/.test(hay))
    return "Privacy";
  if (/windows update|\bupdate\b|upgrade|wuauserv/.test(hay)) return "Windows Update";
  if (/defender|bitlocker|firewall|uac|smartscreen|security|virus|encrypt|exploit|credential/.test(hay))
    return "Security";
  if (/\bcpu\b|processor|core parking|thread|schedul|priority|hyper-?thread/.test(hay))
    return "CPU";
  if (/gpu|display|monitor|graphics|resolution|hdr|vsync|g-sync|nvidia|radeon|vram/.test(hay))
    return "GPU & Display";
  if (/power|powercfg|sleep|standby|hibernat|suspend|aspm|away|energy|ultimate|profile|\bplan\b|boost|usb/.test(hay))
    return "Power";
  if (/\bram\b|memory|svchost|pagefile|paging|cache|compression|\bmem\b/.test(hay))
    return "RAM & Memory";
  if (/bloat|\bstore\b|edge\b|onedrive|appx|\bremove\b|\bapps?\b|widget/.test(hay))
    return "Bloat";
  return "System Tweaks";
}

function gRiskySection(cat: string): boolean {
  return cat === "Power" || cat === "Privacy" || cat === "Security" || cat === "Windows Update";
}

function sortCats(cats: string[]): string[] {
  return [...cats].sort((a, b) => {
    const ia = CAT_ORDER.indexOf(a);
    const ib = CAT_ORDER.indexOf(b);
    if (ia >= 0 && ib >= 0) return ia - ib;
    if (ia >= 0) return -1;
    if (ib >= 0) return 1;
    return String(a || "").localeCompare(String(b || ""));
  });
}

export function renderOptimizer(root: HTMLElement): void {
  root.innerHTML = "";
  let tab: "free" | "paid" = "free";
  const safe = el("div", "card glass card-icon");
  safe.dataset.search = "restore point safety backup revert snapshot";
  safe.innerHTML = `<div class="card-ihead">
      <span class="icon-tile"><i class="ph ph-shield-check"></i></span>
      <h3>Safety first</h3>
    </div>
    <p class="sub">Create a Windows restore point before changing anything (needs admin). Every apply also snapshots first so you can Revert.</p>
    <div class="row">
      <button class="btn primary" id="opt-rp" type="button">Create restore point</button>
    </div>`;
  root.appendChild(safe);
  safe.querySelector("#opt-rp")!.addEventListener("click", async () => {
    const b = safe.querySelector("#opt-rp") as HTMLButtonElement;
    if (!isShell()) {
      toast({ title: "Dev mode", body: "Run inside the shell.", kind: "info" });
      return;
    }
    b.disabled = true;
    try {
      const r = await invoke<OpResult>("sys.restorePoint");
      toast({ title: "Restore point", body: r.message, kind: "ok" });
    } catch (e) {
      toast({ title: "Restore point", body: e instanceof Error ? e.message : String(e), kind: "err" });
    } finally {
      b.disabled = false;
    }
  });
  const card = el("div", "card glass card-icon");
  card.innerHTML = `<div class="card-ihead">
      <span class="icon-tile"><i class="ph ph-sliders-horizontal"></i></span>
      <h3>Tweaks</h3>
    </div>
    <p class="sub">PRIMEx checklist. Check any packs, then Apply. Every apply snapshots first so you can Revert.</p>
    <div class="opt-tabs" role="tablist" aria-label="Tweak pricing">
      <button type="button" class="opt-tab active" data-tab="free" role="tab" aria-selected="true">Free <span class="cnt" id="zg-free-n">0</span></button>
      <button type="button" class="opt-tab" data-tab="paid" role="tab" aria-selected="false">Paid <span class="cnt" id="zg-paid-n">0</span></button>
    </div>
    <div class="row" style="margin-bottom:12px">
      <button class="btn sm" id="zg-all">Select all</button>
      <button class="btn sm ghost" id="zg-none">Clear</button>
      <div class="spacer"></div>
      <span class="stat" id="zg-selcount">0 selected</span>
      <button class="btn sm primary" id="zg-apply">Apply selected</button>
    </div>
    <div id="zg-list" class="stagger"></div>
    <div style="margin-top:14px"><div class="pbar"><div id="zg-bar"></div></div>
    <p class="sub" id="zg-msg" style="margin-top:8px">Idle. Check tweaks to build your selection.</p></div>`;
  root.appendChild(card);

  const list = card.querySelector("#zg-list")!;
  const bar = card.querySelector("#zg-bar") as HTMLElement;
  const msg = card.querySelector("#zg-msg")!;

  const paint = async () => {
    let groups: TweakGroup[] = [];
    const status = new Map<string, { applied: boolean; total: number }>();
    if (isShell()) {
      try {
        groups = await invoke<TweakGroup[]>("tweaks.groups");
      } catch (e) {
        toast({ title: "Groups failed", body: String(e), kind: "err" });
      }
      try {
        const rows = await invoke<StatusRow[]>("tweaks.status");
        for (const s of rows) status.set(s.id, { applied: s.applied, total: s.total });
      } catch {
        /* host without live registry probe — Apply buttons stay as-is */
      }
    }
    const premium = isPremium();
    groups = groups.filter((g) => g.id !== "mouse");
    allGroups = groups.slice();
    const freeN = groups.filter((g) => !g.premium).length;
    const freeEl = card.querySelector("#zg-free-n");
    const paidEl = card.querySelector("#zg-paid-n");
    if (freeEl) freeEl.textContent = String(freeN);
    if (paidEl) paidEl.textContent = String(groups.length - freeN);
    groups = groups.filter((g) => (tab === "paid" ? !!g.premium : !g.premium));
    list.innerHTML =
      groups.length === 0
        ? `<div class="empty"><div class="big">📦</div>${
            isShell()
              ? tab === "paid"
                ? "No paid tweaks."
                : "No free tweaks."
              : "Dev mode — run inside the shell."
          }</div>`
        : "";

    const byCat = new Map<string, TweakGroup[]>();
    for (const g of groups) {
      const k = catKey(g);
      if (!byCat.has(k)) byCat.set(k, []);
      byCat.get(k)!.push(g);
    }

    let delay = 0;
    for (const cat of sortCats([...byCat.keys()])) {
      const section = el("div", "twk-section");
      section.dataset.cat = cat;
      section.dataset.search = cat;
      const risky = gRiskySection(cat);
      section.innerHTML = `
        <div class="twk-head${risky ? " risky" : ""}">
          <button type="button" class="twk-toggle" aria-expanded="true"
                  aria-label="Toggle ${esc(cat)}">
            <span class="caret">▾</span><span class="twk-title">${esc(cat)}</span>
          </button>
          <label class="twk-catsel">
            <input type="checkbox" class="chk" data-cat-sel="${esc(cat)}"
                   aria-label="Select all in ${esc(cat)}" />
          </label>
        </div>
        <div class="twk-body"><div class="tweak-grid"></div></div>`;
      const body = section.querySelector(".tweak-grid") as HTMLElement;

      for (const g of byCat.get(cat)!) {
        const locked = g.premium && !premium;
        const st = status.get(g.id);
        const applied = !!(st && st.applied && st.total > 0);
        const card = el("label", "tweak-card");
        card.style.animationDelay = `${delay * 40}ms`;
        delay++;
        card.dataset.search = `${g.title} ${g.desc} ${g.id} ${cat}`;
        if (locked) card.classList.add("locked");
        if (applied) card.classList.add("applied");
        const warn = g.danger
          ? `<span class="chip warn-badge">${g.count} warning${g.count === 1 ? "" : "s"}</span>`
          : "";
        const lock = locked
          ? '<span class="chip prem" title="PRIMEx Premium">🔒 PREMIUM</span>'
          : "";
        const done = applied
          ? '<span class="chip applied-badge" title="Registry already matches the target value">✓ Already applied</span>'
          : "";
        const applyBtn = applied
          ? ""
          : `<button type="button" class="btn sm primary twk-apply" data-apply="${esc(
              g.id
            )}" ${locked ? "disabled" : ""} title="Apply this tweak">Apply</button>`;
        const rev = g.backup
          ? `<button type="button" class="btn sm ghost twk-rev" data-revert="${esc(
              g.id
            )}" title="Restore backup">Revert</button>`
          : "";
        const icon = applied
          ? "ph ph-check-circle"
          : g.danger
            ? "ph ph-warning"
            : g.premium
              ? "ph ph-lock"
              : g.action
                ? "ph ph-terminal-window"
                : "ph ph-sliders-horizontal";
        const unit = g.action ? "action" : g.count === 1 ? "key" : "keys";
        const inspect = g.action
          ? ""
          : `<button type="button" class="btn sm ghost twk-more" data-detail="${esc(
              g.id
            )}" title="Inspect registry values">Inspect</button>`;
        const chk = applied
          ? ""
          : `<input type="checkbox" class="chk" data-g="${esc(g.id)}"
                   aria-label="Select ${esc(g.title)}"
                   ${locked ? "disabled" : ""} />`;
        card.innerHTML = `
          <div class="tweak-head">
            <span class="icon-tile${g.danger ? " amber" : ""}"><i class="${icon}"></i></span>
            <div class="tweak-titles">
              <h4>${esc(g.title)}</h4>
              <p class="tweak-desc">${esc(g.desc)}</p>
            </div>
          </div>
          <div class="tweak-foot">
            ${warn}${lock}${done}
            <span class="stat">${g.count} ${unit}</span>
            <span class="spacer"></span>
            ${applyBtn}
            ${rev}
            ${inspect}
            ${chk}
          </div>`;
        body.appendChild(card);
      }

      section.querySelector(".twk-toggle")!.addEventListener("click", () => {
        const open = section.classList.toggle("collapsed");
        const btn = section.querySelector(".twk-toggle") as HTMLElement;
        btn.setAttribute("aria-expanded", String(!open));
      });

      list.appendChild(section);
    }

    // highlight selected cards + keep checkbox state in sync
    const syncCards = () => {
      let n = 0;
      list.querySelectorAll<HTMLInputElement>("input[data-g]").forEach((c) => {
        const card = c.closest(".tweak-card") as HTMLElement | null;
        if (card) card.classList.toggle("selected", c.checked);
        if (c.checked) n++;
      });
      const sc = card.querySelector("#zg-selcount");
      if (sc) sc.textContent = `${n} selected`;
    };
    list.addEventListener("change", syncCards);
    syncCards();

    list.querySelectorAll("[data-revert]").forEach((b) =>
      b.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        void onRevert((b as HTMLElement).dataset.revert!);
      })
    );
    list.querySelectorAll("[data-apply]").forEach((b) =>
      b.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        void onApplyOne((b as HTMLElement).dataset.apply!);
      })
    );
      list.querySelectorAll("[data-detail]").forEach((b) =>
      b.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        const id = (b as HTMLElement).dataset.detail!;
        const g = groups.find((x) => x.id === id);
        if (!g) return;
        if (g.premium && !premium) {
          openPremiumPrompt();
          return;
        }
        void openDetail(g);
      })
    );
    list.querySelectorAll<HTMLInputElement>("input[data-g]").forEach((c) =>
      c.addEventListener("click", (e) => e.stopPropagation())
    );
    list.querySelectorAll<HTMLInputElement>("input[data-cat-sel]").forEach((c) =>
      c.addEventListener("change", () => {
        const section = c.closest(".twk-section") as HTMLElement;
        section
          .querySelectorAll<HTMLInputElement>("input[data-g]:not(:disabled)")
          .forEach((x) => {
            x.checked = c.checked;
          });
      })
    );
    // keep category header checkbox in sync
    const syncCat = () => {
      list.querySelectorAll<HTMLElement>(".twk-section").forEach((section) => {
        const boxes = [
          ...section.querySelectorAll<HTMLInputElement>("input[data-g]"),
        ];
        const sel = section.querySelector<HTMLInputElement>(
          "input[data-cat-sel]"
        );
        if (!sel || boxes.length === 0) return;
        const on = boxes.filter((b) => b.checked).length;
        sel.checked = on === boxes.length && boxes.length > 0;
        sel.indeterminate = on > 0 && on < boxes.length;
      });
    };
    list.addEventListener("change", syncCat);
    syncCat();
    syncCards();
  };
  void paint();

  card.querySelectorAll<HTMLButtonElement>(".opt-tab").forEach((b) =>
    b.addEventListener("click", () => {
      const t = b.dataset.tab as "free" | "paid" | undefined;
      if (!t || t === tab) return;
      tab = t;
      card.querySelectorAll<HTMLButtonElement>(".opt-tab").forEach((x) => {
        const on = x.dataset.tab === tab;
        x.classList.toggle("active", on);
        x.setAttribute("aria-selected", String(on));
      });
      void paint();
    })
  );

  card.querySelector("#zg-all")!.addEventListener("click", () => {
    list
      .querySelectorAll<HTMLInputElement>("input[data-g]:not(:disabled)")
      .forEach((c) => {
        c.checked = true;
      });
    list.querySelectorAll<HTMLInputElement>("input[data-cat-sel]").forEach((c) => {
      c.checked = true;
      c.indeterminate = false;
    });
    list.dispatchEvent(new Event("change"));
  });
  card.querySelector("#zg-none")!.addEventListener("click", () => {
    list.querySelectorAll<HTMLInputElement>("input[data-g]").forEach((c) => (c.checked = false));
    list.querySelectorAll<HTMLInputElement>("input[data-cat-sel]").forEach((c) => {
      c.checked = false;
      c.indeterminate = false;
    });
    list.dispatchEvent(new Event("change"));
  });
  card.querySelector("#zg-apply")!.addEventListener("click", async () => {
    const ids = [...list.querySelectorAll<HTMLInputElement>("input[data-g]:checked")].map(
      (c) => c.dataset.g!
    );
    if (ids.length === 0 || running) return;
    if (!isShell()) {
      toast({ title: "Dev mode", body: "Run inside the shell.", kind: "info" });
      return;
    }
    const risky = ids.some(
      (id) => id === "primex-full" || id.startsWith("primex-full::")
    );
    if (risky) {
      const ok = await confirmDlg(
        "Apply registry tweaks?",
        `${ids.length} selection(s) will be written. A backup is taken first; reboot after. Continue?`
      );
      if (!ok) return;
    }
    running = true;
    try {
      for (const id of ids) {
        const r = await invoke<OpResult>("tweaks.apply", { id });
        recordRun(`tweaks:${id}`, 1);
        notify("Optimizer", r.message);
        toast({ title: "Optimizer", body: r.message, kind: "ok" });
      }
      await paint();
    } catch (e) {
      const msg = String(e);
      if (/PREMIUM::/i.test(msg)) {
        openPremiumPrompt();
        toast({
          title: "Premium required",
          body: "This pack requires PRIMEx Premium.",
          kind: "err",
        });
      } else {
        toast({ title: "Optimizer", body: msg, kind: "err" });
      }
    } finally {
      running = false;
    }
  });

  async function onApplyOne(id: string): Promise<void> {
    if (running) return;
    if (!isShell()) {
      toast({ title: "Dev mode", body: "Run inside the shell.", kind: "info" });
      return;
    }
    const g = allGroups.find((x) => x.id === id);
    if (!g) return;
    if (g.premium && !isPremium()) {
      openPremiumPrompt();
      return;
    }
    const risky =
      g.danger || id === "primex-full" || id.startsWith("primex-full::");
    if (risky) {
      const ok = await confirmDlg(
        "Apply this tweak?",
        `${g.title} will be written. A backup is taken first; reboot if prompted. Continue?`
      );
      if (!ok) return;
    }
    running = true;
    try {
      const r = await invoke<OpResult>("tweaks.apply", { id });
      recordRun(`tweaks:${id}`, 1);
      notify("Optimizer", r.message);
      toast({ title: g.title, body: r.message, kind: "ok" });
      await paint();
    } catch (e) {
      const msg = String(e);
      if (/PREMIUM::/i.test(msg)) {
        openPremiumPrompt();
        toast({
          title: "Premium required",
          body: "This pack requires PRIMEx Premium.",
          kind: "err",
        });
      } else {
        toast({ title: g.title, body: msg, kind: "err" });
      }
    } finally {
      running = false;
    }
  }

  async function onRevert(id: string): Promise<void> {
    if (running || !isShell()) return;
    running = true;
    try {
      const r = await invoke<OpResult>("tweaks.revert", { id });
      toast({ title: "Revert", body: r.message, kind: "ok" });
      await paint();
    } catch (e) {
      toast({ title: "Revert", body: String(e), kind: "err" });
    } finally {
      running = false;
    }
  }

  async function openDetail(g: TweakGroup): Promise<void> {
    if (!isShell()) {
      toast({ title: "Dev mode", body: "Run inside the shell.", kind: "info" });
      return;
    }
    let entries: RegEntry[] = [];
    try {
      entries = await invoke<RegEntry[]>("tweaks.entries", { id: g.id });
    } catch (e) {
      const msg = String(e);
      if (/PREMIUM::/i.test(msg)) {
        openPremiumPrompt();
        toast({
          title: "Premium required",
          body: "This pack requires PRIMEx Premium.",
          kind: "err",
        });
      } else {
        toast({ title: g.title, body: msg, kind: "err" });
      }
      return;
    }
    if (!disabled.has(g.id)) {
      // nothing pre-ticked: user enables explicitly
      disabled.set(g.id, new Set(entries.map((_, i) => i)));
    }
    const off = disabled.get(g.id)!;

    const ov = el("div", "overlay");
    ov.innerHTML = `
      <div class="modal glass" role="dialog" aria-modal="true" aria-label="${esc(
        g.title
      )} values"
           style="width:680px;max-width:94vw">
        <h3>${esc(g.title)} <span class="chip">${entries.length} values</span></h3>
        <p class="sub">${esc(g.desc)}</p>
        <div class="reglist" role="group" aria-label="Registry values"></div>
        <div class="row" style="justify-content:flex-end;margin-top:14px;gap:10px">
          <button class="btn sm ghost" data-x="all">All on</button>
          <button class="btn sm ghost" data-x="none">All off</button>
          <div class="spacer"></div>
          <button class="btn sm ghost" data-x="close">Close</button>
          <button class="btn sm primary" data-x="go">Apply enabled</button>
        </div>
      </div>`;
    const box = ov.querySelector(".reglist")!;
    const drawRows = () => {
      box.innerHTML = "";
      let lastCat = "";
      entries.forEach((en, i) => {
        const cat = en.cat || "MISC";
        if (cat !== lastCat) {
          lastCat = cat;
          const inCat = entries
            .map((e, j) => ({ e, j }))
            .filter((x) => (x.e.cat || "MISC") === cat);
          const onCount = inCat.filter((x) => !off.has(x.j)).length;
          const h = document.createElement("div");
          h.className = "regcat";
          h.innerHTML = `<label><input type="checkbox" class="chk" data-cat="${esc(
            cat
          )}" ${onCount === inCat.length ? "checked" : ""} /></label><span>${esc(
            cat
          )}</span><span class="stat">${onCount}/${inCat.length}</span>`;
          box.appendChild(h);
        }
        const r = el("label", "regrow");
        const path = `${en.hive}\\${en.key}\\${en.name ?? "@"}`;
        r.innerHTML = `
          <input type="checkbox" class="chk" data-i="${i}" ${off.has(i) ? "" : "checked"} />
          <span class="regpath">${esc(path)}</span>
          <span class="regval">${esc(String(en.display))}</span>`;
        box.appendChild(r);
      });
      box.querySelectorAll<HTMLInputElement>("input[data-i]").forEach((c) =>
        c.addEventListener("change", () => {
          const i = Number(c.dataset.i);
          if (c.checked) off.delete(i);
          else off.add(i);
          syncGo();
        })
      );
      box.querySelectorAll<HTMLInputElement>("input[data-cat]").forEach((c) =>
        c.addEventListener("change", () => {
          const cat = c.dataset.cat!;
          entries.forEach((en, i) => {
            if ((en.cat || "MISC") !== cat) return;
            if (c.checked) off.delete(i);
            else off.add(i);
          });
          drawRows();
          syncGo();
        })
      );
    };
    const goBtn = ov.querySelector('[data-x="go"]') as HTMLButtonElement;
    const syncGo = () => {
      const n = entries.length - off.size;
      goBtn.textContent = `Apply enabled (${n})`;
    };
    drawRows();
    syncGo();

    const close = () => {
      ov.remove();
      document.removeEventListener("keydown", onKey);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") close();
    };
    document.addEventListener("keydown", onKey);
    ov.addEventListener("mousedown", (e) => {
      if (e.target === ov) close();
    });
    ov.querySelector('[data-x="close"]')!.addEventListener("click", close);
    ov.querySelector('[data-x="all"]')!.addEventListener("click", () => {
      off.clear();
      drawRows();
      syncGo();
    });
    ov.querySelector('[data-x="none"]')!.addEventListener("click", () => {
      entries.forEach((_, i) => off.add(i));
      drawRows();
      syncGo();
    });
    ov.querySelector('[data-x="go"]')!.addEventListener("click", async () => {
      const picked = entries.filter((_, i) => !off.has(i));
      if (picked.length === 0 || running) return;
      close();
      running = true;
      try {
        const r = await invoke<OpResult>("tweaks.applyEntries", {
          id: g.id,
          entries: picked,
        });
        recordRun(`tweaks:${g.id}*`, picked.length);
        notify("Optimizer", r.message);
        toast({ title: g.title, body: r.message, kind: "ok" });
        await paint();
      } catch (e) {
        toast({ title: g.title, body: String(e), kind: "err" });
      } finally {
        running = false;
      }
    });
    document.body.appendChild(ov);
    (ov.querySelector('[data-x="go"]') as HTMLButtonElement).focus();
  }

  onProgress((p) => {
    bar.style.width = `${p.pct}%`;
    msg.textContent = p.msg;
  });
}
