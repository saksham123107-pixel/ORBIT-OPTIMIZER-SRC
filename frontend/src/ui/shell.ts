import { invoke, isShell } from "../lib/ipc";
import { el, esc } from "../lib/format";
import { getSession } from "../lib/session";
import { t, getLang, setLang } from "../i18n";
import type { ToastMsg, ViewId, NotifyItem, AuthSession } from "../types";

let notifies: NotifyItem[] = [];
let notifySeq = 1;
let activeView: ViewId = "dashboard";
let onNav: (v: ViewId) => void = () => {};

interface NavItem {
  id: ViewId;
  icon: string;
  label: string;
  group: string;
  crumb: string;
  title: string;
  sub: string;
}

const NAV: NavItem[] = [
  { id: "dashboard", icon: "ph ph-house", label: "Dashboard", group: "General", crumb: "Home", title: "Dashboard", sub: "System overview, live load, and recent activity." },
  { id: "aimoptimze", icon: "ph ph-crosshair", label: "AIM", group: "Tweaks", crumb: "AIM", title: "AIM Optimizer", sub: "Mouse and input latency tweaks for competitive play." },
  { id: "optimizer", icon: "ph ph-sliders-horizontal", label: "Optimizer", group: "Tweaks", crumb: "Optimizer", title: "Optimizer", sub: "Curated registry packs with backup and revert." },
  { id: "debloat", icon: "ph ph-trash", label: "Debloat", group: "Tweaks", crumb: "Debloat", title: "Debloat", sub: "Remove preinstalled junk safely with restore points." },
  { id: "services", icon: "ph ph-gears", label: "Services", group: "Tweaks", crumb: "Services", title: "Services", sub: "Disable or tune Windows services for performance." },
  { id: "settings", icon: "ph ph-gear", label: "Settings", group: "General", crumb: "Settings", title: "Settings", sub: "Appearance, motion, and about this build." },
];

function meta(v: ViewId): NavItem {
  return NAV.find((n) => n.id === v) ?? NAV[0];
}

/** Show/hide a nav badge (e.g. warning count) for a view. */
export function setNavBadge(v: ViewId, text: string | null): void {
  const b = document.querySelector<HTMLElement>(`.nav-badge[data-badge="${v}"]`);
  if (!b) return;
  if (!text) {
    b.hidden = true;
    b.textContent = "";
  } else {
    b.hidden = false;
    b.textContent = text;
  }
}

function setPfp(box: HTMLElement, avatar: string | undefined, letter: string): void {
  box.textContent = "";
  if (avatar) {
    const img = document.createElement("img");
    img.className = "pfp";
    img.src = avatar;
    img.alt = "";
    img.addEventListener("error", () => {
      box.classList.remove("has-pfp");
      box.textContent = letter;
    });
    box.classList.add("has-pfp");
    box.appendChild(img);
  } else {
    box.classList.remove("has-pfp");
    box.textContent = letter;
  }
}

function applyShellAvatar(s: AuthSession | null): void {
  const letter = (s?.username?.[0] ?? "P").toUpperCase();
  const mark = document.querySelector<HTMLElement>(".brand .mark");
  const av = document.querySelector<HTMLElement>(".topbar .avatar");
  if (mark) setPfp(mark, s?.avatar, "P");
  if (av) setPfp(av, s?.avatar, letter);
  const chip = document.getElementById("pro-chip");
  if (chip) chip.hidden = !(s && s.valid && s.tier === "Lifetime");
}

