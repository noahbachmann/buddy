// Buddy panel: renders the retained MQTT state the web service streams (/api/events) and sends skill commands
// (POST /api/cmd/<service>). No framework, no build step: the Pi serves these files as they are.
"use strict";

const state = {};               // topic → payload (JSON, or a string like "online")
const expanded = new Set();     // room ids showing their single lights
let interacting = 0;            // > 0 while a slider is held: re-rendering would yank it from the finger
let renderQueued = false;

const $ = (id) => document.getElementById(id);

function el(tag, attrs = {}, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else if (v !== undefined && v !== null && v !== false) e.setAttribute(k, v === true ? "" : v);
  }
  for (const c of children.flat()) if (c !== null && c !== undefined && c !== false) e.append(c);
  return e;
}

// --- talking to Buddy -------------------------------------------------------------------------------------------

let toastTimer;
function toast(text) {
  const t = $("toast");
  t.textContent = text;
  t.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove("show"), 3000);
}

async function send(service, cmd) {
  try {
    const r = await fetch(`/api/cmd/${service}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(cmd),
    });
    if (!r.ok) toast((await r.json().catch(() => ({}))).error || `${service}: HTTP ${r.status}`);
  } catch {
    toast("Buddy is not reachable");
  }
}

// Sliders: at most one command per `ms` per key while dragging, and always the final value.
const pending = new Map();
function throttled(key, ms, fn) {
  const p = pending.get(key) || { last: 0, timer: null, fn: null };
  pending.set(key, p);
  p.fn = fn;
  const wait = p.last + ms - Date.now();
  if (wait <= 0) {
    p.last = Date.now();
    fn();
  } else if (!p.timer) {
    p.timer = setTimeout(() => {
      p.timer = null;
      p.last = Date.now();
      p.fn();
    }, wait);
  }
}

function connect() {
  const link = $("link");
  const events = new EventSource("/api/events");
  events.onopen = () => {
    link.textContent = "live";
    link.classList.add("up");
  };
  events.onerror = () => {  // EventSource reconnects by itself
    link.textContent = "reconnecting…";
    link.classList.remove("up");
  };
  events.onmessage = (e) => {
    const { topic, payload } = JSON.parse(e.data);
    if (payload === null) delete state[topic];
    else state[topic] = payload;
    queueRender();
  };
}

function queueRender() {
  if (renderQueued) return;
  renderQueued = true;
  requestAnimationFrame(() => {
    renderQueued = false;
    if (interacting > 0) return;  // render() runs again when the slider is let go
    render();
  });
}

// --- widgets ----------------------------------------------------------------------------------------------------

function toggle(on, label, onChange) {
  return el("button", {
    class: "switch", role: "switch", "aria-checked": String(on), "aria-label": label,
    onclick: (e) => {
      const next = e.currentTarget.getAttribute("aria-checked") !== "true";
      e.currentTarget.setAttribute("aria-checked", String(next));  // optimistic; the bridge's event confirms
      onChange(next);
    },
  });
}

function slider({ min, max, value, label, format, cls, onInput }) {
  const out = el("span", { class: "range-label" }, format(value));
  const input = el("input", {
    type: "range", min, max, value, "aria-label": label, class: cls,
    oninput: (e) => {
      out.textContent = format(+e.target.value);
      onInput(+e.target.value);
    },
    onpointerdown: () => { interacting++; },
  });
  return [input, out];
}
// Pointer released anywhere (also outside the slider): render what came in meanwhile.
addEventListener("pointerup", () => { if (interacting > 0) { interacting = 0; setTimeout(queueRender, 600); } });
addEventListener("pointercancel", () => { interacting = 0; queueRender(); });

const COLORS = [
  ["red", "#e5483b"], ["orange", "#f08a24"], ["yellow", "#f2cf3a"], ["green", "#3fb35d"],
  ["turquoise", "#2ab3b0"], ["blue", "#3b6fe5"], ["purple", "#8b4fd6"], ["pink", "#e85aa8"],
];
const WHITES = [["warm white", "#ffd8a1"], ["neutral white", "#fff3e0"], ["daylight", "#e8f0ff"]];

// --- lights -----------------------------------------------------------------------------------------------------

function hue(cmd) { send("hue", cmd); }

function lightControls(target, lights, key) {
  const nodes = [];
  const dimmable = lights.some((l) => l.dimming);
  const colour = lights.some((l) => l.color);
  const white = lights.some((l) => l.temperature);
  if (target.on && dimmable) {
    nodes.push(el("div", { class: "row" }, slider({
      min: 1, max: 100, value: Math.max(1, target.brightness), label: `${target.name} brightness`,
      format: (v) => `${v} %`,
      onInput: (v) => throttled(`b-${key}`, 300, () => hue({ action: "brightness", query: String(v), id: target.id })),
    })));
  }
  if (target.on && white) {
    const mireks = lights.filter((l) => l.mirek).map((l) => l.mirek);
    const mirek = mireks.length ? mireks.reduce((a, b) => a + b) / mireks.length : 370;
    nodes.push(el("div", { class: "row" }, slider({
      min: 2000, max: 6500, value: Math.round(1e6 / mirek / 100) * 100, label: `${target.name} warm to cool`,
      format: (v) => `${v} K`, cls: "warmth",
      onInput: (v) => throttled(`t-${key}`, 300, () => hue({ action: "temperature", query: String(v), id: target.id })),
    })));
  }
  if (colour) {
    nodes.push(el("div", { class: "controls", role: "group", "aria-label": `${target.name} colour` },
      [...COLORS, ...WHITES].map(([name, css]) => el("button", {
        class: "swatch", style: `background:${css}`, title: name, "aria-label": name,
        onclick: () => hue({ action: "color", query: name, id: target.id }),
      }))));
  }
  return nodes;
}

function roomCard(room, lightsById) {
  const lights = room.lights.map((id) => lightsById[id]).filter(Boolean);
  const onCount = lights.filter((l) => l.on).length;
  const sub = lights.length === 0 ? "no lights"
    : room.on ? `${onCount} of ${lights.length} on · ${room.brightness} %` : `${lights.length} light${lights.length > 1 ? "s" : ""} · off`;
  const card = el("article", { class: `card${room.on ? " lit" : ""}`, "aria-label": room.name },
    el("div", { class: "row" },
      el("div", { class: "name" }, room.name, el("small", {}, room.kind === "zone" ? `zone · ${sub}` : sub)),
      toggle(room.on, `${room.name} on/off`, (on) => hue({ action: on ? "on" : "off", id: room.id }))),
    lightControls(room, lights, room.id));

  if (room.scenes.length) {
    card.append(el("div", { class: "controls", role: "group", "aria-label": `${room.name} scenes` },
      room.scenes.map((s) => el("button", {
        class: "chip-btn", onclick: () => hue({ action: "scene", query: s.name, id: room.id }),
      }, s.name))));
  }
  if (room.kind !== "home" && lights.length > 1) {
    const open = expanded.has(room.id);
    card.append(el("button", {
      class: "more", "aria-expanded": String(open),
      onclick: () => { open ? expanded.delete(room.id) : expanded.add(room.id); render(); },
    }, open ? "Hide lights ▴" : `${lights.length} lights ▾`));
    if (open) {
      card.append(el("div", { class: "lights" }, lights.map((l) => el("div", { class: "light" },
        el("div", { class: "row" },
          el("div", { class: "name" }, l.name, el("small", {}, l.on ? (l.dimming ? `${l.brightness} %` : "on") : "off")),
          toggle(l.on, `${l.name} on/off`, (on) => hue({ action: on ? "on" : "off", id: l.id }))),
        lightControls(l, [l], `${room.id}-${l.id}`)))));
    }
  }
  return card;
}

function renderLights() {
  const hs = state["buddy/hue/state"];
  const online = state["buddy/hue/status"] === "online";
  const rooms = $("rooms");
  const note = {
    ok: "", connecting: "connecting to the bridge…", unreachable: "bridge not reachable", error: "bridge error",
    "key revoked": "bridge forgot Buddy: run buddy-hue --pair", "not paired": "not paired: run buddy-hue --pair",
  };
  $("bridge").textContent = !online ? "hue service offline" : hs ? (note[hs.bridge] ?? hs.bridge) : "";
  rooms.replaceChildren();
  if (!hs || !hs.rooms.length) {
    rooms.append(el("p", { class: "empty" }, online ? "No rooms yet." : "The hue service isn't running."));
    return;
  }
  const lightsById = Object.fromEntries(hs.lights.map((l) => [l.id, l]));
  for (const room of hs.rooms) rooms.append(roomCard(room, lightsById));
}

// --- music ------------------------------------------------------------------------------------------------------

function renderMusic() {
  const s = state["buddy/spotify/state"];
  const online = state["buddy/spotify/status"] === "online";
  $("music-note").textContent = !online ? "spotify service offline" : s && s.ducked ? "quieter while Buddy listens" : "";
  const box = $("music");
  box.replaceChildren();
  if (!online) {
    box.append(el("p", { class: "empty" }, "The spotify service isn't running."));
    return;
  }
  const playing = s && s.playing === true;
  const spotify = (action, query) => send("spotify", query === undefined ? { action } : { action, query });
  box.append(el("div", { class: "row" },
    el("button", { class: "btn", "aria-label": "Previous", onclick: () => spotify("previous") }, "⏮"),
    el("button", { class: "btn primary", "aria-label": playing ? "Pause" : "Play", onclick: () => spotify(playing ? "pause" : "play") }, playing ? "⏸" : "▶"),
    el("button", { class: "btn", "aria-label": "Next", onclick: () => spotify("next") }, "⏭"),
    el("div", { class: "name" }, playing ? "Playing" : s && s.playing === false ? "Paused" : "—",
      el("small", {}, "on the music box"))));
  if (s && typeof s.volume === "number") {
    box.append(el("div", { class: "row" }, slider({
      min: 0, max: 100, value: s.volume, label: "Volume", format: (v) => `${v} %`,
      onInput: (v) => throttled("volume", 400, () => spotify("set_volume", String(v))),
    })));
  }
}

// --- services ---------------------------------------------------------------------------------------------------

// Fixed list: stale retained statuses (removed services, test clients) never show up. Chip online = every member online.
const SERVICE_CHIPS = [
  { name: "voice", members: ["ears", "brain", "voice"] },
  { name: "hue", members: ["hue"] },
  { name: "spotify", members: ["spotify"] },
];

function renderServices() {
  const box = $("services");
  box.replaceChildren();
  for (const { name, members } of SERVICE_CHIPS) {
    const offline = members.filter((m) => state[`buddy/${m}/status`] !== "online");
    const online = offline.length === 0;
    const busy = members.map((m) => state[`buddy/${m}/state`]?.state).find((v) => typeof v === "string" && v !== "idle");
    const detail = online && busy ? busy : "";
    box.append(el("span", { class: `chip${online ? " online" : ""}`, title: online ? "online" : `offline: ${offline.join(", ")}` },
      name, detail && el("span", { class: "detail" }, detail)));
  }
}

function render() {
  renderServices();
  renderLights();
  renderMusic();
}

render();
connect();
