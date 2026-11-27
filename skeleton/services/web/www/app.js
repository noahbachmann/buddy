// Buddy panel: renders the retained MQTT state the web service streams (/api/events) and sends skill commands
// (POST /api/cmd/<service>). No framework, no build step: the Pi serves these files as they are.
"use strict";

const state = {};               // topic → payload (JSON, or a string like "online")
const expanded = new Set();     // room ids showing their single lights
let editing = false;            // cog pressed: rooms as a list to reorder, hide and add back
let interacting = 0;            // > 0 while a slider is held: re-rendering would yank it from the finger
let renderQueued = false;

const $ = (id) => document.getElementById(id);
const PAGES = ["dashboard", "hue", "riftbound"];  // #<page>, none = dashboard
const page = () => (PAGES.includes(location.hash.slice(1)) ? location.hash.slice(1) : "dashboard");

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

function setText(node, text) { if (node.textContent !== text) node.textContent = text; }

// Children rebuilt only when what they show changed: items are [key, sig, make], sig = JSON of make's inputs.
// Unchanged children keep their DOM nodes; a light change rebuilds one card, not the page.
let built = new WeakMap();  // container → Map(key → {sig, node})
function keyed(container, items) {
  const old = built.get(container) || new Map();
  const next = new Map();
  const nodes = items.map(([key, sig, make]) => {
    const o = old.get(key);
    const node = o && o.sig === sig ? o.node : make();
    next.set(key, { sig, node });
    return node;
  });
  built.set(container, next);
  const kids = container.childNodes;
  // Same slots, only new nodes in the changed ones: swap those. Otherwise (added, removed, moved) lay out anew.
  if (kids.length === nodes.length && nodes.every((n, i) => kids[i] === n || !n.isConnected)) {
    nodes.forEach((n, i) => { if (kids[i] !== n) kids[i].replaceWith(n); });
  } else {
    container.replaceChildren(...nodes);
  }
}

