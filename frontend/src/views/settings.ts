import { el, esc } from "../lib/format";
import { toast, applyShellLang, injectPageHead } from "../ui/shell";
import { t, getLang, setLang } from "../i18n";

export function renderSettings(root: HTMLElement): void {
  root.innerHTML = "";
  const card = el("div", "card glass card-icon");
  card.innerHTML = `
    <div class="card-ihead">
      <span class="icon-tile"><i class="ph ph-palette"></i></span>
      <h3>${esc(t("settings.appearance"))}</h3>
    </div>
    <p class="sub">${esc(t("settings.appearance.sub"))}</p>
    <div style="display:flex;align-items:center;gap:12px">
      <div class="toggle" id="theme-t" role="switch" tabindex="0"
           aria-checked="false" aria-label="${esc(t("settings.dark"))}"></div>
      <span id="theme-lbl" style="font-weight:600">${esc(
        (localStorage.getItem("px-theme") ?? "dark") === "dark"
          ? t("settings.dark")
          : t("settings.light")
      )}</span>
    </div>
    <div style="display:flex;align-items:center;gap:12px;margin-top:14px">
      <div class="toggle" id="motion-t" role="switch" tabindex="0"
           aria-checked="true" aria-label="${esc(t("settings.motion"))}"></div>
      <span style="font-weight:600">${esc(t("settings.motion"))}</span>
    </div>`;
  root.appendChild(card);

  const langCard = el("div", "card glass card-icon");
  langCard.innerHTML = `
    <div class="card-ihead">
      <span class="icon-tile"><i class="ph ph-translate"></i></span>
      <h3>${esc(t("settings.language"))}</h3>
    </div>
    <p class="sub">${esc(t("settings.language.sub"))}</p>
    <div style="display:flex;align-items:center;gap:12px">
      <select id="settings-lang" class="lang-sel" aria-label="${esc(t("settings.language"))}">
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
    </div>`;
  root.appendChild(langCard);

  const about = el("div", "card glass");
  about.innerHTML = `
    <h3>${esc(t("settings.about"))}</h3>
    <p class="sub"><strong>ORBIT OPTIMIZER 1.0</strong> is a Windows tuning utility: curated registry
    packs, debloat, service control, input latency and live system monitoring in one glass UI.</p>
    <ul class="sub" style="margin:8px 0 0 18px;padding:0;line-height:1.7">
      <li>Dashboard: live CPU/RAM/GPU load, health score, disk cleanup and a restore point button.</li>
      <li>Optimizer: tweak packs grouped by topic (System, RAM, CPU, GPU, Network...) with one-click
      Apply, per-key Inspect, automatic backup and Revert.</li>
      <li>AIM, Debloat and Services: mouse and input latency, safe removal of preinstalled junk,
      and Windows service tuning.</li>
      <li>Nothing is permanent: every apply snapshots first, and a restore point is one click away.</li>
      <li>9 interface languages, dark/light themes, Discord sign-in with lifetime Premium.</li>
    </ul>
    <p class="sub" style="margin-top:10px">Build: .NET 8 + WebView2 host, Vite/TypeScript UI, typed
    JSON IPC via <span style="font-family:var(--mono)">window.chrome.webview.postMessage</span>.</p>`;
  root.appendChild(about);

  const applyTheme = (dark: boolean) => {
    document.documentElement.dataset.theme = dark ? "dark" : "light";
    localStorage.setItem("px-theme", dark ? "dark" : "light");
    const tg = card.querySelector("#theme-t")!;
    tg.setAttribute("aria-checked", String(dark));
    (card.querySelector("#theme-lbl") as HTMLElement).textContent = dark
      ? t("settings.dark")
      : t("settings.light");
  };

  const dark0 = (localStorage.getItem("px-theme") ?? "dark") === "dark";
  applyTheme(dark0);

  const tgl = card.querySelector("#theme-t")!;
  const flipTheme = () => {
    const nowDark = document.documentElement.dataset.theme !== "dark";
    applyTheme(nowDark);
    toast({
      title: "Theme",
      body: nowDark ? t("settings.theme.on") : t("settings.theme.off"),
      kind: "info",
    });
  };
  tgl.addEventListener("click", flipTheme);
  tgl.addEventListener("keydown", (e) => {
    if ((e as KeyboardEvent).key === "Enter" || (e as KeyboardEvent).key === " ") {
      e.preventDefault();
      flipTheme();
    }
  });

  const motion = card.querySelector("#motion-t")!;
  const applyMotionVars = (on: boolean) => {
    document.documentElement.style.setProperty("--dur-micro", on ? "160ms" : "0.01ms");
    document.documentElement.style.setProperty("--dur-page", on ? "300ms" : "0.01ms");
  };
  const setMotion = (on: boolean) => {
    motion.setAttribute("aria-checked", String(on));
    localStorage.setItem("px-motion", on ? "on" : "off");
    applyMotionVars(on);
  };
  setMotion(localStorage.getItem("px-motion") !== "off");
  const flipMotion = () => {
    const on = motion.getAttribute("aria-checked") !== "true";
    setMotion(on);
  };
  motion.addEventListener("click", flipMotion);
  motion.addEventListener("keydown", (e) => {
    if ((e as KeyboardEvent).key === "Enter" || (e as KeyboardEvent).key === " ") {
      e.preventDefault();
      flipMotion();
    }
  });

  const langSel = langCard.querySelector<HTMLSelectElement>("#settings-lang")!;
  langSel.value = getLang();
  langSel.addEventListener("change", () => {
    const code = setLang(langSel.value);
    applyShellLang();
    toast({
      title: t("lang.title"),
      body: t("lang.saved").replace("{code}", code.toUpperCase()),
      kind: "info",
    });
    // re-render this view so all its strings switch language too
    root.innerHTML = "";
    renderSettings(root);
    injectPageHead("settings", root);
  });
}
