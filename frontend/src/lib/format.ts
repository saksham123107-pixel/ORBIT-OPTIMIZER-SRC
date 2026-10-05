export function nowTime(): string {
  return new Date().toLocaleTimeString("en-GB");
}

export function esc(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/** Animate an integer counter 0 -> target with easeOutExpo over ~900ms. */
export function animateCount(
  el: HTMLElement,
  target: number,
  suffix = ""
): void {
  if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    el.textContent = `${target.toLocaleString()}${suffix}`;
    return;
  }
  const dur = 900;
  const t0 = performance.now();
  const step = (t: number) => {
    const k = Math.min(1, (t - t0) / dur);
    const eased = k === 1 ? 1 : 1 - Math.pow(2, -10 * k);
    el.textContent = `${Math.round(target * eased).toLocaleString()}${suffix}`;
    if (k < 1) requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
}

export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  cls = "",
  html = ""
): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (html) e.innerHTML = html;
  return e;
}
