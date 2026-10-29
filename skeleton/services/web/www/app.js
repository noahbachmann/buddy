// Buddy panel: renders the retained MQTT state the web service streams (/api/events) and sends skill commands
// (POST /api/cmd/<service>). No framework, no build step: the Pi serves these files as they are.
"use strict";

const state = {};               // topic → payload (JSON, or a string like "online")
const expanded = new Set();     // room ids showing their single lights
let editing = false;            // cog pressed: rooms as a list to reorder, hide and add back
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
  setText($("music-note"), !online ? "spotify service offline" : s && s.ducked ? "quieter while Buddy listens" : "");
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
  if (document.visibilityState !== "visible" || location.hash === "#hue") return;
  fetch("/api/cmd/spotify", { method: "POST", headers: { "Content-Type": "application/json" }, body: '{"action":"watch"}' })
    .catch(() => {});  // offline: the link indicator says so
}
setInterval(watchSpotify, 20000);
document.addEventListener("visibilitychange", watchSpotify);
addEventListener("hashchange", watchSpotify);
watchSpotify();

// --- services ---------------------------------------------------------------------------------------------------

// Fixed list: stale retained statuses (removed services, test clients) never show up. Chip online = every member online.
const SERVICE_CHIPS = [
  { name: "voice", members: ["ears", "brain", "voice"] },
  { name: "hue", members: ["hue"] },
  { name: "spotify", members: ["spotify"] },
];

function renderServices() {
  keyed($("services"), SERVICE_CHIPS.map(({ name, members }) => {
    const offline = members.filter((m) => state[`buddy/${m}/status`] !== "online");
    const online = offline.length === 0;
    const busy = members.map((m) => state[`buddy/${m}/state`]?.state).find((v) => typeof v === "string" && v !== "idle");
    const detail = online && busy ? busy : "";
    return [name, JSON.stringify([offline, detail]), () => el("span",
      { class: `chip${online ? " online" : ""}`, title: online ? "online" : `offline: ${offline.join(", ")}` },
      name, detail && el("span", { class: "detail" }, detail))];
  }));
}

function render() {
  renderServices();
  const hue = location.hash === "#hue";
  $("page-dashboard").hidden = hue;
  $("page-hue").hidden = !hue;
  $("customize").hidden = hue;
  $("tab-dashboard").toggleAttribute("aria-current", !hue);
  $("tab-hue").toggleAttribute("aria-current", hue);
  if (hue) {
    renderHue();
  } else {
    renderLights();
    renderMusic();
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
