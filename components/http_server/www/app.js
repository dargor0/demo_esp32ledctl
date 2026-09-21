"use strict";

// Configuration ------------------------------------------------------------
const DEFAULT_REFRESH_MS = 500;
const MIN_REFRESH_MS = 500;
const MAX_REFRESH_MS = 5000;

let refreshMs = DEFAULT_REFRESH_MS;
let pollTimer = null;
let leds = [];
let ledCount = 1;

// DOM ----------------------------------------------------------------------
const el = (id) => document.getElementById(id);
const simContainer = el("sim-leds");

// Helpers ------------------------------------------------------------------
function rgbToHex(c) {
  const toHex = (v) => Math.max(0, Math.min(255, v | 0)).toString(16).padStart(2, "0");
  return `#${toHex(c.r)}${toHex(c.g)}${toHex(c.b)}`;
}

async function fetchJson(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`${path} -> ${response.status}`);
  }
  return response.json();
}

function setConnection(ok, text) {
  const badge = el("conn");
  badge.className = "badge " + (ok ? "badge-ok" : "badge-bad");
  badge.textContent = text || (ok ? "online" : "offline");
}

// Simulated LEDs -----------------------------------------------------------
function buildSimLeds() {
  simContainer.textContent = "";
  for (let i = 0; i < ledCount; i++) {
    const wrap = document.createElement("div");
    wrap.className = "sim-led";
    const dot = document.createElement("div");
    dot.className = "led-dot";
    dot.id = `led-${i}`;
    const label = document.createElement("span");
    label.textContent = `#${i}`;
    wrap.appendChild(dot);
    wrap.appendChild(label);
    simContainer.appendChild(wrap);
  }
}

function renderSimLeds() {
  const now = Date.now();
  for (let i = 0; i < ledCount; i++) {
    const dot = el(`led-${i}`);
    if (!dot) continue;
    const led = leds[i];
    if (!led) {
      dot.style.background = "#000";
      dot.style.boxShadow = "none";
      continue;
    }
    const on = led.blink_ms === 0 || now % led.blink_ms < led.blink_ms / 2;
    const hex = on ? rgbToHex(led) : "#000000";
    dot.style.background = hex;
    dot.style.boxShadow = on ? `0 0 12px ${hex}` : "none";
  }
  requestAnimationFrame(renderSimLeds);
}

// Target selector ----------------------------------------------------------
function rebuildTargets() {
  const select = el("target");
  const previous = select.value;
  select.textContent = "";
  const all = document.createElement("option");
  all.value = "all";
  all.textContent = "All LEDs";
  select.appendChild(all);
  for (let i = 0; i < ledCount; i++) {
    const option = document.createElement("option");
    option.value = String(i);
    option.textContent = `LED #${i}`;
    select.appendChild(option);
  }
  if (previous && select.querySelector(`option[value="${previous}"]`)) {
    select.value = previous;
  }
}

// Polling ------------------------------------------------------------------
function summarize() {
  if (leds.length === 0) {
    return "no LED data";
  }
  const custom = leds.filter((l) => l.state === "custom").length;
  const first = leds[0];
  const color = rgbToHex(first);
  const blink = first.blink_ms === 0 ? "solid" : `blink ${first.blink_ms} ms`;
  return `${ledCount} LED(s), ${custom} custom • LED #0 ${first.state} ${color} ${blink}`;
}

async function poll() {
  try {
    const [status, ledData, time] = await Promise.all([
      fetchJson("/api/status"),
      fetchJson("/api/led"),
      fetchJson("/api/time"),
    ]);

    if (status.system) {
      document.title = status.system;
      el("system-name").textContent = status.system;
    }

    if (typeof ledData.count === "number" && ledData.count !== ledCount) {
      ledCount = ledData.count;
      rebuildTargets();
      buildSimLeds();
    }
    leds = Array.isArray(ledData.leds) ? ledData.leds : [];
    el("led-summary").textContent = summarize();
    el("status-line").textContent =
      `${status.wifi} • ${status.ip || "no ip"} • heap ${status.free_heap} B • ` +
      `button ${status.last_button_event}`;

    if (time.synced) {
      el("device-time").textContent = time.iso8601;
      el("sync-state").textContent = `epoch ${time.epoch}`;
    } else {
      el("device-time").textContent = "not synchronized";
      el("sync-state").textContent = "waiting for NTP";
    }

    setConnection(true);
  } catch (err) {
    setConnection(false, "offline");
    el("status-line").textContent = String(err);
  }
}

function schedulePolling() {
  if (pollTimer !== null) {
    clearInterval(pollTimer);
  }
  pollTimer = setInterval(poll, refreshMs);
  poll();
}

// Control ------------------------------------------------------------------
function currentColor() {
  return {
    r: Number(el("r").value),
    g: Number(el("g").value),
    b: Number(el("b").value),
    blink_ms: Number(el("blink").value),
  };
}

async function apply() {
  const target = el("target").value;
  const body = currentColor();
  const path = target === "all" ? "/api/led" : `/api/led/${target}`;
  const method = target === "all" ? "PUT" : "PUT";
  try {
    const response = await fetch(path, {
      method,
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const data = await response.json().catch(() => ({}));
    el("control-result").textContent = response.ok
      ? "applied"
      : `error: ${data.message || response.status}`;
    poll();
  } catch (err) {
    el("control-result").textContent = String(err);
  }
}

async function clear() {
  const target = el("target").value;
  const path = target === "all" ? "/api/led" : `/api/led/${target}`;
  try {
    const response = await fetch(path, { method: "DELETE" });
    const data = await response.json().catch(() => ({}));
    el("control-result").textContent = response.ok
      ? "cleared"
      : `error: ${data.message || response.status}`;
    poll();
  } catch (err) {
    el("control-result").textContent = String(err);
  }
}

// Wiring -------------------------------------------------------------------
function syncColorInputs(source) {
  if (source === "picker") {
    const hex = el("color").value;
    el("r").value = parseInt(hex.slice(1, 3), 16);
    el("g").value = parseInt(hex.slice(3, 5), 16);
    el("b").value = parseInt(hex.slice(5, 7), 16);
  } else {
    el("color").value = rgbToHex(currentColor());
  }
}

function init() {
  const stored = Number(localStorage.getItem("refreshMs"));
  if (stored >= MIN_REFRESH_MS && stored <= MAX_REFRESH_MS) {
    refreshMs = stored;
  }
  el("refresh").value = String(refreshMs);
  el("refresh-value").textContent = `${refreshMs} ms`;

  el("refresh").addEventListener("input", (event) => {
    refreshMs = Number(event.target.value);
    el("refresh-value").textContent = `${refreshMs} ms`;
    localStorage.setItem("refreshMs", String(refreshMs));
    schedulePolling();
  });

  el("color").addEventListener("input", () => syncColorInputs("picker"));
  ["r", "g", "b"].forEach((id) =>
    el(id).addEventListener("input", () => syncColorInputs("sliders"))
  );

  el("apply").addEventListener("click", apply);
  el("clear").addEventListener("click", clear);

  buildSimLeds();
  rebuildTargets();
  requestAnimationFrame(renderSimLeds);
  schedulePolling();
}

document.addEventListener("DOMContentLoaded", init);