export function mountShell(navigate: (v: ViewId) => void): void {
  onNav = navigate;
  const app = document.getElementById("app")!;
  app.innerHTML = "";
  app.appendChild(Object.assign(document.createElement("div"), { className: "aurora" }));

  const bar = el("div", "titlebar glass");
  bar.innerHTML = `
    <div class="drag" id="drag">
      <span class="dot"></span><span id="drag-title">${t("app.title")}</span>
    </div>
    <div class="winbtns">
      <button class="winbtn" id="w-min" aria-label="Minimize"><i class="ph ph-minus"></i></button>
      <button class="winbtn" id="w-max" aria-label="Maximize or restore"><i class="ph ph-square"></i></button>
      <button class="winbtn close" id="w-close" aria-label="Close"><i class="ph ph-x"></i></button>
    </div>`;
  app.appendChild(bar);

  const layout = el("div", "layout");
  layout.appendChild(buildSidebar());
  const main = el("div", "main");
  main.appendChild(buildTopbar());
  const view = el("div", "view");
  view.id = "view";
  main.appendChild(view);
  layout.appendChild(main);
  app.appendChild(layout);

  const toasts = el("div");
  toasts.id = "toasts";
  app.appendChild(toasts);

  const drag = document.getElementById("drag")!;
  const onMove = (e: MouseEvent) => {
    if (e.buttons & 1) invoke("win.dragmove", { x: e.screenX, y: e.screenY }).catch(() => {});
  };
  const offMove = () => window.removeEventListener("mousemove", onMove);
  drag.addEventListener("mousedown", (e) => {
    if (e.button !== 0) return;
    invoke("win.dragstart", { x: e.screenX, y: e.screenY }).catch(() => {});
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", offMove, { once: true });
  });
  drag.addEventListener("dblclick", () => invoke("win.toggle").catch(() => {}));
  document.getElementById("w-min")!.addEventListener("click", () => invoke("win.min").catch(() => {}));
  document.getElementById("w-max")!.addEventListener("click", () => invoke("win.toggle").catch(() => {}));
  document.getElementById("w-close")!.addEventListener("click", () => invoke("win.close").catch(() => {}));

  document.getElementById("bell")!.addEventListener("click", () => {
    notifies.forEach((n) => (n.read = true));
    renderBell();
    toast({ title: "Notifications", body: "All caught up.", kind: "info" });
  });

  applyShellAvatar(getSession());
}

function buildSidebar(): HTMLElement {
  const side = el("aside", "sidebar glass");
  const brand = el("div", "brand");
  brand.innerHTML = `<div class="mark"><img src="logo.png" alt="" /></div><div><b>ORBIT</b><small>Optimizer</small></div>`;
  side.appendChild(brand);

  const nav = el("nav", "nav");
  nav.setAttribute("aria-label", "Primary");
  const pill = el("div");
  pill.id = "nav-pill";
  nav.appendChild(pill);

  const groups: string[] = [];
  for (const n of NAV) if (!groups.includes(n.group)) groups.push(n.group);

  for (const g of groups) {
    const lab = el("div", "nav-group");
    lab.dataset.group = g;
    lab.textContent = t(`group.${g.toLowerCase()}`);
    nav.appendChild(lab);
    for (const n of NAV.filter((x) => x.group === g)) {
      const b = el("button", n.id === activeView ? "active" : "");
      b.dataset.view = n.id;
      b.innerHTML = `<span class="ic"><i class="${n.icon}"></i></span><span>${t(`nav.${n.id}`)}</span><span class="nav-badge" data-badge="${n.id}" hidden></span>`;
      b.setAttribute("aria-label", t(`nav.${n.id}`));
      b.addEventListener("click", () => setView(n.id));
      nav.appendChild(b);
    }
  }
  side.appendChild(nav);

  const foot = el("div", "side-foot");
  foot.innerHTML = `
    <div class="side-lang">
      <i class="ph ph-translate"></i>
      <select id="lang-sel" class="lang-sel" aria-label="Language">
        <option value="en">English</option>
        <option value="es">Español</option>
        <option value="de">Deutsch</option>
        <option value="fr">Français</option>
        <option value="pt">Português (Brasil)</option>
        <option value="ur">اردو</option>
        <option value="bn">বাংলা</option>
        <option value="th">ไทย</option>
        <option value="ar">العربية</option>
      </select>
    </div>
    <div class="side-dash"></div>
    <div class="socrow">
      <button class="socbtn imgbtn" id="soc-yt" aria-label="YouTube" title="YouTube">
        <img src="social/youtube-glass.svg" alt="" />
      </button>
      <button class="socbtn imgbtn" id="soc-dc" aria-label="Discord" title="Discord">
        <img src="social/discord-glass.svg" alt="" />
      </button>
    </div>
    <div class="side-ver">v1.0 · PRIMEx</div>`;
  foot.querySelectorAll<HTMLImageElement>(".socbtn img").forEach((img) => {
    img.addEventListener("error", () => img.remove());
  });
  foot.querySelector("#soc-yt")!.addEventListener("click", () => {
    invoke("sys.openUrl", { url: "https://www.youtube.com/@PRIME_H.4X/videos" }).catch(() => {});
  });
  foot.querySelector("#soc-dc")!.addEventListener("click", () => {
    invoke("sys.openUrl", { url: "https://discord.gg/drxPk4htSd" }).catch(() => {});
  });
  const lang = foot.querySelector<HTMLSelectElement>("#lang-sel")!;
  lang.value = getLang();
  lang.addEventListener("change", () => {
    setLang(lang.value);
    applyShellLang();
    onNav(activeView);
    toast({
      title: t("lang.title"),
      body: t("lang.saved").replace("{code}", lang.value.toUpperCase()),
      kind: "info",
    });
  });
  side.appendChild(foot);

  const userSlot = el("div");
  userSlot.id = "usercard-slot";
  side.appendChild(userSlot);

  requestAnimationFrame(movePill);
  return side;
}

