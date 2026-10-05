import { invoke, onProgress, isShell } from "../lib/ipc";
import { el, esc } from "../lib/format";
import { confirmDlg, notify, toast } from "../ui/shell";
import { recordRun } from "../lib/session";
import type { OpResult } from "../types";

interface Pkg {
  name: string;
  fullName: string;
  displayName: string;
}

interface Preset {
  id: string;
  label: string;
  patterns: string[];
}

const DEFAULT_PRESETS: Preset[] = [
  {
    id: "xbox",
    label: "Xbox & gaming stubs",
    patterns: [
      "Microsoft.GamingApp",
      "Microsoft.XboxApp",
      "Microsoft.XboxGamingOverlay",
      "Microsoft.XboxIdentityProvider",
      "Microsoft.XboxSpeechToTextOverlay",
      "Microsoft.XboxTcUI",
      "Microsoft.GamingServices",
      "Microsoft.XboxGameCallableUI",
    ],
  },
  {
    id: "social",
    label: "Social & phone-link stubs",
    patterns: [
      "Microsoft.SkypeApp",      "Microsoft.People",      "Microsoft.YourPhone",      "Microsoft.WindowsCommunicationsApps",      "MicrosoftTeams",      "MicrosoftTeamsPhone",      "MicrosoftTeamsMeetingAddin"
    ],
  },
  {
    id: "bing",
    label: "Bing content apps",
    patterns: [
      "Microsoft.BingNews",
      "Microsoft.BingWeather",
      "Microsoft.BingFinance",
      "Microsoft.BingSports",
      "Microsoft.BingFoodAndDrink",
      "Microsoft.BingHealthAndFitness",
      "Microsoft.BingTravel",
      "Microsoft.BingWallpaper",
      "Microsoft.GetHelp",
      "Microsoft.Getstarted",
      "Microsoft.MicrosoftOfficeHub",
      "Microsoft.MicrosoftSolitaireCollection",
      "Microsoft.WindowsFeedbackHub",
      "Microsoft.WindowsMaps",
      "Microsoft.WindowsAlarms",
      "Microsoft.WindowsSoundRecorder",
      "Microsoft.PowerAutomateDesktop",
      "Microsoft.Todos",
      "Microsoft.Clipchamp",
      "Microsoft.MicrosoftStickyNotes",
      "Microsoft.OutlookForWindows",
    ],
  },
  {
    id: "cortana-ai",
    label: "Cortana / Windows AI stubs",
    patterns: [
      "Microsoft.549981C3F5F10",
      "Microsoft.Windows.Copilot",
      "Microsoft.Windows.Ai.Copilot.Provider",
      "Microsoft.Copilot",
      "Microsoft.MicrosoftPCManager",
      "Microsoft.Windows.Ai.ContentDeliveryManager",
    ],
  },
  {
    id: "office-media",
    label: "Office / media stubs",
    patterns: [
      "Microsoft.MicrosoftOfficeHub",
      "Microsoft.OfficeHub",
      "Microsoft.OutlookForWindows",
      "Microsoft.ZuneMusic",
      "Microsoft.ZuneVideo",
      "Clipchamp.Clipchamp",
      "Microsoft.WindowsSoundRecorder",
      "Microsoft.MicrosoftStickyNotes",
      "Microsoft.Whiteboard",
      "Microsoft.MicrosoftPowerBIForWindows",
      "Microsoft.Microsoft3DViewer",
      "Microsoft.MSPaint",
      "Microsoft.Paint",
      "SpotifyAB.SpotifyMusic",
      "Netflix",
      "Disney.",
    ],
  },
  {
    id: "comms",
    label: "Comms / family / help",
    patterns: [
      "Microsoft.SkypeApp",      "Microsoft.People",      "Microsoft.YourPhone",      "microsoft.windowscommunicationsapps",      "MicrosoftTeams",      "MSTeams",      "Microsoft.MicrosoftTeamsforSurfaceHub",      "MicrosoftCorporationII.MailforSurfaceHub",      "MicrosoftCorporationII.QuickAssist",      "MicrosoftCorporationII.MicrosoftFamily",      "Microsoft.Windows.PeopleExperienceHost",      "Microsoft.Windows.SecureAssessmentBrowser",      "Microsoft.WindowsAlarms",      "Microsoft.WindowsMaps",      "Microsoft.WindowsCamera",      "Microsoft.MicrosoftSolitaireCollection",      "Microsoft.WindowsFeedbackHub",      "Microsoft.GetHelp",      "Microsoft.Getstarted",      "Microsoft.Todos",      "Microsoft.PowerAutomateDesktop",      "Microsoft.StartExperiencesApp",      "MicrosoftAdvertising",      "Microsoft.Advertising",      "MixedReality.Portal",      "Microsoft.Windows.DevHome",      "Microsoft.WindowsCalculator",      "Microsoft.ScreenSketch",      "Microsoft.Windows.Photos",      "Microsoft.WindowsStore",      "Microsoft.StorePurchaseApp",      "Microsoft.Services.Store.Engagement",      "Microsoft.GamingApp",      "Microsoft.XboxApp",      "Microsoft.XboxGamingOverlay",      "Microsoft.XboxSpeechToTextOverlay",      "Microsoft.Xbox.TCUI",      "Microsoft.XboxGameCallableUI",      "Microsoft.GamingServices",      "Microsoft.WindowsSoundRecorder",      "Microsoft.MicrosoftOfficeHub"
    ],
  },
  {
    id: "widgets-feeds",
    label: "Widgets / feeds / web experience",
    patterns: [
      "Microsoft.Windows.WebExperience",      "MicrosoftWindows.Client.WebExperience",      "Microsoft.WidgetsPlatformRuntime",      "Microsoft.BingNews",      "Microsoft.BingSearch",      "Microsoft.BingWeather",      "Microsoft.BingFinance",      "Microsoft.BingSports",      "Microsoft.BingFoodAndDrink",      "Microsoft.BingHealthAndFitness",      "Microsoft.BingTravel",      "Microsoft.BingWallpaper",      "Microsoft.WindowsMaps",      "Microsoft.WindowsAlarms",      "Microsoft.WindowsCamera",      "Microsoft.MicrosoftStickyNotes",      "Microsoft.WindowsFeedbackHub",      "Microsoft.GetHelp",      "Microsoft.Getstarted",      "Microsoft.Todos",      "Microsoft.PowerAutomateDesktop",      "Microsoft.MicrosoftSolitaireCollection",      "Microsoft.549981C3F5F10",      "Microsoft.Windows.Copilot",      "Microsoft.Copilot",      "Microsoft.MicrosoftPCManager",      "Microsoft.Windows.Ai.ContentDeliveryManager",      "Microsoft.Windows.Ai.Copilot.Provider",      "Microsoft.Windows.PeopleExperienceHost",      "Microsoft.Windows.SecureAssessmentBrowser",      "Microsoft.StartExperiencesApp",      "MicrosoftAdvertising",      "Microsoft.Advertising",      "MixedReality.Portal",      "Microsoft.Windows.DevHome",      "Microsoft.WindowsCalculator",      "Microsoft.ScreenSketch",      "Microsoft.Windows.Photos",      "Microsoft.WindowsStore",      "Microsoft.StorePurchaseApp",      "Microsoft.Services.Store.Engagement",      "Microsoft.GamingApp",      "Microsoft.XboxApp",      "Microsoft.XboxGamingOverlay",      "Microsoft.XboxSpeechToTextOverlay",      "Microsoft.Xbox.TCUI",      "Microsoft.XboxGameCallableUI",      "Microsoft.GamingServices",      "Microsoft.WindowsSoundRecorder",      "Microsoft.MicrosoftOfficeHub"
    ],
  },
];