// Forget the built DOM: e.g. a switch flipped optimistically for a command that failed snaps back.
function rebuild() {
  built = new WeakMap();
  queueRender();
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
    if (!r.ok) {
      toast((await r.json().catch(() => ({}))).error || `${service}: HTTP ${r.status}`);
      rebuild();
    }
    return r.ok;
  } catch {
    toast("Buddy is not reachable");
    rebuild();
    return false;
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

// Light values a slider just set, shown until the state reports them. hue sends a room at most 1/s and the bridge
// reports each step ~1 s later: a slider let go at 70 % saw 30 → 45 → 60 → 70 meanwhile and jumped back
// (measured 2026-10-30). Kept 5 s at most: a light may clamp it (2000 K on a 2200 K lamp) and never report it.
const sent = new Map();  // "b:<id>" / "t:<id>" → { value, tol, until }
function hold(key, value, tol) {
  const until = Date.now() + 5000;
  sent.set(key, { value, tol, until });
  setTimeout(() => { if (sent.get(key)?.until === until) { sent.delete(key); rebuild(); } }, 5050);
}
function held(key, reported) {
  const s = sent.get(key);
  if (!s) return reported;
  if (Math.abs(reported - s.value) <= s.tol) sent.delete(key);  // caught up (rooms report an average: ±tol)
  return s.value;
}
const shownBrightness = (t) => held(`b:${t.id}`, t.brightness);

// RFC 6902 as the web service sends it (nlohmann's json::diff: add, remove, replace; JSON Pointer paths). Returns the
// patched value (changed in place); throws when the patch doesn't fit `doc`.
function applyPatch(doc, ops) {
  for (const { op, path, value } of ops) {
    if (path === "") {
      if (op === "remove") throw new Error("patch removes the whole payload");
      doc = value;
      continue;
    }
    const keys = path.split("/").slice(1).map((k) => k.replaceAll("~1", "/").replaceAll("~0", "~"));
    const last = keys.pop();
    let parent = doc;
    for (const k of keys) parent = parent?.[k];
    if (parent === null || typeof parent !== "object") throw new Error(`patch path ${path} not found`);
    if (Array.isArray(parent)) {
      const i = last === "-" ? parent.length : Number(last);
      if (!Number.isInteger(i) || i < 0 || i > parent.length - (op === "add" ? 0 : 1)) throw new Error(`patch index ${path}`);
      if (op === "add") parent.splice(i, 0, value);
      else if (op === "remove") parent.splice(i, 1);
      else parent[i] = value;
    } else {
      if (op !== "add" && !Object.hasOwn(parent, last)) throw new Error(`patch key ${path} not found`);
      if (op === "remove") delete parent[last];
      else parent[last] = value;
    }
  }
  return doc;
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
    const { topic, payload, patch } = JSON.parse(e.data);
    try {
      if (patch) state[topic] = applyPatch(state[topic], patch);  // against what this stream sent last
      else if (payload === null) delete state[topic];
      else state[topic] = payload;
    } catch (err) {
      console.warn(err);
      events.close();  // out of step: a new stream starts with every payload in full
      connect();
      return;
    }
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

// Light's current colour at full brightness as [r, g, b] 0..255, null = unknown (white-only lamp: no xy/mirek).
// mirek wins: the bridge reports an approximate xy in white mode too, and only sends mirek while it's valid.
function lightRgb(l) {
  if (l.mirek) return kelvinRgb(1e6 / l.mirek);
  if (l.xy) return xyRgb(l.xy[0], l.xy[1]);
  return null;
}

// Tanner Helland's blackbody fit, fine for 1000..40000 K
function kelvinRgb(k) {
  const t = k / 100;
  const r = t <= 66 ? 255 : 329.698727446 * (t - 60) ** -0.1332047592;
  const g = t <= 66 ? 99.4708025861 * Math.log(t) - 161.1195681661 : 288.1221695283 * (t - 60) ** -0.0755148492;
  const b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * Math.log(t - 10) - 305.0447927307;
  return [r, g, b].map((v) => Math.min(255, Math.max(0, v)));
}

// CIE xy → sRGB (Philips' wide-gamut matrix), normalised so the brightest channel is 1: hue, not dimming
function xyRgb(x, y) {
  if (y <= 0) return null;
  const X = x / y, Z = (1 - x - y) / y;
  const lin = [
    X * 1.656492 - 0.354851 - Z * 0.255038,
    -X * 0.707196 + 1.655397 + Z * 0.036152,
    X * 0.051713 - 0.121364 + Z * 1.01153,
  ].map((v) => Math.max(0, v));
  const max = Math.max(...lin);
  if (max <= 0) return null;
  const gamma = (v) => (v <= 0.0031308 ? 12.92 * v : 1.055 * v ** (1 / 2.4) - 0.055);
  return lin.map((v) => gamma(v / max) * 255);
}

// Average colour of the lights that are on (all of them while off: the switch shows it once turned on)
function lightStyle(lights) {
  const on = lights.filter((l) => l.on);
  const rgbs = (on.length ? on : lights).map(lightRgb).filter(Boolean);
  if (!rgbs.length) return undefined;
  let avg = [0, 1, 2].map((i) => rgbs.reduce((a, c) => a + c[i], 0) / rgbs.length);
  // near-white (cool/daylight) would hide the white switch knob: dim to a light grey of the same tint
  const max = Math.max(...avg), min = Math.min(...avg);
  if (max > 0 && (max - min) / max < 0.15) avg = avg.map((v) => v * 0.8);
  avg = avg.map(Math.round);
  return `--light: rgb(${avg.join(" ")})`;
}

function hue(cmd) { send("hue", cmd); }

function lightControls(target, lights, key) {
  const nodes = [];
  const dimmable = lights.some((l) => l.dimming);
  const colour = lights.some((l) => l.color);
  const white = lights.some((l) => l.temperature);
  if (target.on && dimmable) {
    nodes.push(el("div", { class: "row" }, slider({
      min: 1, max: 100, value: Math.max(1, shownBrightness(target)), label: `${target.name} brightness`,
      format: (v) => `${v} %`,
      onInput: (v) => {
        hold(`b:${target.id}`, v, 1);
        throttled(`b-${key}`, 300, () => hue({ action: "brightness", query: String(v), id: target.id }));
      },
    })));
  }
  if (target.on && white) {
    const mireks = lights.filter((l) => l.mirek).map((l) => l.mirek);
    const mirek = mireks.length ? mireks.reduce((a, b) => a + b) / mireks.length : 370;
    nodes.push(el("div", { class: "row" }, slider({
      min: 2000, max: 6500, value: held(`t:${target.id}`, Math.round(1e6 / mirek / 100) * 100), label: `${target.name} warm to cool`,
      format: (v) => `${v} K`, cls: "warmth",
      onInput: (v) => {
        hold(`t:${target.id}`, v, 100);
        throttled(`t-${key}`, 300, () => hue({ action: "temperature", query: String(v), id: target.id }));
      },
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
    : room.on ? `${onCount} of ${lights.length} on · ${shownBrightness(room)} %` : `${lights.length} light${lights.length > 1 ? "s" : ""} · off`;
  const card = el("article", { class: `card${room.on ? " lit" : ""}`, "aria-label": room.name, style: lightStyle(lights) },
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
      card.append(el("div", { class: "lights" }, lights.map((l) => el("div", { class: "light", style: lightStyle([l]) },
        el("div", { class: "row" },
          el("div", { class: "name" }, l.name, el("small", {}, l.on ? (l.dimming ? `${shownBrightness(l)} %` : "on") : "off")),
          toggle(l.on, `${l.name} on/off`, (on) => hue({ action: on ? "on" : "off", id: l.id }))),
        lightControls(l, [l], `${room.id}-${l.id}`)))));
    }
  }
  return card;
}

// Whole home: only "all off" here; switching everything on or tuning it is a room's job
function homeBar(home, lightsById) {
  const onCount = home.lights.filter((id) => lightsById[id]?.on).length;
  return el("div", { class: "home-bar", "aria-label": home.name },
    el("div", { class: "name" }, home.name, el("small", {}, onCount ? `${onCount} on` : "all off")),
    el("button", { class: "chip-btn", disabled: !home.on, onclick: () => hue({ action: "off", id: home.id }) }, "Turn off"));
}

// Room order and hidden rooms, per browser (phone and PC may want different panels). Rooms the bridge adds later
// show up at the end; ids of deleted rooms stay in here harmlessly.
const LAYOUT_KEY = "buddy.lights.layout";
function loadLayout() {
  try {
    const l = JSON.parse(localStorage.getItem(LAYOUT_KEY));
    if (l && Array.isArray(l.order) && Array.isArray(l.hidden)) return l;
  } catch { /* private mode or garbage: default layout */ }
  return { order: [], hidden: [] };
}
const layout = loadLayout();
function saveLayout() {
  try { localStorage.setItem(LAYOUT_KEY, JSON.stringify(layout)); } catch { /* layout lasts until reload */ }
}

function orderedRooms(rooms) {
  const rank = (r) => { const i = layout.order.indexOf(r.id); return i < 0 ? Infinity : i; };
  return rooms.map((r, i) => [r, i]).sort((a, b) => rank(a[0]) - rank(b[0]) || a[1] - b[1]).map(([r]) => r);
}

function moveRoom(visible, i, by) {
  const ids = visible.map((r) => r.id);
  [ids[i], ids[i + by]] = [ids[i + by], ids[i]];
  layout.order = [...ids, ...layout.order.filter((id) => !ids.includes(id))];
  saveLayout();
  render();
}

function setHidden(id, hide) {
  layout.hidden = layout.hidden.filter((h) => h !== id);
  if (hide) layout.hidden.push(id);
  saveLayout();
  render();
}

function editList(rooms) {
  const visible = rooms.filter((r) => !layout.hidden.includes(r.id));
  const hidden = rooms.filter((r) => layout.hidden.includes(r.id));
  const icon = (label, text, onclick, disabled) =>
    el("button", { class: "icon-btn", "aria-label": label, title: label, disabled, onclick }, text);
  const nodes = visible.map((r, i) => el("div", { class: "edit-row" },
    el("div", { class: "name" }, r.name),
    icon(`Move ${r.name} up`, "↑", () => moveRoom(visible, i, -1), i === 0),
    icon(`Move ${r.name} down`, "↓", () => moveRoom(visible, i, 1), i === visible.length - 1),
    icon(`Hide ${r.name}`, "✕", () => setHidden(r.id, true))));
  if (hidden.length) {
    nodes.push(el("div", { class: "edit-hidden" }, el("span", { class: "muted" }, "Hidden"),
      hidden.map((r) => el("button", { class: "chip-btn", onclick: () => setHidden(r.id, false) }, `+ ${r.name}`))));
  }
  return el("div", { class: "edit-list" }, nodes);
}

function renderLights() {
  const hs = state["buddy/hue/state"];
  const online = state["buddy/hue/status"] === "online";
  const rooms = $("rooms");
  const note = {
    ok: "", connecting: "connecting to the bridge…", unreachable: "bridge not reachable", error: "bridge error",
    "key revoked": "bridge forgot Buddy: run buddy-hue --pair", "not paired": "not paired: run buddy-hue --pair",
  };
  setText($("bridge"), !online ? "hue service offline" : hs ? (note[hs.bridge] ?? hs.bridge) : "");
  if (!hs || !hs.rooms.length) return keyed(rooms, [emptyNote(online ? "No rooms yet." : "The hue service isn't running.")]);
  const ordered = orderedRooms(hs.rooms);
  if (editing) {
    const sig = JSON.stringify([ordered.map((r) => [r.id, r.name]), layout.hidden]);
    return keyed(rooms, [["edit", sig, () => editList(ordered)]]);
  }
  const lightsById = Object.fromEntries(hs.lights.map((l) => [l.id, l]));
  const shown = ordered.filter((r) => !layout.hidden.includes(r.id));
  if (!shown.length) return keyed(rooms, [emptyNote("All rooms hidden: add them back with ⚙.")]);
  keyed(rooms, shown.map((room) => {
    const sig = JSON.stringify([room, room.lights.map((id) => lightsById[id]), expanded.has(room.id)]);
    return [room.id, sig, () => (room.kind === "home" ? homeBar(room, lightsById) : roomCard(room, lightsById))];
  }));
}

function emptyNote(text) { return [`empty:${text}`, "", () => el("p", { class: "empty" }, text)]; }

// --- hue tab: scenes -------------------------------------------------------------------------------------------

let savingScene = null;  // {group, name}: toast once the new scene shows up in the bridge's state
let sceneGroupsKey = "";

function sceneGroups() {
  const hs = state["buddy/hue/state"];
  return hs ? hs.rooms.filter((r) => r.kind !== "home") : [];  // the bridge keeps scenes per room or zone
}

function sameName(a, b) { return a.trim().toLowerCase() === b.trim().toLowerCase(); }

function renderHue() {
  const hs = state["buddy/hue/state"];
  const groups = sceneGroups();
  const select = $("scene-group");
  const key = groups.map((g) => `${g.id}:${g.name}`).join("|");
  if (key !== sceneGroupsKey) {  // rebuilt only when rooms change: an open dropdown survives updates
    const keep = select.value;
    select.replaceChildren(...groups.map((g) => el("option", { value: g.id }, g.kind === "zone" ? `${g.name} (zone)` : g.name)));
    if (groups.some((g) => g.id === keep)) select.value = keep;
    sceneGroupsKey = key;
  }
  const group = groups.find((g) => g.id === select.value);

  const lightsById = hs ? Object.fromEntries(hs.lights.map((l) => [l.id, l])) : {};
  const lights = group ? group.lights.map((id) => lightsById[id]).filter(Boolean) : [];
  keyed($("scene-lights"), lights.length ? lights.map((l) => [l.id, JSON.stringify(l), () => el("div", { class: "light", style: lightStyle([l]) },
    el("div", { class: "row" },
      el("div", { class: "name" }, l.name, el("small", {}, l.on ? (l.dimming ? `${shownBrightness(l)} %` : "on") : "off")),
      toggle(l.on, `${l.name} on/off`, (on) => hue({ action: on ? "on" : "off", id: l.id }))),
    lightControls(l, [l], `scene-${l.id}`))])
    : [emptyNote(hs ? "No rooms or zones with lights." : "The hue service isn't running.")]);

  if (savingScene) {
    const g = groups.find((x) => x.id === savingScene.group);
    if (g && g.scenes.some((s) => sameName(s.name, savingScene.name))) {
      toast(`Saved ${savingScene.name}`);
      savingScene = null;
    }
  }

  keyed($("scene-list"), groups.map((g) => [g.id, JSON.stringify([g.name, g.scenes]), () => el("div", { class: "scene-group" },
    el("div", { class: "scene-group-name" }, g.name),
    g.scenes.length ? el("div", { class: "controls" }, g.scenes.map((s) => el("span", { class: "scene-chip" },
      el("button", {
        class: "scene-load", title: `Show ${s.name} and load it for editing`,
        onclick: () => {
          hue({ action: "scene", query: s.name, id: g.id });
          select.value = g.id;
          $("scene-name").value = s.name;
          render();
        },
      }, s.name),
      el("button", {
        class: "scene-x", "aria-label": `Delete ${s.name} in ${g.name}`, title: "Delete",
        onclick: () => {
          if (confirm(`Delete the scene "${s.name}" in ${g.name}? It's gone from the Hue app too.`)) hue({ action: "scene_delete", id: s.id });
        },
      }, "✕"))))
      : el("span", { class: "muted" }, "no scenes"))]));
}

$("scene-group").addEventListener("change", () => render());
$("scene-form").addEventListener("submit", async (e) => {
  e.preventDefault();
  const group = sceneGroups().find((g) => g.id === $("scene-group").value);
  const name = $("scene-name").value.trim();
  if (!group || !name) return;
  const existing = group.scenes.find((s) => sameName(s.name, name));
  if (existing && !confirm(`"${existing.name}" already exists in ${group.name}. Replace it with the lights as they are now?`)) return;
  if (!existing) savingScene = { group: group.id, name };
  if (await send("hue", { action: "scene_save", query: name, id: group.id }) && existing) toast(`Updated ${existing.name}`);
  $("scene-name").value = "";
});

// --- music ------------------------------------------------------------------------------------------------------

function renderMusic() {
  const s = state["buddy/spotify/state"];
  const online = state["buddy/spotify/status"] === "online";
  const nest = state["buddy/speakers/state"];  // music on a Nest: speakers ducks it, not spotify (D20)
  const ducked = s?.ducked || (nest?.ducked && nest?.music === "playing");
  setText($("music-note"), !online ? "spotify service offline" : ducked ? "quieter while Buddy listens" : "");
  const box = $("music");
  if (!online) return keyed(box, [emptyNote("The spotify service isn't running.")]);
  const playing = s && s.playing === true;
  const device = s?.device ?? null;
  const track = s?.track ?? null;
  // On the device that plays; without one, the service picks (play → the box)
  const spotify = (action, query, id = device?.id) =>
    send("spotify", { action, ...(query !== undefined && { query }), ...(id && { id }) });

  const items = [["now", JSON.stringify([track, device?.name ?? null, s?.playing ?? null]), () => el("div", { class: "row" },
    track?.image ? el("img", { class: "art", src: track.image, alt: "" }) : el("div", { class: "art" }),
    el("div", { class: "name" }, track ? track.name : playing ? "Playing" : "Nothing playing",
      el("small", {}, [track?.artist, device && `on ${device.name}`].filter(Boolean).join(" · ") || "\u00a0")))]];
  if (track && s.progress) {
    items.push(["progress", JSON.stringify([s.progress.duration_ms, !!device]), () => {
      const out = el("span", { class: "range-label time", id: "music-time" });
      const input = el("input", {
        type: "range", min: 0, max: Math.round(s.progress.duration_ms / 1000), value: 0, id: "music-seek",
        "aria-label": "Position", class: "seek",
        oninput: (e) => { out.textContent = clock(+e.target.value * 1000); },
        onchange: (e) => {
          if (!device) return;
          const ms = +e.target.value * 1000;
          spotify("seek", String(ms));
          s.progress = { ...s.progress, ms, at: Date.now() };  // until the poll confirms: no jump back
        },
        onpointerdown: () => { interacting++; },
      });
      return el("div", { class: "row" }, input, out);
    }]);
  }
  items.push(["controls", JSON.stringify(s?.playing ?? null), () => el("div", { class: "row" },
    el("button", { class: "btn", "aria-label": "Previous", onclick: () => spotify("previous") }, "⏮"),
    el("button", { class: "btn primary", "aria-label": playing ? "Pause" : "Play", onclick: () => spotify(playing ? "pause" : "play") }, playing ? "⏸" : "▶"),
    el("button", { class: "btn", "aria-label": "Next", onclick: () => spotify("next") }, "⏭"))]);
  if (s && typeof s.volume === "number") {
    items.push(["volume", String(s.volume), () => el("div", { class: "row" }, slider({
      min: 0, max: 100, value: s.volume, label: "Volume", format: (v) => `${v} %`,
      onInput: (v) => throttled("volume", 400, () => spotify("set_volume", String(v))),
    }))]);
  }
  const devices = Array.isArray(s?.devices) ? s.devices : [];
  items.push(["speakers", JSON.stringify(devices.map((d) => [d.id, d.name, d.active])), () => el("div", { class: "speakers" },
    el("span", { class: "muted" }, "Speakers"),
    devices.length ? devices.map((d) => el("button", {
      class: `chip-btn${d.active ? " active" : ""}`, "aria-pressed": String(d.active), title: d.type,
      onclick: () => { if (!d.active) { spotify("play", undefined, d.id); toast(`Moving the music to ${d.name}`); } },
    }, d.name))
      : el("span", { class: "muted" }, "none: open Spotify on a phone or PC"))]);
  keyed(box, items);
  updateProgress();
}

// 61000 → "1:01"
function clock(ms) {
  const t = Math.max(0, Math.floor(ms / 1000));
  return `${Math.floor(t / 60)}:${String(t % 60).padStart(2, "0")}`;
}

// The service publishes progress only when it jumps (seek, pause, new song): the bar runs on here.
function updateProgress() {
  const s = state["buddy/spotify/state"];
  const input = $("music-seek");
  if (!input || !s?.progress || interacting > 0) return;
  const { ms, duration_ms, at } = s.progress;
  const pos = Math.min(duration_ms, ms + (s.playing ? Math.max(0, Date.now() - at) : 0));
  input.value = String(Math.round(pos / 1000));
  setText($("music-time"), `${clock(pos)} / ${clock(duration_ms)}`);
}
setInterval(updateProgress, 1000);

// Spotify can't push: the service polls only while some panel shows the music card and says so every 20 s.
function watchSpotify() {
  if (document.visibilityState !== "visible" || page() !== "dashboard") return;
  fetch("/api/cmd/spotify", { method: "POST", headers: { "Content-Type": "application/json" }, body: '{"action":"watch"}' })
    .catch(() => {});  // offline: the link indicator says so
}
setInterval(watchSpotify, 20000);
document.addEventListener("visibilitychange", watchSpotify);
addEventListener("hashchange", watchSpotify);
watchSpotify();

// --- system -----------------------------------------------------------------------------------------------------

// One card per host: this Pi publishes buddy/system/state, extra hosts buddy/system-<name>/state (1.17).
function renderSystem() {
  const hosts = Object.keys(state)
    .filter((t) => /^buddy\/system(-[a-z0-9-]+)?\/state$/.test(t) && state[t]?.cpu)
    .map((t) => ({ svc: t.slice("buddy/".length, -"/state".length), s: state[t] }))
    .sort((a, b) => (a.s.label || a.svc).localeCompare(b.s.label || b.svc));
  const box = $("system");
  if (!hosts.length) return keyed(box, [emptyNote("The system service isn't running.")]);
  keyed(box, hosts.map(({ svc, s }) => {
    const online = state[`buddy/${svc}/status`] === "online";
    return [svc, JSON.stringify([s, online]), () => systemCard(s, online)];
  }));
}

function meter(name, frac, text, warn) {
  const pct = Math.max(0, Math.min(100, Math.round((frac || 0) * 100)));
  return el("div", { class: "row" },
    el("span", { class: "meter-name" }, name),
    el("div", { class: "meter", role: "meter", "aria-label": name, "aria-valuenow": pct },
      el("div", { class: `meter-fill${warn ? " warn" : ""}`, style: `width:${pct}%` })),
    el("span", { class: "range-label meter-val" }, text));
}

const kib = (bps) => (bps >= 1024 * 1024 ? `${(bps / 1024 / 1024).toFixed(1)} MiB/s` : `${Math.round(bps / 1024)} KiB/s`);

function systemCard(s, online) {
  const flags = s.throttled ?? {};
  const details = [
    typeof s.temp_c === "number" && `${s.temp_c.toFixed(1).replace(/\.0$/, "")} °C`,
    s.fan_rpm != null && `fan ${s.fan_rpm} rpm`,
    s.net && `↓ ${kib(s.net.rx_bps)} ↑ ${kib(s.net.tx_bps)}`,
  ].filter(Boolean).join(" · ");
  const gb = (mb) => (mb / 1024).toFixed(1);
  return el("div", { class: `card${online ? "" : " offline"}` },
    el("div", { class: "row" },
      el("div", { class: "name" }, s.label, el("small", {}, online ? details || " " : "offline")),
      (flags.now ?? []).map((f) => el("span", { class: "flag bad", title: "active now" }, f)),
      (flags.ever ?? []).filter((f) => !(flags.now ?? []).includes(f))
        .map((f) => el("span", { class: "flag", title: "happened since boot" }, f))),
    meter("CPU", (s.cpu.pct ?? 0) / 100, s.cpu.pct != null ? `${s.cpu.pct} %${s.cpu.mhz ? ` · ${(s.cpu.mhz / 1000).toFixed(1)} GHz` : ""}` : "–",
      (s.cpu.pct ?? 0) >= 90),
    meter("RAM", s.mem.used_mb / s.mem.total_mb, `${gb(s.mem.used_mb)} / ${gb(s.mem.total_mb)} GB`,
      s.mem.used_mb / s.mem.total_mb >= 0.9),
    meter("Disk", s.disk.used_gb / s.disk.total_gb, `${s.disk.used_gb.toFixed(1)} / ${s.disk.total_gb.toFixed(1)} GB`,
      s.disk.used_gb / s.disk.total_gb >= 0.9));
}

// --- screener (#80) --------------------------------------------------------------------------------------------

// buddy/screener/state: counts and kinds only (never names). The browse loop's state, today's counts, stop/pause/resume.
const SCREENER_STATE = { idle: "idle", browsing: "browsing", break: "on a break", waiting: "outside its hours",
  done_today: "likes used up", limit: "usage limit", needs_you: "needs you", paused: "paused", stopped: "stopped",
  failed: "loop failed (see the log)", offline: "phone offline" };
const SCREENER_HOLD = { would_like: "a like waits on the phone", screen: "a screen it couldn't handle",
  likes: "the likes didn't come back", decide: "decisions keep failing", files: "preferences/templates refused",
  chat_message: "a chat message waits on the phone", chat_unmatch: "an unmatch waits for you",
  chat_screen: "a chat screen it couldn't handle", chat_files: "chat files refused" };
const hhmm = (ts) => new Date(ts * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });

function renderScreener() {
  const s = state["buddy/screener/state"];
  const online = state["buddy/screener/status"] === "online";
  $("screener-section").hidden = !s;
  if (!s) return;
  keyed($("screener"), [["screener", JSON.stringify([s, online]), () => screenerCard(s, online)]]);
}

function screenerCard(s, online) {
  const t = s.today ?? {};
  const what = [SCREENER_STATE[s.state] ?? s.state, s.hold && SCREENER_HOLD[s.hold], s.until && `until ${hhmm(s.until)}`]
    .filter(Boolean).join(" · ");
  const cmd = (action) => send("screener", { action });
  const stopped = s.state === "stopped", paused = s.state === "paused" || s.state === "needs_you";
  return el("div", { class: `card${online ? "" : " offline"}` },
    el("div", { class: "row" },
      el("div", { class: "name" }, `Screener (${s.mode ?? "off"})`, el("small", {}, online ? what : "offline")),
      s.phone !== "ok" ? el("span", { class: "flag bad", title: "phone" }, `phone ${s.phone}`) : null),
    meter("Likes", t.likes_max ? (t.likes_max - (t.likes_left ?? 0)) / t.likes_max : 0,
      `${(t.likes_max ?? 0) - (t.likes_left ?? 0)} / ${t.likes_max ?? 0}`),
    el("div", { class: "row muted" },
      `Today: ${t.profiles ?? 0} profiles, ${t.skips ?? 0} skipped, ${t.likes ?? 0} liked` +
      (t.would_like ? `, ${t.would_like} held` : "") +
      (s.last ? ` · last: ${s.last.action} at ${hhmm(s.last.ts)}` : "")),
    s.chats ? el("div", { class: "row muted" }, screenerChats(s)) : null,
    el("div", { class: "controls" },
      el("button", { class: "chip-btn", disabled: !online || stopped, onclick: () => cmd("stop") }, "Stop"),
      el("button", { class: "chip-btn", disabled: !online || stopped || paused, onclick: () => cmd("pause") }, "Pause"),
      el("button", { class: "chip-btn", disabled: !online || !(stopped || paused), onclick: () => cmd("resume") }, "Resume")));
}

// The chats (#77): counts by state, never a name or a message.
function screenerChats(s) {
  const c = s.chats;
  const parts = [["open", "open"], ["held", "held"], ["handover", "need you"], ["done", "got your contact"],
    ["manual", "yours"], ["closed", "closed"], ["liked", "likes waiting"]]
    .filter(([k]) => c[k]).map(([k, label]) => `${c[k]} ${label}`);
  return `Chats (${s.chat_mode ?? "off"}): ${parts.length ? parts.join(", ") : "none"}`;
}

// --- riftbound (2.6) --------------------------------------------------------------------------------------------

// buddy/game/state is built from cards everyone knows (public_view): Buddy's hand is a count, Dev's real cards are
// named by Dev in prompts. Dev answers with `act {v, i}` (one of `options`) or `reveal {v, cards}` (the prompt).
const RB_DOMAIN = { Fury: "#d6453a", Calm: "#3c9f66", Mind: "#3a74d4", Body: "#e0862a", Chaos: "#8a52d4", Order: "#c9a52c" };
const RB_STATUS = {
  setup: "Buddy sets up the table…", thinking: "Buddy is thinking…", acting: "Buddy is moving cards…",
  dev_to_act: "Your move", dev_to_reveal: "Name your card",
};
const RB_PURPOSE = {
  target: "Choose a target", may: "Use the ability?", "optional cost": "Pay the optional cost?", discard: "Discard a card",
  "recycle from trash": "Recycle from your trash", "trigger order": "Which ability goes on the chain next?",
  effect: "Choose", location: "Where does it go?", move: "Which units move?", stage: "Which showdown first?",
  "assign damage": "Assign combat damage",
};
const RB_PROMPT = {
  battlefield: "Which battlefield did you put down?", hand: "Which card left your hand?",
  deck: "Which card came off your deck?", runes: "Which runes did you channel?",
};

let rbPending = null;            // {v, timer}: an act/reveal is on its way, until the payload moves on
let rbPick = { key: "", codes: [] };  // the prompt's selection so far (codes, in tap order)
let rbSheet = null;              // open card sheet: {id} (a board object), {code} (a card), {pile: "trash"|…, who}
let rbAll = false;               // every option as a list (fallback to tapping cards)

const rbGame = () => state["buddy/game/state"];
const rbCard = (code) => rbGame()?.catalogue?.[code];
const rbName = (code) => (code ? rbCard(code)?.name ?? code : "a hidden card");
const rbShort = (code) => rbName(code).split(",")[0];

// ":rb_might:" & co. in Riftcodex text → small symbols; ")Next" (reminder text ends a line) → line break
const RB_ICON = { might: "⚔", exhaust: "↷", rune_rainbow: "◈" };
function rbText(text) {
  return (text || "").replace(/\)(?=[A-Z[])/g, ")\n").split(/(:rb_[a-z0-9_]+:)/).filter(Boolean).map((part) => {
    const m = /^:rb_([a-z0-9_]+):$/.exec(part);
    if (!m) return part;
    const k = m[1];
    if (k.startsWith("energy_")) return el("span", { class: "rb-energy", title: "Energy" }, k.slice(7));
    if (k.startsWith("rune_") && k !== "rune_rainbow") {
      const d = k[5].toUpperCase() + k.slice(6);
      return el("span", { class: "rb-rune", style: `--dom:${RB_DOMAIN[d] ?? "var(--muted)"}`, title: d }, d);
    }
    return el("span", { class: "rb-sym", title: k.replace("_", " ") }, RB_ICON[k] ?? k);
  });
}

function rbSend(cmd) {
  const g = rbGame();
  if (rbPending) return;
  const v = g?.v;
  // game answers an accepted act/reveal/resume with a new payload at once; none within 4 s = refused (stale v, …)
  rbPending = { v, timer: setTimeout(() => {
    if (rbPending?.v !== v) return;
    rbPending = null;
    toast("Buddy didn't take that: the game had moved on. Try again.");
    queueRender();
  }, 4000) };
  if (rbSheet && $("rb-sheet").open) $("rb-sheet").close();
  const withV = cmd.action === "act" || cmd.action === "reveal";  // resume/concede take no fields: v would be refused
  send("game", { ...cmd, ...(withV && v !== undefined && { v }) }).then((ok) => {
    if (!ok && rbPending?.v === v) { clearTimeout(rbPending.timer); rbPending = null; queueRender(); }
  });
  queueRender();
}
const rbAct = (o) => rbSend({ action: "act", i: o.i });

// Where each option goes: on a board object, on a hand card (by code), or the bar (pass, keep, done, …).
function rbPlace(g) {
  const onBoard = new Set();
  for (const who of ["buddy", "dev"]) {
    const p = g.players?.[who];
    if (!p) continue;
    for (const o of [p.legend, ...p.champion, ...p.base]) if (o) onBoard.add(o.id);
  }
  for (const f of g.battlefields ?? []) {
    onBoard.add(f.id);
    for (const o of [...f.units.buddy, ...f.units.dev]) onBoard.add(o.id);
  }
  const byObj = new Map(), byCard = new Map(), bar = [];
  const push = (m, k, o) => { if (!m.has(k)) m.set(k, []); m.get(k).push(o); };
  for (const o of g.options ?? []) {
    if (o.obj !== undefined && onBoard.has(o.obj)) push(byObj, o.obj, o);
    else if (o.card) push(byCard, o.card, o);
    else bar.push(o);
  }
  return { byObj, byCard, bar };
}

// Every object the payload shows, by id (for the sheet and attached gear)
function rbObjects(g) {
  const m = new Map();
  for (const who of ["buddy", "dev"]) {
    const p = g.players?.[who];
    if (!p) continue;
    for (const o of [p.legend, ...p.champion, ...p.base]) if (o) m.set(o.id, { ...o, owner: who });
  }
  for (const f of g.battlefields ?? []) {
    m.set(f.id, { id: f.id, card: f.card, owner: f.owner, battlefield: f });
    for (const who of ["buddy", "dev"]) for (const o of f.units[who]) m.set(o.id, { ...o, owner: who });
  }
  return m;
}

function rbObj(o, opts, where) {
  if (o.hidden) return el("div", { class: "rb-obj hidden" }, "facedown");
  const c = rbCard(o.card);
  const full = rbName(o.card);
  const [head, ...rest] = full.split(", ");
  const stats = [];
  if (o.might !== undefined) stats.push(el("span", { class: "rb-might", title: "Might" }, `⚔${o.might}`));
  if (o.damage) stats.push(el("span", { class: "rb-dmg", title: "Damage" }, `−${o.damage}`));
  if (o.buffs) stats.push(el("span", { class: "rb-tag" }, `+${o.buffs}`));
  if (o.stunned) stats.push(el("span", { class: "rb-tag bad" }, "stunned"));
  if (o.empowered) stats.push(el("span", { class: "rb-tag" }, "empowered"));
  if (o.attached_to !== undefined) stats.push(el("span", { class: "rb-tag" }, "gear"));
  const n = opts?.length ?? 0;
  return el("button", {
    class: `rb-obj${o.exhausted ? " exhausted" : ""}${n ? " can" : ""}${c?.type === "Legend" ? " legend" : ""}`,
    "aria-label": `${full}${o.exhausted ? ", exhausted" : ""}${n ? `, ${n} move${n > 1 ? "s" : ""}` : ""}`,
    onclick: () => rbOpen({ id: o.id }),
  },
  el("span", { class: "rb-obj-name" }, head),
  rest.length ? el("small", {}, rest.join(", ")) : where && el("small", {}, where),
  stats.length ? el("span", { class: "rb-stats" }, stats) : null,
  o.keywords ? el("small", { class: "rb-kw" }, o.keywords.join(" · ")) : null,
  n ? el("span", { class: "rb-badge" }, String(n)) : null);
}

function rbRunes(p) {
  const runes = p.runes.map((r) => el("span", {
    class: "rb-rune", style: `--dom:${RB_DOMAIN[r.domain] ?? "var(--muted)"}`,
    title: `${r.domain}: ${r.ready} ready, ${r.exhausted} exhausted`,
  }, `${r.domain} ${r.ready}/${r.ready + r.exhausted}`));
  const pool = [p.pool.energy && `${p.pool.energy} energy`, ...Object.entries(p.pool.power).map(([d, v]) => `${v} ${d}`),
    p.pool.any && `${p.pool.any} any`].filter(Boolean);
  if (pool.length) runes.push(el("span", { class: "rb-tag" }, `pool ${pool.join(", ")}`));
  return runes;
}

function rbSide(g, who, place) {
  const p = g.players[who];
  const me = who === "dev";
  const pile = (name, list) => el("button", {
    class: "chip-btn rb-pile", disabled: !list.length, onclick: () => rbOpen({ pile: name, who }),
  }, `${name} ${list.length}`);
  const head = el("div", { class: "rb-side-head" },
    el("span", { class: "rb-who" }, me ? "You" : "Buddy"),
    el("span", { class: "rb-points", title: `${p.points} of ${g.victory} points` }, `${p.points}`, el("small", {}, `/${g.victory}`)),
    el("span", { class: "muted" }, `hand ${p.hand} · deck ${p.deck} · runes ${p.rune_deck}`),
    pile("trash", p.trash), p.banishment.length ? pile("banished", p.banishment) : null);
  const objs = [p.legend, ...p.champion, ...p.base].filter(Boolean)
    .map((o) => rbObj(o, place.byObj.get(o.id), p.champion.includes(o) ? "champion zone" : null));
  return el("div", { class: `rb-side ${who}${g.turn_player === who ? " turn" : ""}` },
    me ? null : head,
    el("div", { class: "rb-runes" }, rbRunes(p)),
    el("div", { class: "rb-row" }, objs),
    me ? head : null);
}

function rbField(g, f, i, place) {
  const sd = g.showdown && g.showdown.battlefield === i ? g.showdown : null;
  const opts = place.byObj.get(f.id);
  const tag = [
    f.controller && el("span", { class: `rb-tag ${f.controller}` }, f.controller === "dev" ? "yours" : "Buddy's"),
    f.contested && el("span", { class: "rb-tag warn" }, "contested"),
    sd && el("span", { class: "rb-tag warn" }, `${sd.combat ? "combat" : "showdown"} · focus ${sd.focus === "dev" ? "you" : "Buddy"}`),
    f.facedown && el("span", { class: "rb-tag" }, `${f.facedown} facedown`),
  ].filter(Boolean);
  const units = (who) => el("div", { class: `rb-row rb-units ${who}` },
    f.units[who].length ? f.units[who].map((o) => rbObj(o, place.byObj.get(o.id))) : el("span", { class: "rb-none" }, "–"));
  return el("div", { class: `rb-field${sd ? " fight" : ""}${opts ? " can" : ""}` },
    units("buddy"),
    el("button", {
      class: "rb-field-name", onclick: () => rbOpen({ id: f.id }),
      "aria-label": `${rbName(f.card)}${opts ? `, ${opts.length} move${opts.length > 1 ? "s" : ""}` : ""}`,
    }, el("span", {}, rbName(f.card)), opts ? el("span", { class: "rb-badge" }, String(opts.length)) : null),
    tag.length ? el("div", { class: "rb-field-tags" }, tag) : null,
    units("dev"));
}

function rbChain(g) {
  if (!g.chain?.length) return null;
  return el("ol", { class: "rb-chain", "aria-label": "Chain, newest first" }, [...g.chain].reverse().map((c) => el("li", {},
    el("button", { class: "rb-link", onclick: () => c.card && rbOpen({ code: c.card }) },
      `${c.controller === "dev" ? "You" : "Buddy"}: ${rbName(c.card)}${c.kind === "card" ? "" : ` (${c.kind})`}`),
    c.targets.length ? ` → ${c.targets.map((t) => (t.player ? (t.player === "dev" ? "you" : "Buddy") : rbShort(t.card))).join(", ")}` : "")));
}

function rbStatusLine(g) {
  if (!g || g.status === "idle") return "No game running.";
  if (g.status === "over") return g.winner ? (g.winner === "dev" ? "You won!" : "Buddy won.") : "Game over.";
  if (g.status === "paused") return g.pause?.message ?? "Paused.";
  let s = RB_STATUS[g.status] ?? g.status;
  if (g.status === "dev_to_act" && g.decision?.kind === "mulligan") s = "Mulligan: set aside up to 2 cards, then keep";
  else if (g.status === "dev_to_act" && g.decision?.kind === "recycle") s = "Which rune do you recycle?";
  else if (g.status === "dev_to_act" && g.decision?.kind === "choose") s = RB_PURPOSE[g.decision.purpose] ?? s;
  return s;
}

function renderRiftbound() {
  const g = rbGame();
  const online = state["buddy/game/status"] === "online";
  if (rbPending && g?.v !== rbPending.v) { clearTimeout(rbPending.timer); rbPending = null; }
  const running = g && !["idle", "over"].includes(g.status);
  $("rb-new").hidden = running || !online;
  setText($("rb-game"), !online ? "game service offline" : state["buddy/table/status"] !== "online" ? "table offline" : "");

  const score = g?.players && el("span", { class: "rb-score" }, `Buddy ${g.players.buddy.points} – ${g.players.dev.points} you`);
  const where = g?.turn ? `Turn ${g.turn} · ${g.turn_player === "dev" ? "your" : "Buddy's"} ${g.phase}` : "";
  keyed($("rb-status"), [["s", JSON.stringify([g?.status, g?.pause, g?.decision, g?.winner, g?.turn, g?.phase, g?.turn_player,
    g?.players?.buddy.points, g?.players?.dev.points, online, !!rbPending]), () => el("div", {
    class: `card rb-status-card ${g?.status ?? "idle"}${g?.status === "dev_to_act" || g?.status === "dev_to_reveal" ? " yours" : ""}`,
  },
  el("div", { class: "row" },
    el("div", { class: "name" }, !online ? "The game service isn't running." : rbPending ? "Sending…" : rbStatusLine(g),
      el("small", {}, where || " ")),
    score))]]);

  if (!g?.players) {
    keyed($("rb-board"), []);
    keyed($("rb-act"), []);
    keyed($("rb-log"), []);
    return rbRenderSheet();
  }
  const place = rbPlace(g);
  const sig = (x) => JSON.stringify([x, g.catalogue ? Object.keys(g.catalogue).length : 0]);
  const optSig = JSON.stringify(g.options ?? []);
  keyed($("rb-board"), [
    ["buddy", sig([g.players.buddy, g.turn_player, optSig]), () => rbSide(g, "buddy", place)],
    ["chain", sig(g.chain), () => rbChain(g) ?? el("div", { hidden: true })],
    ["fields", sig([g.battlefields, g.showdown, optSig]), () => el("div", { class: "rb-fields" },
      g.battlefields.map((f, i) => rbField(g, f, i, place)))],
    ["dev", sig([g.players.dev, g.turn_player, optSig]), () => rbSide(g, "dev", place)],
  ]);

  keyed($("rb-act"), [["act", JSON.stringify([g.status, g.pause, g.options, g.prompt, g.decision, g.voice, rbPick, rbAll, !!rbPending]),
    () => rbActions(g, place)]]);
  keyed($("rb-log"), (g.log ?? []).map((l, i) => [`${i}:${l.turn}:${l.text}`, "", () => el("li", { class: l.player },
    el("span", { class: "muted" }, `T${l.turn}`), " ", el("b", {}, l.player === "dev" ? "You" : "Buddy"), " ", l.text)]).reverse());
  rbRenderSheet();
}

function rbActions(g, place) {
  const nodes = [];
  const busy = !!rbPending;
  if (g.status === "dev_to_reveal" && g.prompt) nodes.push(rbPrompt(g));
  if (g.status === "dev_to_act") {
    // Buddy doesn't know Dev's hand: every card that could be in it, by name (find the one you hold)
    const hand = [...place.byCard.entries()].sort(([a], [b]) => rbName(a).localeCompare(rbName(b)));
    if (hand.length) {
      nodes.push(el("div", { class: "rb-hand" }, el("span", { class: "muted" }, "From your hand: tap the card you hold"),
        el("div", { class: "rb-row" }, hand.map(([code, opts]) => el("button", {
          class: "rb-obj can", disabled: busy, onclick: () => rbOpen({ code }),
        }, el("span", { class: "rb-obj-name" }, rbName(code)), el("small", {}, rbCost(rbCard(code))),
        opts.length > 1 ? el("span", { class: "rb-badge" }, String(opts.length)) : null)))));
    }
    if (place.bar.length) {
      nodes.push(el("div", { class: "controls rb-bar" }, place.bar.map((o) => el("button", {
        class: `text-btn${o.group === "pass" ? " primary" : ""}`, disabled: busy, onclick: () => rbAct(o),
      }, rbCap(o.text)))));
    }
    if (place.byObj.size) nodes.push(el("p", { class: "muted rb-hint" }, "Cards with a number have moves: tap them."));
    nodes.push(el("button", { class: "more", "aria-expanded": String(rbAll), onclick: () => { rbAll = !rbAll; render(); } },
      rbAll ? "Hide the list ▴" : `All ${g.options.length} moves as a list ▾`));
    if (rbAll) nodes.push(rbOptionList(g.options, busy));
  }
  if (g.status === "paused" && g.pause?.resumable) {
    nodes.push(el("div", { class: "controls" }, el("button", {
      class: "text-btn primary", disabled: busy, onclick: () => rbSend({ action: "resume" }),
    }, "Resume")));
    nodes.push(el("p", { class: "muted" }, "Fix the table first (put the card back, free the hand pile), then resume."));
  } else if (g.status === "paused") {
    nodes.push(el("p", { class: "muted" }, "This game can't go on: concede it, then start a new one."));
  }
  if (!["idle", "over"].includes(g.status)) {
    // off by default: game then sends no lines to the voice service at all (no speech synthesized)
    nodes.push(el("div", { class: "controls rb-concede" },
      el("span", { class: "muted" }, "Buddy speaks"),
      toggle(!!g.voice, "Buddy speaks", (on) => send("game", { action: "voice", on })),
      el("button", {
      class: "chip-btn", disabled: busy,
      onclick: () => { if (confirm("Concede this game?")) rbSend({ action: "concede" }); },
    }, "Concede")));
  }
  return el("div", {}, nodes);
}

const rbCap = (t) => t.charAt(0).toUpperCase() + t.slice(1);
function rbCost(c) {
  if (!c) return "";
  const parts = [];
  if (c.cost !== undefined) parts.push(`${c.cost} energy`);
  if (c.power) parts.push(`+${c.power} ${c.domains.join("/")}`);
  return [c.type, parts.join(" ")].filter(Boolean).join(" · ");
}

function rbOptionList(options, busy) {
  const groups = ["play", "activate", "move", "choose", "pass"];
  const names = { play: "Play", activate: "Abilities", move: "Move", choose: "Choose", pass: "Pass" };
  return el("div", { class: "card rb-list" }, groups.filter((gr) => options.some((o) => o.group === gr)).map((gr) =>
    el("div", { class: "rb-list-group" }, el("div", { class: "muted" }, names[gr]),
      el("div", { class: "controls" }, options.filter((o) => o.group === gr).map((o) => el("button", {
        class: "chip-btn", disabled: busy, onclick: () => rbAct(o),
      }, rbCap(o.text)))))));
}

// Prompt: Dev names the cards that just turned public. Tap a candidate to add a copy, a picked chip to take it back.
// Runes come as whole combinations (one tap per channel step) when there are few.
function rbPrompt(g) {
  const p = g.prompt;
  const key = JSON.stringify(p);
  if (rbPick.key !== key) rbPick = { key, codes: [] };
  const picked = rbPick.codes;
  const count = (code) => picked.filter((c) => c === code).length;
  const set = (codes) => { rbPick = { key, codes }; render(); };
  const busy = !!rbPending;
  const nodes = [el("div", { class: "rb-prompt-q" }, RB_PROMPT[p.kind] ?? "Name the card",
    p.n > 1 ? el("small", {}, ` ${p.n} cards`) : null)];
  const combos = p.kind === "runes" ? rbCombos(p.candidates, p.n) : null;
  if (combos && combos.length > 1 && combos.length <= 6) {
    nodes.push(el("div", { class: "controls" }, combos.map((codes) => el("button", {
      class: `chip-btn${JSON.stringify(codes) === JSON.stringify([...picked].sort()) ? " active" : ""}`,
      onclick: () => set(codes),
    }, rbComboText(codes)))));
  } else {
    nodes.push(el("div", { class: "rb-row" }, p.candidates.map((c) => {
      const n = count(c.code);
      const full = n >= c.left || picked.length >= p.n;
      return el("button", {
        class: `rb-obj${n ? " picked" : ""}`, disabled: full && !n,
        style: c.domain ? `--dom:${RB_DOMAIN[c.domain] ?? "var(--muted)"}` : undefined,
        onclick: () => { if (!full) set([...picked, c.code]); },
      }, el("span", { class: "rb-obj-name" }, rbName(c.code)),
      el("small", {}, [rbCost(rbCard(c.code)), c.left > 1 && `${c.left} left`].filter(Boolean).join(" · ")),
      n ? el("span", { class: "rb-badge" }, `${n}`) : null);
    })));
  }
  if (picked.length) {
    nodes.push(el("div", { class: "controls" }, el("span", { class: "muted" }, "Picked:"), picked.map((code, i) => el("button", {
      class: "chip-btn", "aria-label": `Take back ${rbName(code)}`, onclick: () => set(picked.filter((_, j) => j !== i)),
    }, `${rbShort(code)} ✕`))));
  }
  nodes.push(el("div", { class: "controls" }, el("button", {
    class: "text-btn primary", disabled: busy || picked.length !== p.n,
    onclick: () => rbSend({ action: "reveal", cards: picked }),
  }, `Confirm${p.n > 1 ? ` (${picked.length}/${p.n})` : ""}`)));
  return el("div", { class: "card rb-prompt" }, nodes);
}

// Multisets of n candidate codes within their `left` counts, sorted codes each
function rbCombos(cands, n) {
  const out = [];
  const walk = (i, acc) => {
    if (acc.length === n) return out.push([...acc]);
    if (i >= cands.length || out.length > 6) return;
    for (let k = Math.min(cands[i].left, n - acc.length); k >= 0; k--) {
      walk(i + 1, [...acc, ...Array(k).fill(cands[i].code)]);
    }
  };
  walk(0, []);
  return out.map((c) => c.sort());
}
function rbComboText(codes) {
  const m = new Map();
  for (const c of codes) m.set(c, (m.get(c) ?? 0) + 1);
  return [...m].map(([c, k]) => `${k} × ${rbShort(c).replace(/ Rune$/, "")}`).join(" + ");
}

// The card sheet: text of any card, an object's state and the moves on it. Rebuilt with every state update while open.
function rbOpen(what) {
  rbSheet = what;
  rbSheetSig = "";
  rbRenderSheet();
  const d = $("rb-sheet");
  if (!d.open) d.showModal();
}
let rbSheetSig = "";
function rbRenderSheet() {
  const d = $("rb-sheet");
  if (!rbSheet || !d.open && rbSheetSig) return;
  const g = rbGame();
  if (!g?.players) return;
  const place = rbPlace(g);
  const objs = rbObjects(g);
  let body;
  const sigOf = [rbSheet, !!rbPending];
  if (rbSheet.pile) {
    const list = g.players[rbSheet.who][rbSheet.pile === "trash" ? "trash" : "banishment"];
    sigOf.push(list);
    body = [el("h2", { id: "rb-sheet-title" }, `${rbSheet.who === "dev" ? "Your" : "Buddy's"} ${rbSheet.pile}`),
      el("div", { class: "controls" }, list.map((code) => el("button", {
        class: "chip-btn", onclick: () => rbOpen({ code }),
      }, rbName(code))))];
  } else {
    const o = rbSheet.id !== undefined ? objs.get(rbSheet.id) : null;
    const code = o ? o.card : rbSheet.code;
    const opts = (o ? place.byObj.get(o.id) : place.byCard.get(code)) ?? [];
    sigOf.push(o, opts, code);
    const c = rbCard(code);
    const state_ = o && !o.battlefield ? [
      o.owner === "dev" ? "yours" : "Buddy's", o.exhausted ? "exhausted" : "ready",
      o.might !== undefined && `Might ${o.might}${o.damage ? `, ${o.damage} damage` : ""}`,
      o.buffs && `+${o.buffs} buff`, o.stunned && "stunned", o.empowered && "empowered",
      o.attached_to !== undefined && `on ${rbName(objs.get(o.attached_to)?.card)}`,
      o.keywords?.join(", "),
    ].filter(Boolean).join(" · ") : o?.battlefield ? [
      o.owner === "dev" ? "your battlefield" : "Buddy's battlefield",
      o.battlefield.controller && `held by ${o.battlefield.controller === "dev" ? "you" : "Buddy"}`,
    ].filter(Boolean).join(" · ") : "";
    body = [
      el("h2", { id: "rb-sheet-title" }, rbName(code)),
      el("p", { class: "muted" }, [rbCost(c), c?.might !== undefined && `Might ${c.might}`, c?.domains?.join("/"),
        c?.tags?.length && c.tags.join(", ")].filter(Boolean).join(" · ")),
      state_ && el("p", { class: "rb-sheet-state" }, state_),
      c?.text ? el("p", { class: "rb-text" }, rbText(c.text)) : null,
      opts.length ? el("div", { class: "rb-sheet-opts" }, opts.map((x) => el("button", {
        class: "text-btn primary", disabled: !!rbPending, onclick: () => rbAct(x),
      }, rbCap(x.text)))) : null,
    ];
  }
  const sig = JSON.stringify(sigOf);
  if (sig === rbSheetSig) return;
  rbSheetSig = sig;
  $("rb-sheet-body").replaceChildren(...body.filter(Boolean));
}
$("rb-sheet").addEventListener("close", () => { rbSheet = null; rbSheetSig = ""; });
$("rb-sheet").addEventListener("click", (e) => { if (e.target === e.currentTarget) e.currentTarget.close(); });  // backdrop

$("rb-new-form").addEventListener("submit", (e) => {
  e.preventDefault();
  send("game", { action: "new", buddy: $("rb-new-buddy").value, dev: $("rb-new-dev").value, voice: $("rb-new-voice").checked });
});

// --- services ---------------------------------------------------------------------------------------------------

// Fixed list: stale retained statuses (removed services, test clients) never show up. Chip online = every member online.
// detail: what the chip says besides its name; default = the first member state that isn't "idle".
const SERVICE_CHIPS = [
  { name: "voice", members: ["ears", "brain", "voice"] },
  { name: "hue", members: ["hue"] },
  { name: "spotify", members: ["spotify"] },
  { name: "speakers", members: ["speakers"] },
  { name: "game", members: ["game", "table"], detail: gameDetail },
  { name: "messenger", members: ["messenger"], detail: messengerDetail },
  { name: "screener", members: ["screener"], detail: screenerDetail },
];

// The loop's state unless idle; a hold first (Dev is needed).
function screenerDetail() {
  const s = state["buddy/screener/state"];
  if (!s || s.state === "idle") return "";
  return s.state === "needs_you" ? "needs you" : SCREENER_STATE[s.state] ?? s.state;
}

// A Riftbound game running: what it waits for (buddy/game/state's status; table has no /state of its own).
const GAME_DETAIL = { setup: "setup", dev_to_act: "your move", dev_to_reveal: "name a card", thinking: "thinking",
  acting: "playing", paused: "paused" };
function gameDetail() {
  return GAME_DETAIL[state["buddy/game/state"]?.status] || "";
}

// Link trouble first (Dev wouldn't get the question), else how many questions wait in the chat.
function messengerDetail() {
  const s = state["buddy/messenger/state"];
  if (typeof s?.link !== "string") return "";
  if (s.link !== "ok") return s.link;
  return s.open > 0 ? `${s.open} open` : "";
}

function renderServices() {
  keyed($("services"), SERVICE_CHIPS.map(({ name, members, detail: own }) => {
    const offline = members.filter((m) => state[`buddy/${m}/status`] !== "online");
    const online = offline.length === 0;
    const busy = () => members.map((m) => state[`buddy/${m}/state`]?.state).find((v) => typeof v === "string" && v !== "idle");
    const detail = (online && (own ? own() : busy())) || "";
    return [name, JSON.stringify([offline, detail]), () => el("span",
      { class: `chip${online ? " online" : ""}`, title: online ? "online" : `offline: ${offline.join(", ")}` },
      name, detail && el("span", { class: "detail" }, detail))];
  }));
}

function render() {
  renderServices();
  const current = page();
  for (const p of PAGES) {
    $(`page-${p}`).hidden = p !== current;
    $(`tab-${p}`).toggleAttribute("aria-current", p === current);
  }
  $("customize").hidden = current !== "dashboard";
  if (current === "hue") {
    renderHue();
  } else if (current === "riftbound") {
    renderRiftbound();
  } else {
    renderLights();
    renderMusic();
    renderSystem();
    renderScreener();
  }
}

addEventListener("hashchange", () => render());

$("customize").addEventListener("click", (e) => {
  editing = !editing;
  e.currentTarget.setAttribute("aria-pressed", String(editing));
  render();
});

render();
connect();