export function setView(v: ViewId): void {
  activeView = v;  document.querySelectorAll(".nav button").forEach((b) => {
    const on = (b as HTMLElement).dataset.view === v;
    b.classList.toggle("active", on);
    b.setAttribute("aria-current", on ? "page" : "false");
  });
  updateBreadcrumb(v);
  movePill();
  onNav(v);
}

/** Re-apply translated chrome after a language change. */
export function applyShellLang(): void {
  const drag = document.getElementById("drag-title");
  if (drag) drag.textContent = t("app.title");
  document.querySelectorAll<HTMLElement>(".nav-group[data-group]").forEach((g) => {
    g.textContent = t(`group.${g.dataset.group!.toLowerCase()}`);
  });
  document.querySelectorAll<HTMLElement>(".nav button[data-view]").forEach((b) => {
    const label = t(`nav.${b.dataset.view!}`);
    const span = b.querySelector<HTMLElement>("span:nth-child(2)");
    if (span) span.textContent = label;
    b.setAttribute("aria-label", label);
  });
  const q = document.getElementById("q");
  if (q) (q as HTMLInputElement).placeholder = t("search");
  const bell = document.getElementById("bell");
  if (bell) bell.setAttribute("aria-label", t("bell"));
  const sel = document.getElementById("lang-sel") as HTMLSelectElement | null;
  if (sel) sel.value = getLang();
  updateBreadcrumb(activeView);
}

function updateBreadcrumb(v: ViewId): void {
  const box = document.getElementById("crumbs");
  if (!box) return;
  const m = meta(v);
  const home = t("crumb.home");
  if (v === "dashboard") {
    box.innerHTML = `<i class="ph ph-house"></i><span class="crumb-now">${esc(m.crumb === "Home" ? home : t(`nav.${v}`))}</span>`;
  } else if (v === "settings") {
    box.innerHTML = `<i class="ph ph-house"></i><span class="crumb">${esc(home)}</span><span class="crumb-sep">/</span><span class="crumb-now">${esc(t("nav.settings"))}</span>`;
  } else {
    box.innerHTML = `<i class="ph ph-house"></i><span class="crumb">${esc(home)}</span><span class="crumb-sep">/</span><span class="crumb">${esc(t("group.tweaks"))}</span><span class="crumb-sep">/</span><span class="crumb-now">${esc(t(`nav.${v}`))}</span>`;
  }
}