// Windows Terminal is a useful tool — drop it from the Bing preset if present.
DEFAULT_PRESETS[2].patterns = DEFAULT_PRESETS[2].patterns.filter(
  (p) => p !== "Microsoft.WindowsTerminal"
);

let running = false;
let liveTimer: number | undefined;

function stopLive(): void {
  if (liveTimer !== undefined) {
    window.clearInterval(liveTimer);
    liveTimer = undefined;
  }
}

// Match against package Name OR DisplayName (live scan of installed packages).
function matchesPattern(pkg: Pkg, patterns: string[]): boolean {
  const fields = [pkg.name, pkg.displayName]
    .filter(Boolean)
    .map((s) => (s || "").toLowerCase());
  return patterns.some((p) => {
    const pl = p.toLowerCase();
    return fields.some((f) => f === pl || f.startsWith(pl) || f.includes(pl));
  });
}

export function renderDebloat(root: HTMLElement): void {
  root.innerHTML = "";
  const card = el("div", "card glass card-icon");
  card.dataset.search = "debloat remove apps packages bloatware appx";
  card.innerHTML = `<div class="card-ihead">
      <span class="icon-tile"><i class="ph ph-trash"></i></span>
      <h3>Debloat</h3>
    </div>
    <p class="sub">Strip preinstalled Store apps and stubs. Select presets or individual packages, then Remove. Removed apps can usually be reinstalled from the Microsoft Store.</p>
    <div class="row" style="margin-bottom:12px">
      <div id="db-presets" style="display:flex;gap:8px;flex-wrap:wrap"></div>
      <div class="spacer"></div>
      <button class="btn sm" id="db-all">Select all listed</button>
      <button class="btn sm ghost" id="db-none">Clear</button>
      <button class="btn sm primary" id="db-go">Remove selected</button>
    </div>
    <div class="row" style="margin-bottom:10px">
      <input type="text" id="db-q" class="input" placeholder="Filter packages…"
             aria-label="Filter packages"
             style="flex:1;min-width:180px;padding:8px 10px;border-radius:var(--r-sm);border:1px solid var(--border-glass);background:var(--bg-layer-1);color:var(--text-1)" />
      <span class="stat" id="db-count">loading…</span>
    </div>
    <div id="db-list" class="stagger" style="max-height:420px;overflow-y:auto"></div>
    <div style="margin-top:14px"><div class="pbar"><div id="db-bar"></div></div>
    <p class="sub" id="db-msg" style="margin-top:8px">Idle. Loading installed packages…</p></div>`;
  root.appendChild(card);

  const list = card.querySelector("#db-list") as HTMLElement;
  const bar = card.querySelector("#db-bar") as HTMLElement;
  const msg = card.querySelector("#db-msg")!;
  const countEl = card.querySelector("#db-count")!;
  const presetBox = card.querySelector("#db-presets")!;
  const filter = card.querySelector("#db-q") as HTMLInputElement;

  let packages: Pkg[] = [];
  let presets: Preset[] = DEFAULT_PRESETS;

  const selected = new Set<string>();

  const paintCount = () => {
    const visible = [...list.querySelectorAll<HTMLInputElement>("input[data-pkg]:checked")]
      .length;
    const totalVisible = list.querySelectorAll<HTMLInputElement>("input[data-pkg]").length;
    countEl.textContent = `${selected.size} selected · ${totalVisible} shown / ${packages.length} installed`;
    void visible;
  };

  const applyFilter = () => {
    const q = filter.value.trim().toLowerCase();
    list.querySelectorAll<HTMLElement>(".twk-row").forEach((row) => {
      const s = row.dataset.search || "";
      row.style.display = !q || s.includes(q) ? "" : "none";
    });
    paintCount();
  };

  const paintPresets = () => {
    presetBox.innerHTML = "";
    for (const p of presets) {
      const matches = packages.filter((pkg) => matchesPattern(pkg, p.patterns));
      const b = el("button", "btn sm ghost");
      b.type = "button";
      b.textContent = `${p.label} (${matches.length})`;
      b.title = matches.length
        ? `Select ${matches.length} matching package(s): ${matches.map((m) => m.name).join(", ")}`
        : `No packages from this preset are installed on this PC.`;
      b.addEventListener("click", () => {
        if (matches.length === 0) {
          toast({
            title: p.label,
            body: "No packages from this preset are installed on this PC.",
            kind: "info",
          });
          return;
        }
        for (const pkg of matches) selected.add(pkg.fullName);
        syncChecks();
        paintCount();
        toast({
          title: p.label,
          body: `${matches.length} package(s) selected.`,
          kind: "info",
        });
      });
      presetBox.appendChild(b);
    }
  };

  const syncChecks = () => {
    list.querySelectorAll<HTMLInputElement>("input[data-pkg]").forEach((c) => {
      c.checked = selected.has(c.dataset.pkg!);
    });
  };

  const paint = () => {
    list.innerHTML = "";
    if (packages.length === 0) {
      list.innerHTML = `<div class="empty"><div class="big"><i class="ph ph-trash"></i></div>${
        isShell() ? "No AppX packages found." : "Dev mode — run inside the shell."
      }</div>`;
      paintCount();
      return;
    }
    const frag = document.createDocumentFragment();
    const dlabel = (p: { displayName?: string; name?: string }) =>
      String(p.displayName || p.name || "");
    packages
      .slice()
      .filter((p) => !!p && typeof (p.name || p.displayName) === "string")
      .sort((a, b) => dlabel(a).localeCompare(dlabel(b)))
      .forEach((pkg, i) => {
        const row = el("label", "twk-row");
        row.style.animationDelay = `${Math.min(i, 40) * 15}ms`;
        row.dataset.search = `${pkg.displayName || ""} ${pkg.name || ""} ${pkg.fullName || ""}`.toLowerCase();
        const checked = selected.has(pkg.fullName) ? "checked" : "";
        row.innerHTML = `
          <input type="checkbox" class="chk" data-pkg="${esc(pkg.fullName || "")}"
                 aria-label="Select ${esc(pkg.displayName || pkg.name || "pkg")}" ${checked} />
          <span class="twk-meta">
            <span class="twk-name">${esc(pkg.displayName || pkg.name || "Unknown")}</span>
            <span class="twk-desc">${esc(pkg.name || "")}</span>
          </span>
          <span class="stat">appx</span>`;
        const cb = row.querySelector("input") as HTMLInputElement;
        cb.addEventListener("change", () => {
          if (cb.checked) selected.add(pkg.fullName);
          else selected.delete(pkg.fullName);
          paintCount();
        });
        frag.appendChild(row);
      });
    list.appendChild(frag);
    applyFilter();
  };

  paintPresets();
  paint();

  const refresh = async (opts?: { live?: boolean }) => {
    if (!isShell()) {
      if (!opts?.live) {
        msg.textContent = "Dev mode — run inside the shell.";
        paintCount();
      }
      return;
    }
    if (!opts?.live) msg.textContent = "Loading installed packages…";
    try {
      const next = await invoke<Pkg[]>("debloat.list");
      const changed =
        next.length !== packages.length ||
        next.some((p, i) => p.fullName !== packages[i]?.fullName);
      packages = next;
      if (!opts?.live) {
        try {
          const p = await invoke<Preset[]>("debloat.presets");
          if (Array.isArray(p) && p.length > 0 && p.every((x) => x?.patterns)) {
            presets = p;
          }
        } catch {
          /* presets optional — defaults already painted */
        }
      }
      // drop selections no longer installed
      const names = new Set(packages.map((x) => x.fullName));
      for (const k of [...selected]) if (!names.has(k)) selected.delete(k);
      if (!opts?.live || changed) {
        paintPresets(); // re-count matches against live packages
        paint();
      }
      if (!opts?.live)
        msg.textContent = `${packages.length} packages installed. Live-refreshing every 5s — pick presets or packages, then Remove selected.`;
      else paintCount();
    } catch (e) {
      if (!opts?.live) {
        msg.textContent = "Load failed.";
        toast({ title: "Debloat", body: String(e), kind: "err" });
      }
    }
    if (!opts?.live) paintCount();
  };
  void refresh();

  // real-time package scan while the Debloat view is open
  stopLive();
  liveTimer = window.setInterval(() => {
    if (running) return;
    void refresh({ live: true });
  }, 5000);

  filter.addEventListener("input", applyFilter);

  card.querySelector("#db-all")!.addEventListener("click", () => {
    for (const pkg of packages) selected.add(pkg.fullName);
    syncChecks();
    paintCount();
  });
  card.querySelector("#db-none")!.addEventListener("click", () => {
    selected.clear();
    syncChecks();
    paintCount();
  });

  onProgress((p) => {
    bar.style.width = `${p.pct}%`;
    msg.textContent = p.msg;
  });

  card.querySelector("#db-go")!.addEventListener("click", async () => {
    if (running || !isShell()) return;
    if (selected.size === 0) {
      toast({
        title: "Debloat",
        body: "Select at least one package.",
        kind: "info",
      });
      return;
    }
    const ok = await confirmDlg(
      "Remove selected apps?",
      `${selected.size} package(s) will be removed for this user. Some can be reinstalled from the Microsoft Store. Continue?`
    );
    if (!ok) return;
    running = true;
    try {
      const r = await invoke<OpResult & { removed?: number; failed?: number }>(
        "debloat.remove",
        { packages: [...selected] }
      );
      recordRun("debloat:remove", selected.size);
      notify("Debloat", r.message);
      toast({ title: "Debloat", body: r.message, kind: r.failed ? "info" : "ok" });
      selected.clear();
      await refresh();
      bar.style.width = "100%";
      msg.textContent = r.message;
    } catch (e) {
      toast({ title: "Debloat", body: String(e), kind: "err" });
      msg.textContent = String(e);
    } finally {
      running = false;
    }
  });

  // stop live poll when the view is swapped out (innerHTML cleared on navigate)
  const observer = new MutationObserver(() => {
    if (!document.body.contains(card)) {
      stopLive();
      observer.disconnect();
    }
  });
  observer.observe(root, { childList: true });
}
