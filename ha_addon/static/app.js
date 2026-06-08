// ─── Constants ────────────────────────────────────────────────────────────────
const MAX_SCREENS = 8;
const MAX_TILES   = 12;
const MAX_ROWS    = 6;
const ESP_W       = 1280;
const ESP_H       = 692;
const SNAP        = 16;
const MIN_W       = 128;
const MIN_H       = 56;
const HANDLE_PX   = 14;

// ─── Helpers ──────────────────────────────────────────────────────────────────
const api = (path) => `${BASE}/api${path}`;
const esc = (s) => String(s ?? "")
  .replace(/&/g,"&amp;").replace(/</g,"&lt;")
  .replace(/>/g,"&gt;").replace(/"/g,"&quot;");

// ─── i18n ─────────────────────────────────────────────────────────────────────
let _L = localStorage.getItem("lang") || "en";

const T = {
  en: {
    checking: "Checking...", not_configured_ip: "IP not configured",
    max_screens: (n) => `Maximum ${n} screens.`,
    screen_name: (n) => `Screen ${n}`, delete_screen: "Delete screen?",
    set_done: "★ Set",
    max_tiles: (n) => `Maximum ${n} tiles per screen.`,
    tile_name: (n) => `Tile ${n}`, tile_name_ph: "Tile name",
    delete_tile: "Delete tile?",
    max_rows: (n) => `Maximum ${n} rows in tile.`,
    max_tile_rows: (n) => `Tile can have max ${n} rows.`,
    add_row: "+ Row",
    drop_hint: `↓ Drag entity here or click "+ Row"`,
    drop_hint_small: (n) => `↓ Drop entity to add row (${n} more)`,
    rows_full: (n) => `Row limit ${n} reached`,
    relay_badge: "🔌 Relay", sensor_badge: "📊 Sensor", presence_badge: "👤 Presence", device_badge: "📍 Device",
    entity_label: "Entity", choose_btn: "Choose",
    label_label: "Label", row_name_ph: "Row name",
    type_label: "Type", sensor_opt: "Sensor", relay_opt: "Relay", presence_opt: "Presence", device_opt: "Device",
    sensor_type_label: "Sensor type",
    display_mode_label: "Display style",
    display_mode_text: "Text",
    display_mode_arc: "Arc gauge",
    device_active: "Active", device_inactive: "Away",
    presence_home: "Home", presence_away: "Away", presence_zone: "Zone",
    unit_label: "Unit", unit_ph: "e.g. °C",
    attr_label: "HA Attribute", attr_ph: "(optional)",
    sw_off: "OFF", sw_on: "ON",
    layout_vertical: "Vertical", layout_horizontal: "Horizontal",
    delete_tile_title: "Delete tile",
    st_generic: "Generic", st_temperature: "Temperature", st_humidity: "Humidity",
    st_cpu: "CPU", st_memory: "Memory", st_power: "Power/Energy", st_illuminance: "Illuminance",
    canvas_drop: "↓ drop entity",
    canvas_props: (i, n) => `📐 Properties — Tile ${i}/${n}`,
    close_btn: "× Close",
    empty_hint: `Click <strong>+ Screen</strong> to add a screen,<br>then <strong>+ Add tile</strong>.`,
    saving: "Saving...", saved: "✓ Saved", save_error: "Save error",
    sending: "Sending...", sent_ok: "✓ Sent! (device restarting…)",
    conn_error: "Connection error",
    downloading: "Downloading...", downloaded: "✓ Downloaded",
    error_msg: (e) => `Error: ${e}`,
    no_screens_field: "Missing 'screens' field",
    invalid_json: (e) => `Invalid JSON file: ${e}`,
    no_results: "No results", loading_error: "Error loading entities",
    tts_no_rules: `No rules. Click "+ Add rule" to start.`,
    tts_entity_ph: "— pick entity —", tts_text_ph: "e.g. John left home",
    tts_saved: "✓ Saved", tts_save_error: "Error", tts_net_error: "Network error",
    tts_test_alert: "Enter rule text before testing.",
    tts_error_prefix: (e) => `Error: ${e}`,
    tts_net_error_prefix: (e) => `Network error: ${e}`,
    tts_state_hint: "* = any state",
    btn_save: "Save", btn_send: "Send to device", btn_fetch: "Fetch from device",
    // Camera tile
    tile_type: "Tile type", tile_type_normal: "Normal", tile_type_camera: "Camera",
    cam_entity_label: "Camera entity", cam_pick: "Pick camera",
    cam_refresh: "Refresh (s)", cam_no_entity: "No camera selected",
    // HTML static elements
    tab_tts: "Voice notifications",
    ha_entities: "HA Entities",
    search: "Search...", search_entities: "Search entities...",
    loading_entities: "Loading entities...",
    screen: "Screen", add_tile: "Add tile",
    btn_default: "★ Default", set_default_title: "Set as default screen (shown on startup)",
    view_list: "List", view_list_title: "List view", view_canvas_title: "Canvas view",
    canvas_hint: "Drag tile to move &nbsp;•&nbsp; ▪ bottom-right corner = resize &nbsp;•&nbsp; Grid 16 px &nbsp;•&nbsp; Drop entity from list onto tile",
    pick_entity: "Pick entity",
    tts_desc: "When a Home Assistant entity changes state, the ESP32 panel speaks the defined text through the ES8311 speaker. Requires speaker enabled on the panel and a TTS engine <strong>Piper</strong> (or other WAV) in HA.",
    tts_col_active: "Active", tts_col_entity: "Entity",
    tts_col_from: "State: from", tts_col_to: "State: to", tts_col_text: "Text to speak",
    tts_add_rule: "Add rule", tts_save_rules: "Save rules",
    tts_hint_summary: "Typical state values",
    state_home: "at home", state_not_home: "away",
    state_on: "on / detected", state_off: "off / clear",
    state_locked: "locked", state_unlocked: "unlocked",
    state_armed: "armed", state_disarmed: "disarmed",
    tts_wildcard_hint: `The "State: from/to" field accepts <code>*</code> as any state. Example: from <code>*</code> to <code>not_home</code> triggers every time the entity changes to not_home.`,
  },
  pl: {
    checking: "sprawdzam...", not_configured_ip: "Nie skonfigurowano IP",
    max_screens: (n) => `Maksymalnie ${n} ekranów.`,
    screen_name: (n) => `Ekran ${n}`, delete_screen: "Usunąć ekran?",
    set_done: "★ Ustawiono",
    max_tiles: (n) => `Maksymalnie ${n} kafelków na ekranie.`,
    tile_name: (n) => `Kafelek ${n}`, tile_name_ph: "Nazwa kafelka",
    delete_tile: "Usunąć kafelek?",
    max_rows: (n) => `Maksymalnie ${n} wierszy w kafelku.`,
    max_tile_rows: (n) => `Kafelek może mieć maksymalnie ${n} wierszy.`,
    add_row: "+ Wiersz",
    drop_hint: `↓ Przeciągnij encję tutaj lub kliknij „+ Wiersz"`,
    drop_hint_small: (n) => `↓ Upuść encję aby dodać wiersz (jeszcze ${n})`,
    rows_full: (n) => `Limit ${n} wierszy osiągnięty`,
    relay_badge: "🔌 Przekaźnik", sensor_badge: "📊 Sensor", presence_badge: "👤 Obecność", device_badge: "📍 Urządzenie",
    entity_label: "Encja", choose_btn: "Wybierz",
    label_label: "Etykieta", row_name_ph: "Nazwa wiersza",
    type_label: "Typ", sensor_opt: "Sensor", relay_opt: "Przekaźnik", presence_opt: "Obecność", device_opt: "Urządzenie",
    sensor_type_label: "Typ sensora",
    display_mode_label: "Sposób wyświetlania",
    display_mode_text: "Tekst",
    display_mode_arc: "Wskaźnik kołowy",
    device_active: "Aktywny", device_inactive: "Nieobecny",
    presence_home: "W domu", presence_away: "Nieobecny", presence_zone: "Strefa",
    unit_label: "Jednostka", unit_ph: "np. °C",
    attr_label: "Atrybut HA", attr_ph: "(opcjonalne)",
    sw_off: "WYŁĄCZONY", sw_on: "WŁĄCZONY",
    layout_vertical: "Pionowy", layout_horizontal: "Poziomy",
    delete_tile_title: "Usuń kafelek",
    st_generic: "Ogólny", st_temperature: "Temperatura", st_humidity: "Wilgotność",
    st_cpu: "CPU", st_memory: "Pamięć", st_power: "Moc/Energia", st_illuminance: "Oświetlenie",
    canvas_drop: "↓ upuść encję",
    canvas_props: (i, n) => `📐 Właściwości — Kafelek ${i}/${n}`,
    close_btn: "× Zamknij",
    empty_hint: `Kliknij <strong>+ Ekran</strong> aby dodać ekran,<br>następnie <strong>+ Dodaj kafelek</strong>.`,
    saving: "Zapisywanie...", saved: "✓ Zapisano", save_error: "Błąd zapisu",
    sending: "Wysyłanie...", sent_ok: "✓ Wysłano! (restart urządzenia…)",
    conn_error: "Błąd połączenia",
    downloading: "Pobieranie...", downloaded: "✓ Pobrano",
    error_msg: (e) => `Błąd: ${e}`,
    no_screens_field: "Brak pola 'screens'",
    invalid_json: (e) => `Błędny plik JSON: ${e}`,
    no_results: "Brak wyników", loading_error: "Błąd ładowania encji",
    tts_no_rules: `Brak reguł. Kliknij „+ Dodaj regułę" aby zacząć.`,
    tts_entity_ph: "— wybierz encję —", tts_text_ph: "Np. Jan wyszedł z domu",
    tts_saved: "✓ Zapisano", tts_save_error: "Błąd", tts_net_error: "Błąd sieci",
    tts_test_alert: "Wpisz tekst reguły przed testem.",
    tts_error_prefix: (e) => `Błąd: ${e}`,
    tts_net_error_prefix: (e) => `Błąd sieci: ${e}`,
    tts_state_hint: "* = dowolny stan",
    btn_save: "Zapisz", btn_send: "Wyślij do urządzenia", btn_fetch: "Pobierz z urządzenia",
    // Camera tile
    tile_type: "Typ kafelka", tile_type_normal: "Normalny", tile_type_camera: "Kamera",
    cam_entity_label: "Encja kamery", cam_pick: "Wybierz kamerę",
    cam_refresh: "Odświeżanie (s)", cam_no_entity: "Nie wybrano kamery",
    // HTML static elements
    tab_tts: "Powiadomienia głosowe",
    ha_entities: "Encje HA",
    search: "Szukaj...", search_entities: "Szukaj encji...",
    loading_entities: "Ładowanie encji...",
    screen: "Ekran", add_tile: "Dodaj kafelek",
    btn_default: "★ Domyślny", set_default_title: "Ustaw jako ekran domyślny (wyświetlany po starcie)",
    view_list: "Lista", view_list_title: "Widok listy", view_canvas_title: "Widok canvas",
    canvas_hint: "Przeciągnij kafelek aby przesunąć &nbsp;•&nbsp; ▪ prawy dolny róg = zmień rozmiar &nbsp;•&nbsp; Siatka 16 px &nbsp;•&nbsp; Upuść encję z listy na kafelek",
    pick_entity: "Wybierz encję",
    tts_desc: `Gdy encja Home Assistant zmieni stan, panel ESP32 wypowie zdefiniowany tekst przez głośnik ES8311. Wymaga włączonego głośnika na panelu oraz silnika TTS <strong>Piper</strong> (lub innego WAV) w HA.`,
    tts_col_active: "Aktywna", tts_col_entity: "Encja",
    tts_col_from: "Stan: z", tts_col_to: "Stan: na", tts_col_text: "Tekst do wypowiedzenia",
    tts_add_rule: "Dodaj regułę", tts_save_rules: "Zapisz reguły",
    tts_hint_summary: "Typowe wartości stanów",
    state_home: "w domu", state_not_home: "poza domem",
    state_on: "włączony / wykryto", state_off: "wyłączony / brak",
    state_locked: "zamknięty", state_unlocked: "otwarty",
    state_armed: "uzbrojony", state_disarmed: "wyłączony",
    tts_wildcard_hint: `Pole „Stan: z/na" przyjmuje <code>*</code> jako dowolny stan. Przykład: z <code>*</code> na <code>not_home</code> uruchomi regułę za każdym razem gdy encja zmieni stan na not_home.`,
  },
};

function sensorTypeOptions() {
  return [
    ["generic",     T[_L].st_generic],
    ["temperature", T[_L].st_temperature],
    ["humidity",    T[_L].st_humidity],
    ["cpu",         T[_L].st_cpu],
    ["memory",      T[_L].st_memory],
    ["power",       T[_L].st_power],
    ["illuminance", T[_L].st_illuminance],
  ];
}

function applyLang() {
  document.querySelectorAll("[data-i18n]").forEach(el => {
    const k = el.getAttribute("data-i18n");
    if (T[_L][k] !== undefined) el.textContent = T[_L][k];
  });
  document.querySelectorAll("[data-i18n-html]").forEach(el => {
    const k = el.getAttribute("data-i18n-html");
    if (T[_L][k] !== undefined) el.innerHTML = T[_L][k];
  });
  document.querySelectorAll("[data-i18n-ph]").forEach(el => {
    const k = el.getAttribute("data-i18n-ph");
    if (T[_L][k] !== undefined) el.placeholder = T[_L][k];
  });
  document.querySelectorAll("[data-i18n-title]").forEach(el => {
    const k = el.getAttribute("data-i18n-title");
    if (T[_L][k] !== undefined) el.title = T[_L][k];
  });
  const btn = document.getElementById("lang-btn");
  if (btn) btn.textContent = _L === "en" ? "PL" : "EN";
  document.documentElement.lang = _L;
}

function toggleLang() {
  _L = _L === "en" ? "pl" : "en";
  localStorage.setItem("lang", _L);
  applyLang();
  render();
  renderTtsRules();
}

// ─── State ────────────────────────────────────────────────────────────────────
const state = {
  dashboard:          { default_screen: 0, screens: [] },
  entities:           [],
  cameraEntities:     [],
  currentScreen:      0,
  entitySearch:       "",
  viewMode:           "canvas",  // "list" | "canvas"
  selectedCanvasTile: -1,
};

let entityModalCallback = null;

// ─── Canvas drag state ────────────────────────────────────────────────────────
let dragState = null;
// { tileIdx, mode:"move"|"resize", startMX, startMY, origX, origY, origW, origH }

// ─── Init ──────────────────────────────────────────────────────────────────────
async function init() {
  applyLang();
  await Promise.all([loadDashboard(), loadEntities(), loadCameraEntities(), checkDeviceStatus()]);
  setInterval(checkDeviceStatus, 10000);
  bindEvents();
  initCanvasResizeObserver();
  render();
}

async function loadCameraEntities() {
  try {
    const r = await fetch(api("/camera_entities"));
    const d = await r.json();
    if (Array.isArray(d)) state.cameraEntities = d;
  } catch { state.cameraEntities = []; }
}

// ─── Data loading ──────────────────────────────────────────────────────────────
async function loadDashboard() {
  try {
    const r = await fetch(api("/dashboard"));
    const d = await r.json();
    state.dashboard = d;
    if (!Array.isArray(state.dashboard.screens))    state.dashboard.screens = [];
    if (state.dashboard.default_screen == null)     state.dashboard.default_screen = 0;
  } catch (e) { console.error("loadDashboard:", e); }
}

async function loadEntities() {
  try {
    const r = await fetch(api("/entities"));
    const d = await r.json();
    if (Array.isArray(d)) state.entities = d;
    renderEntityPanel();
  } catch {
    document.getElementById("entity-list").innerHTML =
      `<div class="loading" style="color:#ef4444">${T[_L].loading_error}</div>`;
  }
}

async function checkDeviceStatus() {
  try {
    const r = await fetch(api("/device_status"));
    const d = await r.json();
    const el = document.getElementById("device-status");
    if (d.online) { el.textContent = `● ${d.ip}`;  el.className = "status-badge online"; }
    else { el.textContent = d.ip ? `○ ${d.ip} (offline)` : T[_L].not_configured_ip;
           el.className = "status-badge offline"; }
  } catch { /* silent */ }
}

// ─── Events ────────────────────────────────────────────────────────────────────
function bindEvents() {
  document.getElementById("btn-add-screen").onclick   = addScreen;
  document.getElementById("btn-add-tile").onclick     = addTile;
  document.getElementById("btn-add-tile-canvas").onclick = addTile;
  document.getElementById("btn-save").onclick         = saveDashboard;
  document.getElementById("btn-send").onclick         = sendToDevice;
  document.getElementById("btn-fetch").onclick        = fetchFromDevice;
  document.getElementById("btn-export").onclick       = exportJSON;
  document.getElementById("btn-import").onclick       = () =>
    document.getElementById("file-import").click();
  document.getElementById("file-import").onchange     = importJSON;
  document.getElementById("btn-set-default").onclick  = setDefaultScreen;
  document.getElementById("btn-view-list").onclick    = () => setViewMode("list");
  document.getElementById("btn-view-canvas").onclick  = () => setViewMode("canvas");

  document.getElementById("entity-search").oninput = (e) => {
    state.entitySearch = e.target.value.toLowerCase();
    renderEntityPanel();
  };
  document.getElementById("modal-search").oninput = (e) =>
    renderModalEntities(e.target.value.toLowerCase());

  // ── Drag source: entity list ──────────────────────────────────────────────
  document.getElementById("entity-list").addEventListener("dragstart", (e) => {
    const item = e.target.closest(".entity-item[draggable]");
    if (!item?.dataset.eid) return;
    e.dataTransfer.setData("text/plain", item.dataset.eid);
    e.dataTransfer.effectAllowed = "copy";
    item.classList.add("dragging");
  });
  document.getElementById("entity-list").addEventListener("dragend", (e) => {
    e.target.closest(".entity-item")?.classList.remove("dragging");
  });

  // ── Drag target: tiles-container (widok listy) ────────────────────────────
  const listContainer = document.getElementById("tiles-container");
  listContainer.addEventListener("dragover", (e) => {
    const tile = e.target.closest(".tile-card");
    if (!tile) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = "copy";
    document.querySelectorAll(".tile-card.drag-over")
      .forEach(t => t.classList.remove("drag-over"));
    tile.classList.add("drag-over");
  });
  listContainer.addEventListener("dragleave", (e) => {
    const tile = e.target.closest(".tile-card");
    if (tile && !tile.contains(e.relatedTarget)) tile.classList.remove("drag-over");
  });
  listContainer.addEventListener("drop", (e) => {
    e.preventDefault();
    const tile = e.target.closest(".tile-card");
    if (!tile) return;
    tile.classList.remove("drag-over");
    const entityId = e.dataTransfer.getData("text/plain");
    const tileIdx  = parseInt(tile.dataset.tileIdx, 10);
    if (isValidEntityId(entityId) && !isNaN(tileIdx)) addEntityToTile(tileIdx, entityId);
  });

  // ── Drag target: esp-canvas (widok canvas) ────────────────────────────────
  const viewCanvasEl = document.getElementById("view-canvas");
  viewCanvasEl.addEventListener("dragover", (e) => {
    const tile = e.target.closest(".canvas-tile");
    if (!tile) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = "copy";
    document.querySelectorAll(".canvas-tile.drag-over")
      .forEach(t => t.classList.remove("drag-over"));
    tile.classList.add("drag-over");
  });
  viewCanvasEl.addEventListener("dragleave", (e) => {
    const tile = e.target.closest(".canvas-tile");
    if (tile && !tile.contains(e.relatedTarget)) tile.classList.remove("drag-over");
  });
  viewCanvasEl.addEventListener("drop", (e) => {
    e.preventDefault();
    const tile = e.target.closest(".canvas-tile");
    if (!tile) return;
    tile.classList.remove("drag-over");
    const entityId = e.dataTransfer.getData("text/plain");
    const tileIdx  = parseInt(tile.dataset.tileIdx, 10);
    if (isValidEntityId(entityId) && !isNaN(tileIdx)) addEntityToTile(tileIdx, entityId);
  });

  // ── Canvas: mysz (move/resize kafelków) ───────────────────────────────────
  document.getElementById("esp-canvas").addEventListener("mousedown", onCanvasMouseDown);
  document.addEventListener("mousemove", onCanvasMouseMove);
  document.addEventListener("mouseup",   onCanvasMouseUp);
}

// ─── View management ──────────────────────────────────────────────────────────
function setViewMode(mode) {
  state.viewMode = mode;
  document.getElementById("view-list")  .classList.toggle("hidden", mode !== "list");
  document.getElementById("view-canvas").classList.toggle("hidden", mode !== "canvas");
  document.getElementById("btn-view-list")  .classList.toggle("active", mode === "list");
  document.getElementById("btn-view-canvas").classList.toggle("active", mode === "canvas");
  if (mode === "canvas") {
    initCanvasResizeObserver();
    renderCanvas();
  }
}

let _canvasObsInited = false;
function initCanvasResizeObserver() {
  if (_canvasObsInited) return;
  _canvasObsInited = true;
  const scroll = document.getElementById("canvas-scroll");
  new ResizeObserver(() => {
    if (state.viewMode === "canvas") renderCanvas();
  }).observe(scroll);
}

// ─── Dashboard mutations ───────────────────────────────────────────────────────
function addScreen() {
  if (state.dashboard.screens.length >= MAX_SCREENS) {
    alert(T[_L].max_screens(MAX_SCREENS)); return;
  }
  state.dashboard.screens.push({ name: T[_L].screen_name(state.dashboard.screens.length + 1), tiles: [] });
  state.currentScreen = state.dashboard.screens.length - 1;
  render();
}

function deleteScreen(idx) {
  if (!confirm(T[_L].delete_screen)) return;
  state.dashboard.screens.splice(idx, 1);
  if (state.dashboard.default_screen >= state.dashboard.screens.length)
    state.dashboard.default_screen = Math.max(0, state.dashboard.screens.length - 1);
  state.currentScreen = Math.max(0, Math.min(state.currentScreen, state.dashboard.screens.length - 1));
  render();
}

function switchScreen(idx) {
  state.currentScreen = idx;
  state.selectedCanvasTile = -1;
  render();
}

function setDefaultScreen() {
  if (!state.dashboard.screens.length) return;
  state.dashboard.default_screen = state.currentScreen;
  render();
  const btn = document.getElementById("btn-set-default");
  btn.textContent = T[_L].set_done;
  setTimeout(() => {
    btn.innerHTML = `<span data-i18n="btn_default">${T[_L].btn_default}</span>`;
  }, 1500);
}

function addTile() {
  if (!state.dashboard.screens.length) addScreen();
  const screen = state.dashboard.screens[state.currentScreen];
  if (screen.tiles.length >= MAX_TILES) {
    alert(T[_L].max_tiles(MAX_TILES)); return;
  }
  const idx = screen.tiles.length;
  screen.tiles.push({
    type: "normal", label: T[_L].tile_name(idx + 1), layout: "vertical",
    camera_entity: "", refresh_s: 10,
    x: (idx % 2) * (488 + 16) + 8,
    y: Math.floor(idx / 2) * (200 + 16) + 8,
    w: 488, h: 200, rows: [],
  });
  render();
}

function deleteTile(tileIdx) {
  if (!confirm(T[_L].delete_tile)) return;
  state.dashboard.screens[state.currentScreen].tiles.splice(tileIdx, 1);
  state.selectedCanvasTile = -1;
  render();
}

function updateTileField(tileIdx, field, value) {
  const tile = state.dashboard.screens[state.currentScreen].tiles[tileIdx];
  tile[field] = value;
  // W trybie canvas zaktualizuj nazwę bez pełnego re-renderu
  if (state.viewMode === "canvas" && field === "label") {
    const el = document.querySelector(`.canvas-tile[data-tile-idx="${tileIdx}"] .ct-name`);
    if (el) el.textContent = value;
  }
}

function updateTileLayout(tileIdx, isHorizontal) {
  updateTileField(tileIdx, "layout", isHorizontal ? "horizontal" : "vertical");
  render();
}

function setTileType(tileIdx, type) {
  const tile = state.dashboard.screens[state.currentScreen].tiles[tileIdx];
  tile.type = type;
  if (type === "camera") {
    tile.rows = [];
    if (tile.camera_entity === undefined) tile.camera_entity = "";
    if (tile.refresh_s    === undefined) tile.refresh_s    = 10;
  }
  render();
}

// ── Picker kamery ─────────────────────────────────────────────────────────────

let _camPickCallback = null;

function openCameraModal(tileIdx) {
  _camPickCallback = (entityId) => {
    updateTileField(tileIdx, "camera_entity", entityId);
    render();
  };
  const modal = document.getElementById("camera-entity-modal");
  modal.classList.remove("hidden");
  filterCameraModal("");
}

function closeCameraModal() {
  document.getElementById("camera-entity-modal").classList.add("hidden");
  _camPickCallback = null;
}

function filterCameraModal(q) {
  const list = document.getElementById("cam-modal-list");
  const entities = state.cameraEntities || [];
  const filtered = q
    ? entities.filter(e => e.entity_id.includes(q) || e.friendly_name.toLowerCase().includes(q.toLowerCase()))
    : entities;
  list.innerHTML = filtered.length
    ? filtered.map(e => `
        <div class="entity-item" onclick="pickCameraEntity('${esc(e.entity_id)}')">
          <span class="entity-id">📷 ${esc(e.entity_id)}</span>
          <span class="entity-name">${esc(e.friendly_name)}</span>
        </div>`).join("")
    : `<div class="no-entities">${T[_L].no_results}</div>`;
}

function pickCameraEntity(entityId) {
  if (_camPickCallback) _camPickCallback(entityId);
  closeCameraModal();
}

function addRow(tileIdx) {
  const tile = state.dashboard.screens[state.currentScreen].tiles[tileIdx];
  if (tile.rows.length >= MAX_ROWS) {
    alert(T[_L].max_rows(MAX_ROWS)); return;
  }
  tile.rows.push({ entity_id:"", label:"", attribute:"", unit:"", sensor_type:"generic", domain:"" });
  render();
}

function deleteRow(tileIdx, rowIdx) {
  state.dashboard.screens[state.currentScreen].tiles[tileIdx].rows.splice(rowIdx, 1);
  render();
}

function updateRowField(tileIdx, rowIdx, field, value) {
  const row = state.dashboard.screens[state.currentScreen].tiles[tileIdx].rows[rowIdx];
  row[field] = value;
  if (field === "entity_id") {
    const entity = state.entities.find(e => e.entity_id === value);
    if (entity) {
      if (!row.label) row.label = entity.friendly_name;
      if (!row.unit)  row.unit  = entity.unit_of_measurement || "";
      row.domain      = entity.domain;
      row.sensor_type = autoSensorType(entity);
    }
    render();
  }
}

function setRowType(tileIdx, rowIdx, type) {
  const row = state.dashboard.screens[state.currentScreen].tiles[tileIdx].rows[rowIdx];
  if      (type === "switch")   row.sensor_type = "switch";
  else if (type === "presence") row.sensor_type = "presence";
  else if (type === "device")   row.sensor_type = "device";
  else {
    // Sensor — zachowaj sensor_type jeśli już był sensorem; inaczej ustaw generic
    if (["switch","presence","device"].includes(row.sensor_type))
      row.sensor_type = "generic";
    // else: zachowaj temperature/humidity/etc.
  }
  render();
}

function addEntityToTile(tileIdx, entityId) {
  const tile = state.dashboard.screens[state.currentScreen]?.tiles[tileIdx];
  if (!tile) return;
  if (tile.rows.length >= MAX_ROWS) {
    alert(T[_L].max_tile_rows(MAX_ROWS)); return;
  }
  const entity = state.entities.find(e => e.entity_id === entityId);
  tile.rows.push({
    entity_id:    entityId,
    label:        entity?.friendly_name || entityId,
    attribute:    "",
    unit:         entity?.unit_of_measurement || "",
    sensor_type:  entity ? autoSensorType(entity) : "generic",
    domain:       entity?.domain || "",
    display_mode: "text",
  });
  render();
}

// Entity ID jest prawidłowy jeśli wygląda jak "domena.nazwa" i pasuje do known entities
function isValidEntityId(id) {
  return typeof id === "string" && id.includes(".") &&
    state.entities.some(e => e.entity_id === id);
}

// ─── Auto-detection ────────────────────────────────────────────────────────────
const SWITCH_DOMAINS = new Set(["switch","light","fan","input_boolean","automation","cover","media_player"]);

function autoSensorType(entity) {
  if (entity.domain === "person" || entity.domain === "device_tracker") return "presence";
  if (SWITCH_DOMAINS.has(entity.domain)) return "switch";
  const dc = entity.device_class;
  if (dc === "temperature")              return "temperature";
  if (dc === "humidity")                 return "humidity";
  if (dc === "power" || dc === "energy") return "power";
  if (dc === "illuminance")              return "illuminance";
  return "generic";
}

// ─── Save / Send / Fetch ───────────────────────────────────────────────────────
async function saveDashboard() {
  const btn = document.getElementById("btn-save");
  btn.textContent = T[_L].saving; btn.disabled = true;
  try {
    const r = await fetch(api("/dashboard"), {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify(state.dashboard),
    });
    const d = await r.json();
    btn.textContent = d.ok ? T[_L].saved : T[_L].save_error;
    setTimeout(() => { btn.textContent = T[_L].btn_save || "Save"; btn.disabled = false; }, 1600);
  } catch { btn.textContent = T[_L].save_error; btn.disabled = false; }
}

async function sendToDevice() {
  await saveDashboard();
  const btn = document.getElementById("btn-send");
  btn.textContent = T[_L].sending; btn.disabled = true;
  try {
    const r = await fetch(api("/send"), {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify(state.dashboard),
    });
    const d = await r.json();
    btn.textContent = d.ok ? T[_L].sent_ok : T[_L].error_msg(d.error || d.status);
    btn.classList.add(d.ok ? "btn-success" : "btn-error");
  } catch { btn.textContent = T[_L].conn_error; btn.classList.add("btn-error"); }
  setTimeout(() => {
    btn.textContent = T[_L].btn_send || "Send to device"; btn.disabled = false;
    btn.classList.remove("btn-success", "btn-error");
  }, 2500);
}

async function fetchFromDevice() {
  const btn = document.getElementById("btn-fetch");
  btn.textContent = T[_L].downloading; btn.disabled = true;
  try {
    const r = await fetch(api("/fetch"));
    const d = await r.json();
    if (d.error) throw new Error(d.error);
    state.dashboard = d;
    if (!Array.isArray(state.dashboard.screens))  state.dashboard.screens = [];
    if (state.dashboard.default_screen == null)   state.dashboard.default_screen = 0;
    state.currentScreen = 0; state.selectedCanvasTile = -1;
    render();
    btn.textContent = T[_L].downloaded; btn.classList.add("btn-success");
    fetch(api("/dashboard"), {
      method: "POST", headers: {"Content-Type":"application/json"},
      body: JSON.stringify(state.dashboard),
    });
  } catch (e) { btn.textContent = T[_L].error_msg(e.message); btn.classList.add("btn-error"); }
  setTimeout(() => {
    btn.textContent = `⬇ ${T[_L].btn_fetch || "Fetch from device"}`; btn.disabled = false;
    btn.classList.remove("btn-success", "btn-error");
  }, 2500);
}

function exportJSON() {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(
    new Blob([JSON.stringify(state.dashboard, null, 2)], { type: "application/json" }));
  a.download = "ha_dashboard.json"; a.click();
}

async function importJSON(e) {
  const file = e.target.files[0]; if (!file) return;
  try {
    const data = JSON.parse(await file.text());
    if (!Array.isArray(data.screens)) throw new Error(T[_L].no_screens_field);
    state.dashboard = data; state.currentScreen = 0; render();
  } catch (err) { alert(T[_L].invalid_json(err.message)); }
  e.target.value = "";
}

// ─── Entity modal ──────────────────────────────────────────────────────────────
function openEntityModal(callback) {
  entityModalCallback = callback;
  document.getElementById("entity-modal").classList.remove("hidden");
  const inp = document.getElementById("modal-search");
  inp.value = ""; renderModalEntities(""); inp.focus();
}
function closeEntityModal() {
  document.getElementById("entity-modal").classList.add("hidden");
}
function renderModalEntities(search) {
  document.getElementById("modal-entity-list").innerHTML =
    state.entities
      .filter(e => !search || e.entity_id.includes(search) ||
                   e.friendly_name.toLowerCase().includes(search))
      .map(e => `
        <div class="entity-item" onclick="pickEntity('${esc(e.entity_id)}')">
          <span class="entity-domain">${esc(e.domain)}</span>
          <div class="entity-info">
            <span class="entity-name">${esc(e.friendly_name)}</span>
            <span class="entity-id">${esc(e.entity_id)}</span>
          </div>
          <span class="entity-state">${esc(e.state)}${e.unit_of_measurement ? " "+esc(e.unit_of_measurement) : ""}</span>
        </div>`)
      .join("");
}
function pickEntity(entityId) {
  closeEntityModal();
  if (entityModalCallback) entityModalCallback(entityId);
}

// ─── Render (dispatcher) ──────────────────────────────────────────────────────
function render() {
  renderScreenTabs();
  if (state.viewMode === "canvas") renderCanvas();
  else renderTiles();
}

function renderScreenTabs() {
  const defaultIdx = state.dashboard.default_screen ?? 0;
  document.getElementById("screen-tab-list").innerHTML =
    state.dashboard.screens.map((s, i) => `
      <div class="tab ${i === state.currentScreen ? "active" : ""}" onclick="switchScreen(${i})">
        ${i === defaultIdx ? '<span class="default-star">★</span>' : ""}
        <input class="tab-name-input" value="${esc(s.name)}"
               onclick="event.stopPropagation()"
               onchange="state.dashboard.screens[${i}].name = this.value">
        <button class="tab-del" onclick="event.stopPropagation(); deleteScreen(${i})">×</button>
      </div>`).join("");
}

// ══════════════════════════════════════════════════════════════════════════════
// ── WIDOK LISTY ───────────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

function renderTiles() {
  const container = document.getElementById("tiles-container");
  if (!state.dashboard.screens.length) {
    container.innerHTML = `<div class="empty-hint">${T[_L].empty_hint}</div>`;
    return;
  }
  const screen = state.dashboard.screens[state.currentScreen];
  container.innerHTML = (screen?.tiles || []).map((t, i) => renderTileCard(t, i)).join("");
}


function renderTileCard(tile, ti) {
  const isCamera = tile.type === "camera";
  const isHoriz  = tile.layout === "horizontal";
  const rowCount = (tile.rows || []).length;
  const rowsLeft = MAX_ROWS - rowCount;
  const camEntity = state.cameraEntities
    ? state.cameraEntities.find(e => e.entity_id === tile.camera_entity)
    : null;
  const camName = camEntity ? camEntity.friendly_name : (tile.camera_entity || T[_L].cam_no_entity);

  const cameraBlock = `
  <div class="camera-fields">
    <div class="field-group full-col">
      <label>${T[_L].cam_entity_label}</label>
      <div class="entity-picker">
        <span class="entity-picker-name">${esc(camName)}</span>
        <button class="btn btn-xs btn-secondary"
                onclick="openCameraModal(${ti})">${T[_L].cam_pick}</button>
      </div>
      ${tile.camera_entity?`<span class="entity-picker-id">${esc(tile.camera_entity)}</span>`:""}
    </div>
    <div class="field-group">
      <label>${T[_L].cam_refresh}</label>
      <input type="number" class="field-input" style="width:70px"
             min="0" max="3600" value="${tile.refresh_s ?? 10}"
             onchange="updateTileField(${ti},'refresh_s',+this.value)">
    </div>
  </div>`;

  const normalBlock = `
  <div class="rows-list">${(tile.rows||[]).map((r,ri)=>renderRow(r,ti,ri)).join("")}</div>
  ${rowCount===0
    ? `<div class="drop-hint">${T[_L].drop_hint}</div>`
    : rowsLeft>0
      ? `<div class="drop-hint-small">${T[_L].drop_hint_small(rowsLeft)}</div>`
      : `<div class="rows-full">${T[_L].rows_full(MAX_ROWS)}</div>`}
  <div style="padding:0 12px 10px">
    <button class="btn btn-dashed btn-sm" onclick="addRow(${ti})">${T[_L].add_row}</button>
  </div>`;

  return `
<div class="tile-card${isCamera?" tile-card-camera":""}" data-tile-idx="${ti}">
  <div class="tile-header">
    <input class="tile-name" value="${esc(tile.label)}" placeholder="${T[_L].tile_name_ph}"
           onchange="updateTileField(${ti},'label',this.value)">
    <div class="tile-controls">
      ${!isCamera ? `<label class="layout-toggle">
        <span class="${!isHoriz?"active-lbl":""}">${T[_L].layout_vertical}</span>
        <input type="checkbox" ${isHoriz?"checked":""} onchange="updateTileLayout(${ti},this.checked)">
        <span class="${isHoriz?"active-lbl":""}">${T[_L].layout_horizontal}</span>
      </label>` : ""}
      <button class="btn btn-xs btn-ghost" title="${T[_L].delete_tile_title}" onclick="deleteTile(${ti})">🗑</button>
    </div>
  </div>
  <div class="tile-type-row">
    <span class="tile-type-label">${T[_L].tile_type}:</span>
    <label><input type="radio" name="ttype_${ti}" ${!isCamera?"checked":""}
                  onchange="setTileType(${ti},'normal')"> ${T[_L].tile_type_normal}</label>
    <label><input type="radio" name="ttype_${ti}" ${isCamera?"checked":""}
                  onchange="setTileType(${ti},'camera')"> 📷 ${T[_L].tile_type_camera}</label>
  </div>
  <div class="tile-position">
    <label>X <input type="number" class="pos-input" value="${tile.x}"
                    onchange="updateTileField(${ti},'x',+this.value)"></label>
    <label>Y <input type="number" class="pos-input" value="${tile.y}"
                    onchange="updateTileField(${ti},'y',+this.value)"></label>
    <label>W <input type="number" class="pos-input" value="${tile.w}"
                    onchange="updateTileField(${ti},'w',+this.value)"></label>
    <label>H <input type="number" class="pos-input" value="${tile.h}"
                    onchange="updateTileField(${ti},'h',+this.value)"></label>
    <span class="pos-hint">(-1 = auto)</span>
  </div>
  ${isCamera ? cameraBlock : normalBlock}
</div>`;
}

function renderRow(row, ti, ri) {
  const isSwitch   = row.sensor_type === "switch";
  const isPresence = row.sensor_type === "presence";
  const isDevice   = row.sensor_type === "device";
  const isSensor   = !isSwitch && !isPresence && !isDevice;
  const isArc      = isSensor && row.display_mode === "arc";
  const entity   = state.entities.find(e => e.entity_id === row.entity_id);
  const dispName = entity ? entity.friendly_name : (row.entity_id || "—");
  const sensorOpts = sensorTypeOptions().map(([v,l]) =>
    `<option value="${v}" ${row.sensor_type===v?"selected":""}>${esc(l)}</option>`).join("");
  const badgeClass = isSwitch ? "badge-switch"
                   : isPresence ? "badge-presence"
                   : isDevice   ? "badge-device"
                   : "badge-sensor";
  const badgeText  = isSwitch ? T[_L].relay_badge
                   : isPresence ? T[_L].presence_badge
                   : isDevice   ? T[_L].device_badge
                   : T[_L].sensor_badge;
  const rowClass   = isSwitch ? "row-switch"
                   : isPresence ? "row-presence"
                   : isDevice   ? "row-device"
                   : "row-sensor";
  return `
<div class="row-item ${rowClass}">
  <div class="row-header">
    <span class="row-type-badge ${badgeClass}">${badgeText}</span>
    <button class="btn btn-xs btn-ghost row-del" onclick="deleteRow(${ti},${ri})">×</button>
  </div>
  <div class="row-fields">
    <div class="field-group full-col">
      <label>${T[_L].entity_label}</label>
      <div class="entity-picker">
        <span class="entity-picker-name">${esc(dispName)}</span>
        <button class="btn btn-xs btn-secondary"
                onclick="openEntityModal(id=>updateRowField(${ti},${ri},'entity_id',id))">${T[_L].choose_btn}</button>
      </div>
      ${row.entity_id?`<span class="entity-picker-id">${esc(row.entity_id)}</span>`:""}
    </div>
    <div class="field-group">
      <label>${T[_L].label_label}</label>
      <input type="text" class="field-input" value="${esc(row.label)}" placeholder="${T[_L].row_name_ph}"
             onchange="updateRowField(${ti},${ri},'label',this.value)">
    </div>
    <div class="field-group ${isDevice ? "full-col" : ""}">
      <label>${T[_L].type_label}</label>
      <div class="radio-group" style="flex-wrap:wrap;gap:8px 14px">
        <label><input type="radio" name="rtype_${ti}_${ri}" ${isSensor?"checked":""}
                      onchange="setRowType(${ti},${ri},'sensor')"> ${T[_L].sensor_opt}</label>
        <label><input type="radio" name="rtype_${ti}_${ri}" ${isSwitch?"checked":""}
                      onchange="setRowType(${ti},${ri},'switch')"> ${T[_L].relay_opt}</label>
        <label><input type="radio" name="rtype_${ti}_${ri}" ${isPresence?"checked":""}
                      onchange="setRowType(${ti},${ri},'presence')"> ${T[_L].presence_opt}</label>
        <label><input type="radio" name="rtype_${ti}_${ri}" ${isDevice?"checked":""}
                      onchange="setRowType(${ti},${ri},'device')"> ${T[_L].device_opt}</label>
      </div>
    </div>
    ${isDevice ? `
      <div class="device-preview full-col">
        <div class="dev-state dev-active"><span class="dev-dot dev-dot-active">●</span> ${T[_L].device_active}</div>
        <div class="dev-state dev-inactive"><span class="dev-dot dev-dot-inactive">●</span> ${T[_L].device_inactive}</div>
      </div>
    ` : isPresence ? `
      <div class="presence-preview full-col">
        <div class="pres-state pres-home">🟢 ${T[_L].presence_home}</div>
        <div class="pres-state pres-away">⚫ ${T[_L].presence_away}</div>
        <div class="pres-state pres-zone">🟠 ${T[_L].presence_zone}</div>
      </div>
    ` : isSensor ? `
      <div class="field-group">
        <label>${T[_L].sensor_type_label}</label>
        <select class="field-input" onchange="updateRowField(${ti},${ri},'sensor_type',this.value)">
          ${sensorOpts}</select>
      </div>
      <div class="field-group">
        <label>${T[_L].unit_label}</label>
        <input type="text" class="field-input" value="${esc(row.unit)}" placeholder="${T[_L].unit_ph}"
               onchange="updateRowField(${ti},${ri},'unit',this.value)">
      </div>
      <div class="field-group">
        <label>${T[_L].attr_label}</label>
        <input type="text" class="field-input" value="${esc(row.attribute)}" placeholder="${T[_L].attr_ph}"
               onchange="updateRowField(${ti},${ri},'attribute',this.value)">
      </div>
      <div class="field-group full-col">
        <label>${T[_L].display_mode_label}</label>
        <div class="radio-group">
          <label><input type="radio" name="dm_${ti}_${ri}" ${!isArc?"checked":""}
                        onchange="updateRowField(${ti},${ri},'display_mode','text')"> ${T[_L].display_mode_text}</label>
          <label><input type="radio" name="dm_${ti}_${ri}" ${isArc?"checked":""}
                        onchange="updateRowField(${ti},${ri},'display_mode','arc')"> ${T[_L].display_mode_arc}</label>
        </div>
      </div>
      ${isArc ? renderArcPreview(row) : ""}
    ` : `
      <div class="switch-preview full-col">
        <div class="toggle-preview off">${T[_L].sw_off}</div>
        <div class="toggle-preview on">${T[_L].sw_on}</div>
      </div>
    `}
  </div>
</div>`;
}

// ══════════════════════════════════════════════════════════════════════════════
// ── WIDOK CANVAS ──────────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

function getCanvasScale() {
  const scroll = document.getElementById("canvas-scroll");
  if (!scroll || !scroll.clientWidth) return 0.7;
  return Math.max(0.2, (scroll.clientWidth - 32) / ESP_W);
}

function getEffectiveTileGeom(tile, idx) {
  let { x, y, w, h } = tile;
  if (x < 0) x = (idx % 2) * (488 + 16) + 8;
  if (y < 0) y = Math.floor(idx / 2) * (200 + 16) + 8;
  if (!w || w < MIN_W) w = 488;
  if (!h || h < MIN_H) h = 200;
  return { x, y, w, h };
}

const SENSOR_ICONS = {
  temperature:"🌡", humidity:"💧", power:"⚡",
  illuminance:"💡", cpu:"🖥", memory:"💾",
};

// Kolor wartości sensora — identyczny jak w firmware panel_ui.cpp
function getSensorColor(type, value) {
  if (type === "temperature") {
    const v = parseFloat(value);
    if (isNaN(v)) return "#607080";
    if (v > 28)   return "#e06050";
    if (v > 24)   return "#e0a050";
    if (v > 18)   return "#50c8a0";
    return "#6090e0";
  }
  if (type === "humidity")    return "#6090e0";
  if (type === "power")       return "#e0c050";
  if (type === "illuminance") return "#e0e080";
  if (type === "cpu" || type === "memory") {
    const v = parseFloat(value);
    if (isNaN(v)) return "#607080";
    if (v > 80)   return "#e06050";
    if (v > 50)   return "#e0a050";
    return "#50c880";
  }
  return "#9ab8c8";
}

// Zakres wskaznika kolowego dla danego typu sensora (identyczny jak w firmware)
function arcRangeForSensor(type) {
  switch (type) {
    case "temperature": return [-20, 50];
    case "humidity":    return [0, 100];
    case "cpu":         return [0, 100];
    case "memory":      return [0, 100];
    case "power":       return [0, 3500];
    case "illuminance": return [0, 1000];
    default:            return [0, 100];
  }
}

// Renderuje podgląd SVG wskaznika kolowego (270° jak speedometr)
function renderArcPreview(row) {
  const stateVal = getEntityStateVal(row.entity_id);
  const [mn, mx] = arcRangeForSensor(row.sensor_type);
  const numVal   = parseFloat(stateVal);
  const hasVal   = !isNaN(numVal) && stateVal !== null && stateVal !== "unavailable";
  const pct      = hasVal ? Math.max(0, Math.min(1, (numVal - mn) / (mx - mn))) : 0;
  const color    = hasVal ? getSensorColor(row.sensor_type, stateVal) : "#607080";

  const cx = 50, cy = 54, r = 36;
  const deg2rad = Math.PI / 180;

  // Sciezka luku SVG od kąta startDeg przez sweepDeg stopni zgodnie z ruchem wskazówek
  function arcPath(startDeg, sweepDeg) {
    if (sweepDeg <= 0) return "";
    if (sweepDeg >= 360) sweepDeg = 359.9;
    const s = startDeg * deg2rad;
    const e = (startDeg + sweepDeg) * deg2rad;
    const sx = cx + r * Math.cos(s), sy = cy + r * Math.sin(s);
    const ex = cx + r * Math.cos(e), ey = cy + r * Math.sin(e);
    const lg = sweepDeg > 180 ? 1 : 0;
    return `M ${sx.toFixed(2)} ${sy.toFixed(2)} A ${r} ${r} 0 ${lg} 1 ${ex.toFixed(2)} ${ey.toFixed(2)}`;
  }

  const bgPath  = arcPath(135, 270);
  const valPath = pct > 0.005 ? arcPath(135, pct * 270) : "";
  const valStr  = hasVal
    ? `${numVal % 1 === 0 ? numVal : numVal.toFixed(1)}${row.unit ? row.unit : ""}`
    : `--${row.unit ? row.unit : ""}`;
  const rangeStr = `${mn}…${mx}${row.unit ? row.unit : ""}`;

  return `<div class="arc-preview full-col">
    <svg viewBox="0 0 100 108" width="100" height="108" aria-label="${esc(row.label)} arc gauge">
      <path d="${bgPath}"  fill="none" stroke="#2A3A4A" stroke-width="7" stroke-linecap="round"/>
      ${valPath ? `<path d="${valPath}" fill="none" stroke="${color}" stroke-width="7" stroke-linecap="round"/>` : ""}
      <text x="50" y="52" text-anchor="middle" dominant-baseline="middle"
            font-size="14" font-weight="600" fill="${color}" font-family="system-ui,sans-serif">${esc(valStr)}</text>
      <text x="50" y="68" text-anchor="middle" dominant-baseline="middle"
            font-size="9" fill="#607080" font-family="system-ui,sans-serif">${esc(row.label || "")}</text>
      <text x="50" y="100" text-anchor="middle" dominant-baseline="middle"
            font-size="8" fill="#3a4a5a" font-family="system-ui,sans-serif">${esc(rangeStr)}</text>
    </svg>
  </div>`;
}

// Pobierz stan encji z listy (lub null)
function getEntityStateVal(entityId) {
  if (!entityId) return null;
  return state.entities.find(e => e.entity_id === entityId)?.state ?? null;
}

const SWITCH_ON_STATES = new Set(["on","true","1","home","playing","open","unlocked","active"]);

// Renderuje pojedynczy wiersz kafelka z żywą wartością
function renderCtRow(row, isHorizontal) {
  const stateVal   = getEntityStateVal(row.entity_id);
  const isSwitch   = row.sensor_type === "switch";
  const isPresence = row.sensor_type === "presence";
  const isDevice   = row.sensor_type === "device";
  const isArc      = !isSwitch && !isPresence && !isDevice && row.display_mode === "arc";
  const label      = row.label || row.entity_id || "—";

  // ── Device: tylko kolorowa kropka (bez tekstu statusu) ─────────────────────
  if (isDevice) {
    const unavail = stateVal === null || stateVal === "unavailable" || stateVal === "unknown";
    const active  = !unavail && (stateVal === "home" || stateVal === "on"
                                 || stateVal === "true" || stateVal === "active");
    const dotCol  = unavail ? "#4A5568" : active ? "#22c55e" : "#4A5568";
    if (isHorizontal) {
      return `<div class="ct-row ct-sensor ct-h-block" style="justify-content:center">
        <span style="color:${dotCol};font-size:14px;line-height:1">⬤</span>
      </div>`;
    }
    return `<div class="ct-row ct-sensor">
      <span class="ct-label">${esc(label)}</span>
      <span style="color:${dotCol};font-size:14px;line-height:1;margin-left:auto;flex-shrink:0">⬤</span>
    </div>`;
  }

  // ── Arc sensor: mały wskaznik SVG w canvas ──────────────────────────────────
  if (isArc) {
    const unavail = stateVal === null || stateVal === "unavailable" || stateVal === "unknown";
    const numVal  = parseFloat(stateVal);
    const hasVal  = !isNaN(numVal) && !unavail;
    const [mn, mx] = arcRangeForSensor(row.sensor_type);
    const pct   = hasVal ? Math.max(0, Math.min(1, (numVal - mn) / (mx - mn))) : 0;
    const color = hasVal ? getSensorColor(row.sensor_type, stateVal) : "#607080";
    const valStr = unavail ? "--" : `${numVal % 1 === 0 ? numVal : numVal.toFixed(1)}${row.unit ? row.unit : ""}`;

    const cx = 20, cy = 22, r = 14, deg2rad = Math.PI / 180;
    function ap(sd, sw) {
      if (sw <= 0) return "";
      if (sw >= 360) sw = 359.9;
      const s = sd * deg2rad, e = (sd + sw) * deg2rad;
      const sx = cx + r * Math.cos(s), sy = cy + r * Math.sin(s);
      const ex = cx + r * Math.cos(e), ey = cy + r * Math.sin(e);
      return `M ${sx.toFixed(1)} ${sy.toFixed(1)} A ${r} ${r} 0 ${sw>180?1:0} 1 ${ex.toFixed(1)} ${ey.toFixed(1)}`;
    }
    const bgP  = ap(135, 270);
    const valP = pct > 0.01 ? ap(135, pct * 270) : "";

    if (isHorizontal) {
      return `<div class="ct-row ct-sensor ct-h-block" style="align-items:center;gap:2px">
        <svg viewBox="0 0 40 44" width="32" height="36" style="flex-shrink:0">
          <path d="${bgP}" fill="none" stroke="#2A3A4A" stroke-width="3" stroke-linecap="round"/>
          ${valP?`<path d="${valP}" fill="none" stroke="${color}" stroke-width="3" stroke-linecap="round"/>`:""}
          <text x="20" y="22" text-anchor="middle" dominant-baseline="middle" font-size="7" fill="${color}" font-family="system-ui">${esc(valStr)}</text>
        </svg>
      </div>`;
    }
    return `<div class="ct-row ct-sensor" style="align-items:center">
      <svg viewBox="0 0 40 44" width="34" height="38" style="flex-shrink:0">
        <path d="${bgP}" fill="none" stroke="#2A3A4A" stroke-width="3" stroke-linecap="round"/>
        ${valP?`<path d="${valP}" fill="none" stroke="${color}" stroke-width="3" stroke-linecap="round"/>`:""}
        <text x="20" y="22" text-anchor="middle" dominant-baseline="middle" font-size="7" fill="${color}" font-family="system-ui">${esc(valStr)}</text>
      </svg>
      <span class="ct-label">${esc(label)}</span>
    </div>`;
  }

  if (isPresence) {
    const unavail = stateVal === null || stateVal === "unavailable" || stateVal === "unknown";
    const isHome  = !unavail && stateVal === "home";
    const isAway  = !unavail && stateVal === "not_home";
    const color   = unavail ? "#607080" : isHome ? "#22c55e" : isAway ? "#9baab3" : "#f6b84a";
    const text    = unavail ? "—"
                  : isHome  ? T[_L].presence_home
                  : isAway  ? T[_L].presence_away
                  : (stateVal || "—");
    if (isHorizontal) {
      return `<div class="ct-row ct-sensor ct-h-block">
        <span class="ct-icon" style="color:${color}">●</span>
        <span class="ct-val" style="color:${color}">${esc(text)}</span>
      </div>`;
    }
    return `<div class="ct-row ct-sensor">
      <span class="ct-icon" style="color:${color}">●</span>
      <span class="ct-label">${esc(label)}</span>
      <span class="ct-val" style="color:${color}">${esc(text)}</span>
    </div>`;
  }

  if (isSwitch) {
    const unavail = stateVal === null || stateVal === "unavailable" || stateVal === "unknown";
    const isOn    = !unavail && SWITCH_ON_STATES.has(stateVal.toLowerCase());
    const bg      = unavail ? "#1a2a38" : isOn ? "#0C4838" : "#1E2A35";
    const fg      = unavail ? "#405060"  : isOn ? "#2ddf99"  : "#e06060";
    const dot     = unavail ? "#405060"  : isOn ? "#22c55e"  : "#ef4444";
    const stText  = unavail ? "—" : isOn ? T[_L].sw_on : T[_L].sw_off;
    return `<div class="ct-row ct-sw-btn" style="background:${bg}">
      <span class="ct-dot" style="color:${dot}">⬤</span>
      <span class="ct-sw-lbl" style="color:${fg}">${esc(label)}</span>
      <span class="ct-sw-state" style="color:${fg}">${stText}</span>
    </div>`;
  }

  // Sensor
  const icon  = SENSOR_ICONS[row.sensor_type] || "•";
  const color = getSensorColor(row.sensor_type, stateVal);
  const unavail = stateVal === null || stateVal === "unavailable" || stateVal === "unknown";
  const valStr  = unavail
    ? `—${row.unit ? " "+row.unit : ""}`
    : `${stateVal}${row.unit ? " "+row.unit : ""}`;

  if (isHorizontal) {
    // Kompaktowy blok: ikona + wartość (bez osobnego labela — jak na panelu)
    return `<div class="ct-row ct-sensor ct-h-block">
      <span class="ct-icon" style="color:${color}">${icon}</span>
      <span class="ct-val" style="color:${color}">${esc(valStr)}</span>
    </div>`;
  }
  return `<div class="ct-row ct-sensor">
    <span class="ct-icon" style="color:${color}">${icon}</span>
    <span class="ct-label">${esc(label)}</span>
    <span class="ct-val" style="color:${color}">${esc(valStr)}</span>
  </div>`;
}

function renderCanvas() {
  const sc = getCanvasScale();
  const espCanvas = document.getElementById("esp-canvas");
  if (!espCanvas) return;

  // Wymiary + tło z siatką
  const ss = Math.round(SNAP * sc);
  const ls = Math.round(64  * sc);
  espCanvas.style.cssText = `
    width:${ESP_W*sc}px; height:${ESP_H*sc}px;
    background-image:
      linear-gradient(rgba(26,54,72,.55) 1px,transparent 1px),
      linear-gradient(90deg,rgba(26,54,72,.55) 1px,transparent 1px),
      linear-gradient(rgba(18,38,52,.35) 1px,transparent 1px),
      linear-gradient(90deg,rgba(18,38,52,.35) 1px,transparent 1px);
    background-size:${ls}px ${ls}px,${ls}px ${ls}px,${ss}px ${ss}px,${ss}px ${ss}px;`;

  const screen = state.dashboard.screens[state.currentScreen];
  if (!screen) { espCanvas.innerHTML = ""; return; }

  espCanvas.innerHTML = screen.tiles
    .map((tile, idx) => renderCanvasTileHtml(tile, idx, sc)).join("");

  // Przywróć zaznaczenie
  if (state.selectedCanvasTile >= 0) {
    if (state.selectedCanvasTile >= screen.tiles.length) {
      state.selectedCanvasTile = -1;
    } else {
      espCanvas.querySelector(`[data-tile-idx="${state.selectedCanvasTile}"]`)
        ?.classList.add("selected");
    }
  }
  refreshCanvasTileProps();
}

function renderCanvasTileHtml(tile, idx, sc) {
  const { x, y, w, h } = getEffectiveTileGeom(tile, idx);

  if (tile.type === "camera") {
    return `
<div class="canvas-tile canvas-tile-camera" data-tile-idx="${idx}" draggable="false"
     style="left:${x*sc}px;top:${y*sc}px;width:${w*sc}px;height:${h*sc}px">
  <div class="ct-header">
    <span class="ct-name">📷 ${esc(tile.label)}</span>
  </div>
  <div class="ct-body" style="justify-content:center;align-items:center;display:flex;flex:1">
    <span style="font-size:28px;opacity:.5">📷</span>
  </div>
  <div class="resize-handle"></div>
</div>`;
  }

  const isH = tile.rows && tile.layout === "horizontal";
  const rowsHtml = (tile.rows || []).map(row => renderCtRow(row, isH)).join("");

  return `
<div class="canvas-tile" data-tile-idx="${idx}" draggable="false"
     style="left:${x*sc}px;top:${y*sc}px;width:${w*sc}px;height:${h*sc}px">
  <div class="ct-header">
    <span class="ct-name">${esc(tile.label)}</span>
    <span class="ct-layout-icon">${isH?"↔":"↕"}</span>
  </div>
  <div class="ct-body ${isH?"ct-horizontal":""}">
    ${rowsHtml || `<div class="ct-empty">${T[_L].canvas_drop}</div>`}
  </div>
  <div class="resize-handle"></div>
</div>`;
}

function updateCanvasTilePos(tileIdx) {
  const sc = getCanvasScale();
  const tile = state.dashboard.screens[state.currentScreen]?.tiles[tileIdx];
  if (!tile) return;
  const el = document.querySelector(`.canvas-tile[data-tile-idx="${tileIdx}"]`);
  if (!el) return;
  const { x, y, w, h } = getEffectiveTileGeom(tile, tileIdx);
  el.style.left   = `${x * sc}px`;
  el.style.top    = `${y * sc}px`;
  el.style.width  = `${w * sc}px`;
  el.style.height = `${h * sc}px`;
}

function selectCanvasTile(idx) {
  state.selectedCanvasTile = idx;
  document.querySelectorAll(".canvas-tile.selected").forEach(el => el.classList.remove("selected"));
  document.querySelector(`.canvas-tile[data-tile-idx="${idx}"]`)?.classList.add("selected");
  refreshCanvasTileProps();
}

function refreshCanvasTileProps() {
  const panel = document.getElementById("canvas-tile-props");
  if (!panel) return;
  const idx    = state.selectedCanvasTile;
  const screen = state.dashboard.screens[state.currentScreen];
  if (idx < 0 || !screen || idx >= screen.tiles.length) {
    panel.classList.add("hidden"); return;
  }
  panel.classList.remove("hidden");
  panel.innerHTML = `
    <div class="canvas-props-header">
      <span>${T[_L].canvas_props(idx+1, screen.tiles.length)}</span>
      <button class="btn btn-xs btn-ghost" onclick="closeCanvasTileProps()">${T[_L].close_btn}</button>
    </div>
    ${renderTileCard(screen.tiles[idx], idx)}`;
}

function closeCanvasTileProps() {
  state.selectedCanvasTile = -1;
  document.getElementById("canvas-tile-props")?.classList.add("hidden");
  document.querySelectorAll(".canvas-tile.selected").forEach(el => el.classList.remove("selected"));
}

// ── Canvas: obsługa myszy (drag/resize kafelków) ───────────────────────────────
function onCanvasMouseDown(e) {
  const tile = e.target.closest(".canvas-tile");
  if (!tile) { closeCanvasTileProps(); return; }

  const tileIdx  = parseInt(tile.dataset.tileIdx, 10);
  const rect     = tile.getBoundingClientRect();
  const isResize = e.clientX >= rect.right - HANDLE_PX && e.clientY >= rect.bottom - HANDLE_PX;
  const tileData = state.dashboard.screens[state.currentScreen].tiles[tileIdx];
  const geom     = getEffectiveTileGeom(tileData, tileIdx);

  // Ustaw faktyczne x/y/w/h jeśli były -1 (auto)
  tileData.x = geom.x; tileData.y = geom.y;
  tileData.w = geom.w; tileData.h = geom.h;

  dragState = {
    tileIdx, mode: isResize ? "resize" : "move",
    startMX: e.clientX, startMY: e.clientY,
    origX: geom.x, origY: geom.y, origW: geom.w, origH: geom.h,
  };

  selectCanvasTile(tileIdx);
  e.preventDefault();
}

function onCanvasMouseMove(e) {
  if (!dragState) {
    // Zmień kursor gdy nad uchwytem
    if (state.viewMode === "canvas" && e.target.closest(".resize-handle"))
      document.getElementById("esp-canvas").style.cursor = "nwse-resize";
    else if (state.viewMode === "canvas" && e.target.closest(".canvas-tile"))
      document.getElementById("esp-canvas").style.cursor = "move";
    else if (state.viewMode === "canvas")
      document.getElementById("esp-canvas").style.cursor = "default";
    return;
  }

  const sc = getCanvasScale();
  const dx = (e.clientX - dragState.startMX) / sc;
  const dy = (e.clientY - dragState.startMY) / sc;
  const tile = state.dashboard.screens[state.currentScreen].tiles[dragState.tileIdx];

  if (dragState.mode === "move") {
    const newX = Math.round((dragState.origX + dx) / SNAP) * SNAP;
    const newY = Math.round((dragState.origY + dy) / SNAP) * SNAP;
    tile.x = Math.max(0, Math.min(newX, ESP_W - tile.w));
    tile.y = Math.max(0, Math.min(newY, ESP_H - tile.h));
  } else {
    const newW = Math.round((dragState.origW + dx) / SNAP) * SNAP;
    const newH = Math.round((dragState.origH + dy) / SNAP) * SNAP;
    tile.w = Math.max(MIN_W, Math.min(newW, ESP_W - tile.x));
    tile.h = Math.max(MIN_H, Math.min(newH, ESP_H - tile.y));
  }

  updateCanvasTilePos(dragState.tileIdx);
  syncPropsPosInputs(dragState.tileIdx, tile);
}

function onCanvasMouseUp() {
  dragState = null;
}

function syncPropsPosInputs(tileIdx, tile) {
  const panel = document.getElementById("canvas-tile-props");
  if (!panel || panel.classList.contains("hidden")) return;
  const inputs = panel.querySelectorAll(".pos-input");
  if (inputs.length >= 4) {
    inputs[0].value = tile.x; inputs[1].value = tile.y;
    inputs[2].value = tile.w; inputs[3].value = tile.h;
  }
}

// ── Entity panel ──────────────────────────────────────────────────────────────
function renderEntityPanel() {
  const list   = document.getElementById("entity-list");
  const search = state.entitySearch;
  const filtered = state.entities.filter(e =>
    !search || e.entity_id.includes(search) ||
    e.friendly_name.toLowerCase().includes(search));

  const countEl = document.getElementById("entity-count");
  if (countEl) countEl.textContent = search
    ? `${filtered.length}/${state.entities.length}` : `${state.entities.length}`;

  if (!filtered.length) { list.innerHTML = `<div class="loading">${T[_L].no_results}</div>`; return; }

  list.innerHTML = filtered.map(e => `
    <div class="entity-item" draggable="true" data-eid="${esc(e.entity_id)}">
      <span class="entity-domain">${esc(e.domain)}</span>
      <div class="entity-info">
        <span class="entity-name">${esc(e.friendly_name)}</span>
        <span class="entity-id">${esc(e.entity_id)}</span>
      </div>
      <span class="entity-state">${esc(e.state)}${e.unit_of_measurement?" "+esc(e.unit_of_measurement):""}</span>
    </div>`).join("");
}

// ─── Boot ─────────────────────────────────────────────────────────────────────
init();

// ══════════════════════════════════════════════════════════════════════════════
// ZAKŁADKI GŁÓWNE (Dashboard / Powiadomienia głosowe)
// ══════════════════════════════════════════════════════════════════════════════
function setMainTab(name) {
  ["dashboard", "tts"].forEach(t => {
    document.getElementById("tab-" + t).classList.toggle("hidden", t !== name);
    document.getElementById("tabn-" + t).classList.toggle("active",  t === name);
  });
  if (name === "tts" && !_ttsLoaded) {
    _ttsLoaded = true;
    loadTtsTab();
  }
}

// ══════════════════════════════════════════════════════════════════════════════
// POWIADOMIENIA GŁOSOWE — TTS RULES
// ══════════════════════════════════════════════════════════════════════════════
let _ttsRules    = [];    // [{id, enabled, entity_id, from_state, to_state, text}]
let _ttsEntities = [];    // [{entity_id, friendly_name, state, domain}]
let _ttsLoaded   = false;
let _ttsPickerCb = null;  // callback po wyborze encji w modalu

// ── Ładowanie ───────────────────────────────────────────────────────────────
async function loadTtsTab() {
  await Promise.all([loadTtsEntities(), loadTtsRules()]);
}

async function loadTtsEntities() {
  try {
    const r = await fetch(api("/tts_entities"));
    _ttsEntities = await r.json();
  } catch(e) { _ttsEntities = []; }
}

async function loadTtsRules() {
  try {
    const r = await fetch(api("/tts_rules"));
    const d = await r.json();
    _ttsRules = Array.isArray(d.rules) ? d.rules : [];
  } catch(e) { _ttsRules = []; }
  renderTtsRules();
}

// ── Renderowanie tabeli ─────────────────────────────────────────────────────
function renderTtsRules() {
  const tbody = document.getElementById("tts-tbody");
  if (!tbody) return;

  if (_ttsRules.length === 0) {
    tbody.innerHTML =
      `<tr id="tts-empty-row"><td colspan="6" style="text-align:center;` +
      `color:#64748b;padding:24px">${T[_L].tts_no_rules}</td></tr>`;
    return;
  }

  tbody.innerHTML = _ttsRules.map((rule, i) => {
    const fname = _ttsEntityLabel(rule.entity_id);
    const on = rule.enabled !== false;
    return `<tr>
      <td style="text-align:center">
        <input type="checkbox" ${on ? "checked" : ""}
               onchange="_ttsRules[${i}].enabled=this.checked"
               style="width:16px;height:16px;cursor:pointer">
      </td>
      <td>
        <button class="tts-entity-btn" onclick="openTtsEntityPicker(${i})"
                title="${esc(rule.entity_id)}">
          ${esc(fname)}
        </button>
      </td>
      <td>
        <input class="tts-state-input" value="${esc(rule.from_state||'*')}"
               placeholder="*"
               onchange="_ttsRules[${i}].from_state=this.value.trim()||'*'"
               title="${T[_L].tts_state_hint}">
      </td>
      <td>
        <input class="tts-state-input" value="${esc(rule.to_state||'*')}"
               placeholder="*"
               onchange="_ttsRules[${i}].to_state=this.value.trim()||'*'"
               title="${T[_L].tts_state_hint}">
      </td>
      <td>
        <input class="tts-text-input" value="${esc(rule.text||'')}"
               placeholder="${T[_L].tts_text_ph}"
               onchange="_ttsRules[${i}].text=this.value">
      </td>
      <td style="white-space:nowrap">
        <button class="btn btn-xs btn-secondary" onclick="testTtsRule(${i})"
                title="${T[_L].tts_play_title||'Play now'}">▶</button>
        <button class="btn btn-xs btn-error" onclick="deleteTtsRule(${i})"
                title="${T[_L].tts_delete_title||'Delete rule'}" style="margin-left:4px">✕</button>
      </td>
    </tr>`;
  }).join("");
}

function _ttsEntityLabel(eid) {
  if (!eid) return T[_L].tts_entity_ph;
  const e = _ttsEntities.find(x => x.entity_id === eid);
  return e ? `${e.friendly_name || eid} (${e.state})` : eid;
}

// ── CRUD reguł ──────────────────────────────────────────────────────────────
function addTtsRule() {
  _ttsRules.push({
    id:         crypto.randomUUID ? crypto.randomUUID() : Date.now().toString(36),
    enabled:    true,
    entity_id:  "",
    from_state: "*",
    to_state:   "*",
    text:       "",
  });
  renderTtsRules();
}

function deleteTtsRule(i) {
  _ttsRules.splice(i, 1);
  renderTtsRules();
}

async function saveTtsRules() {
  const btn    = document.querySelector("#tab-tts .btn-primary");
  const status = document.getElementById("tts-save-status");
  if (btn) btn.disabled = true;
  if (status) status.textContent = "";
  try {
    const r = await fetch(api("/tts_rules"), {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({rules: _ttsRules}),
    });
    const d = await r.json();
    if (status) { status.textContent = d.ok ? T[_L].tts_saved : T[_L].tts_save_error; }
  } catch(e) {
    if (status) status.textContent = T[_L].tts_net_error;
  }
  if (btn) btn.disabled = false;
  setTimeout(() => { if (status) status.textContent = ""; }, 3000);
}

async function testTtsRule(i) {
  const rule = _ttsRules[i];
  if (!rule?.text?.trim()) { alert(T[_L].tts_test_alert); return; }
  try {
    const r = await fetch(api("/tts_test"), {
      method:  "POST",
      headers: {"Content-Type": "application/json"},
      body:    JSON.stringify({text: rule.text}),
    });
    const d = await r.json();
    if (!d.ok) alert(T[_L].tts_error_prefix(d.error || d.status));
  } catch(e) { alert(T[_L].tts_net_error_prefix(e.message)); }
}

// ── Picker encji dla TTS ────────────────────────────────────────────────────
function openTtsEntityPicker(ruleIdx) {
  _ttsPickerCb = (eid) => {
    _ttsRules[ruleIdx].entity_id = eid;
    renderTtsRules();
  };
  document.getElementById("tts-modal-search").value = "";
  filterTtsEntityModal("");
  document.getElementById("tts-entity-modal").classList.remove("hidden");
  setTimeout(() => document.getElementById("tts-modal-search").focus(), 50);
}

function closeTtsEntityModal() {
  document.getElementById("tts-entity-modal").classList.add("hidden");
  _ttsPickerCb = null;
}

function filterTtsEntityModal(query) {
  const q = query.toLowerCase();
  const list = document.getElementById("tts-modal-list");
  const hits = _ttsEntities.filter(e =>
    e.entity_id.toLowerCase().includes(q) ||
    (e.friendly_name || "").toLowerCase().includes(q)
  );

  if (hits.length === 0) {
    list.innerHTML = `<div class="loading" style="color:#64748b">${T[_L].no_results}</div>`;
    return;
  }

  list.innerHTML = hits.map(e => `
    <div class="entity-item" onclick="_ttsPickEntity('${esc(e.entity_id)}')">
      <div class="entity-id">${esc(e.entity_id)}</div>
      <div class="entity-meta">
        <span class="entity-name">${esc(e.friendly_name || e.entity_id)}</span>
        <span class="entity-state">${esc(e.state)}</span>
      </div>
    </div>`).join("");
}

function _ttsPickEntity(eid) {
  if (_ttsPickerCb) _ttsPickerCb(eid);
  closeTtsEntityModal();
}