/** Prepend page title + subtitle after a view renders. */
export function injectPageHead(v: ViewId, root: HTMLElement): void {
  const head = el("header", "page-head");
  head.innerHTML = `<div class="page-titles"><h1>${esc(t(`title.${v}`))}</h1><p>${esc(t(`sub.${v}`))}</p></div><div class="page-actions"></div>`;
  if (v === "dashboard" || v === "optimizer") {
    const bar = head.querySelector<HTMLElement>(".page-actions")!;
    bar.classList.add("qa-row");
    bar.innerHTML = `<button class="qa-chip" id="qa-rp" type="button"><i class="ph ph-shield-check"></i><span>Create restore point</span></button>`;
    bar.querySelector("#qa-rp")!.addEventListener("click", async () => {
      if (!isShell()) {
        toast({ title: "Dev mode", body: "Run inside the shell.", kind: "info" });
        return;
      }
      try {
        const r = await invoke<{ message: string }>("sys.restorePoint");
        toast({ title: "Restore point", body: r.message, kind: "ok" });
      } catch (e) {
        toast({ title: "Restore point", body: String(e), kind: "err" });
      }
    });
  }
  root.insertBefore(head, root.firstChild);
}

export function pageActions(root: HTMLElement): HTMLElement | null {
  return root.querySelector<HTMLElement>(".page-actions");
}

function movePill(): void {
  const active = document.querySelector<HTMLElement>(".nav button.active");
  const pill = document.getElementById("nav-pill");
  if (!active || !pill) return;
  pill.style.top = `${active.offsetTop}px`;
  pill.style.height = `${active.offsetHeight}px`;
}
window.addEventListener("resize", movePill);

function buildTopbar(): HTMLElement {
  const top = el("div", "topbar glass");
  const crumbs = el("div", "crumbs");
  crumbs.id = "crumbs";
  top.appendChild(crumbs);

  const search = el("div", "search");
  search.innerHTML = `<i class="ph ph-magnifying-glass"></i><input id="q" type="text" placeholder="${esc(t("search"))}" aria-label="Search" />`;
  top.appendChild(search);

  const pro = el("span", "pro-chip");
  pro.id = "pro-chip";
  pro.hidden = true;
  pro.title = "ORBIT Lifetime";
  pro.innerHTML = `<i class="ph ph-crown-simple"></i><span>PRO</span>`;
  top.appendChild(pro);

  const bell = el("button", "iconbtn");
  bell.id = "bell";
  bell.setAttribute("aria-label", t("bell"));
  bell.innerHTML = `<i class="ph ph-bell"></i><span class="badge hidden" id="bell-n">0</span>`;
  top.appendChild(bell);

  const av = el("div", "avatar");
  av.textContent = "O";
  av.title = "ORBIT";
  top.appendChild(av);
  applyShellAvatar(getSession());
  updateBreadcrumb(activeView);

  search.querySelector("input")!.addEventListener("input", (e) => {
    const q = (e.target as HTMLInputElement).value.toLowerCase();
    document.querySelectorAll<HTMLElement>("[data-search]").forEach((n) => {
      n.style.display = n.dataset.search!.toLowerCase().includes(q) ? "" : "none";
    });
    document.querySelectorAll<HTMLElement>(".twk-section").forEach((sec) => {
      const rows = [...sec.querySelectorAll<HTMLElement>(".tweak-card, .twk-row")];
      if (rows.length === 0) return;
      const any = rows.some((r) => r.style.display !== "none");
      sec.style.display = any || sec.dataset.cat!.toLowerCase().includes(q) ? "" : "none";
    });
  });
  return top;
}

export function toast(t: ToastMsg): void {
  let box = document.getElementById("toasts");
  if (!box) {
    box = document.createElement("div");
    box.id = "toasts";
    document.body.appendChild(box);
  }
  const d = el("div", `toast glass ${t.kind}`);
  d.setAttribute("role", "status");
  d.innerHTML = `<span class="dot"></span><div><b>${esc(t.title)}</b>${
    t.body ? `<small>${esc(t.body)}</small>` : ""
  }</div>`;
  box.appendChild(d);
  requestAnimationFrame(() => d.classList.add("show"));
  setTimeout(() => {
    d.classList.remove("show");
    setTimeout(() => d.remove(), 260);
  }, 4200);
}

