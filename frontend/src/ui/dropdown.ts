import { el, esc } from "../lib/format";

export interface DropOption {
  value: string;
  label: string;
}

// Custom glass dropdown: dark popup, full keyboard support.
// Replaces native <select> (whose popup is OS-white and unthemeable).
export function dropdown(opts: {
  options: DropOption[];
  value: string;
  label: string;
  onChange: (value: string) => void;
}): { el: HTMLElement; getValue: () => string } {
  let value = opts.value;
  const root = el("div", "dd");
  const btn = el("button", "dd-btn") as HTMLButtonElement;
  btn.type = "button";
  btn.setAttribute("aria-haspopup", "listbox");
  btn.setAttribute("aria-expanded", "false");
  btn.setAttribute("aria-label", opts.label);
  const list = el("div", "dd-list hidden");
  list.setAttribute("role", "listbox");
  list.setAttribute("aria-label", opts.label);
  let activeIdx = Math.max(
    0,
    opts.options.findIndex((o) => o.value === value)
  );

  const paint = () => {
    const cur = opts.options.find((o) => o.value === value);
    btn.innerHTML = `<span>${esc(cur ? cur.label : value)}</span><span class="dd-caret">▾</span>`;
    list.querySelectorAll(".dd-opt").forEach((n, i) => {
      n.classList.toggle("sel", opts.options[i].value === value);
      n.classList.toggle("act", i === activeIdx);
      n.setAttribute("aria-selected", String(opts.options[i].value === value));
    });
  };

  opts.options.forEach((o, i) => {
    const n = el("div", "dd-opt");
    n.setAttribute("role", "option");
    n.dataset.i = String(i);
    n.textContent = o.label;
    n.addEventListener("click", () => {
      value = o.value;
      opts.onChange(value);
      paint();
      close();
      btn.focus();
    });
    n.addEventListener("mousemove", () => {
      activeIdx = i;
      paint();
    });
    list.appendChild(n);
  });

  const open = () => {
    list.classList.remove("hidden");
    btn.setAttribute("aria-expanded", "true");
    paint();
  };
  const close = () => {
    list.classList.add("hidden");
    btn.setAttribute("aria-expanded", "false");
  };
  const isOpen = () => !list.classList.contains("hidden");

  btn.addEventListener("click", () => (isOpen() ? close() : open()));
  btn.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      if (!isOpen()) open();
      else {
        activeIdx = (activeIdx + 1) % opts.options.length;
        paint();
      }
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      if (isOpen()) {
        activeIdx = (activeIdx + opts.options.length - 1) % opts.options.length;
        paint();
      }
    } else if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      if (!isOpen()) open();
      else {
        value = opts.options[activeIdx].value;
        opts.onChange(value);
        paint();
        close();
      }
    } else if (e.key === "Escape" || e.key === "Tab") {
      close();
    }
  });
  list.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
      close();
      btn.focus();
    }
  });
  document.addEventListener("mousedown", (e) => {
    if (isOpen() && !root.contains(e.target as Node)) close();
  });
  document.addEventListener("focusin", (e) => {
    if (isOpen() && !root.contains(e.target as Node)) close();
  });

  root.appendChild(btn);
  root.appendChild(list);
  paint();
  return { el: root, getValue: () => value };
}
