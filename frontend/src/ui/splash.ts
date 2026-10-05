import { el } from "../lib/format";

// JARVIS-style boot splash: typed init lines + progress, fades out.
// Skipped fast when animations are off; click anywhere to skip.
const LINES = [
  "ORBIT OS // cold boot",
  "> INITIALISING CORE ............ OK",
  "> LINKING REGISTRY ENGINE ...... OK",
  "> BINDING ADB BRIDGE ........... OK",
  "> CALIBRATING INTERFACE ........ OK",
  "> MOUNTING SYSTEM HIVE ......... OK",
  "> LOADING GPU PROFILES ......... OK",
  "> VERIFYING AUTH CHANNEL ....... OK",
  "> ALL SYSTEMS ONLINE",
];

export function splash(done: () => void): void {
  const reduced =
    localStorage.getItem("px-motion") === "off" ||
    window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  const ov = el("div", "splash");
  ov.innerHTML = `
    <div class="splash-inner">
      <div class="splash-mark"><img src="logo.png" alt="" /></div>
      <div class="splash-title">ORBIT</div>
      <div class="splash-log" aria-live="polite"></div>
      <div class="pbar splash-bar"><div></div></div>
    </div>`;
  document.body.appendChild(ov);
  // force first paint before animating
  void ov.offsetWidth;
  requestAnimationFrame(() => ov.classList.add("show"));

  const log = ov.querySelector(".splash-log")!;
  const bar = ov.querySelector(".splash-bar > div") as HTMLElement;
  let finished = false;
  const finish = () => {
    if (finished) return;
    finished = true;
    ov.classList.add("hide");
    setTimeout(() => {
      ov.remove();
      done();
    }, reduced ? 0 : 480);
  };
  ov.addEventListener("mousedown", finish);

  if (reduced) {
    log.innerHTML = LINES.map((l) => `<div>${l}</div>`).join("");
    bar.style.width = "100%";
    setTimeout(finish, 350);
    return;
  }
  let i = 0;
  const step = () => {
    if (finished) return;
    if (i < LINES.length) {
      const d = document.createElement("div");
      d.className = "sline";
      d.style.animationDelay = "0ms";
      d.textContent = LINES[i];
      log.appendChild(d);
      bar.style.width = `${Math.round(((i + 1) / LINES.length) * 100)}%`;
      i++;
      setTimeout(step, 380);
    } else {
      setTimeout(finish, 750);
    }
  };
  setTimeout(step, 450);
}