export function notify(title: string, body: string): void {
  notifies.unshift({
    id: notifySeq++,
    title,
    body,
    time: new Date().toLocaleTimeString("en-GB"),
    read: false,
  });
  renderBell();
}

function renderBell(): void {
  const n = document.getElementById("bell-n");
  if (!n) return;
  const unread = notifies.filter((x) => !x.read).length;
  n.textContent = String(unread);
  n.classList.toggle("hidden", unread === 0);
  (n as HTMLElement).style.display = unread === 0 ? "none" : "flex";
}

export function confirmDlg(title: string, body: string): Promise<boolean> {
  return new Promise((resolve) => {
    const ov = el("div", "overlay");
    ov.innerHTML = `
      <div class="modal glass" role="dialog" aria-modal="true" aria-label="${esc(title)}">
        <h3>${esc(title)}</h3><p>${esc(body)}</p>
        <div class="row" style="display:flex;gap:10px;justify-content:flex-end">
          <button class="btn ghost" id="m-no">${esc(t("dlg.cancel"))}</button>
          <button class="btn primary" id="m-yes">${esc(t("dlg.confirm"))}</button>
        </div>
      </div>`;
    const done = (v: boolean) => {
      ov.remove();
      document.removeEventListener("keydown", onKey);
      resolve(v);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") done(false);
    };
    document.addEventListener("keydown", onKey);
    ov.querySelector("#m-no")!.addEventListener("click", () => done(false));
    ov.querySelector("#m-yes")!.addEventListener("click", () => done(true));
    ov.addEventListener("mousedown", (e) => {
      if (e.target === ov) done(false);
    });
    document.body.appendChild(ov);
    (ov.querySelector("#m-yes") as HTMLButtonElement).focus();
  });
}

export function renderUserCard(
  s: AuthSession,
  opts: { onSignOut: () => void }
): void {
  const slot = document.getElementById("usercard-slot");
  if (!slot) return;
  slot.innerHTML = "";
  const tierCls =
    s.tier === "Lifetime" ? "lifetime" : s.tier === "Monthly" ? "monthly" : "trial";
  const b = document.createElement("button");
  b.className = "usercard";
  b.setAttribute("aria-label", "Account menu");
  const sub = s.grace
    ? "offline"
    : s.tier === "Lifetime"
      ? t("user.lifetime")
      : s.timeLeft
        ? s.timeLeft
        : "";
  const badge = s.tier === "Lifetime" ? "PREMIUM" : s.tierLabel;
  b.innerHTML = `<span class="lav"></span>
    <span class="who"><b>${esc(s.username)}</b><small>${esc(sub)}</small></span>
    <span class="tierbadge ${tierCls}">${esc(badge)}</span>
    <span class="chev" aria-hidden="true"><i class="ph ph-caret-right"></i></span>`;
  const lav = b.querySelector<HTMLElement>(".lav")!;
  setPfp(lav, s.avatar, (s.username[0] ?? "P").toUpperCase());
  applyShellAvatar(s);
  const menu = document.createElement("div");
  menu.className = "usermenu glass";
  menu.style.display = "none";
  menu.innerHTML = `<button data-m="out">${esc(t("user.signout"))}</button>`;
  const close = () => {
    menu.style.display = "none";
    document.removeEventListener("mousedown", outside);
  };
  const outside = (e: MouseEvent) => {
    if (!menu.contains(e.target as Node) && e.target !== b && !b.contains(e.target as Node))
      close();
  };
  b.addEventListener("click", () => {
    if (menu.style.display === "none") {
      menu.style.display = "";
      document.addEventListener("mousedown", outside);
    } else close();
  });
  menu.querySelector('[data-m="out"]')!.addEventListener("click", () => {
    close();
    opts.onSignOut();
  });
  slot.appendChild(b);
  slot.appendChild(menu);
}

export function clearUserCard(): void {
  const slot = document.getElementById("usercard-slot");
  if (slot) slot.innerHTML = "";
  applyShellAvatar(null);
}
