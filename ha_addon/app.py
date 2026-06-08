import os
import json
import time
import uuid
import threading
import requests
from flask import Flask, request, jsonify, render_template

app = Flask(__name__, static_folder="static", template_folder="templates")

SUPERVISOR_TOKEN = os.environ.get("SUPERVISOR_TOKEN", "")
HA_API     = "http://supervisor/core/api"
HA_WS_URL  = "ws://supervisor/core/api/websocket"
DATA_FILE  = "/data/dashboard.json"
TTS_FILE   = "/data/tts_rules.json"
OPTIONS_FILE = "/data/options.json"

DASHBOARD_DOMAINS = {
    "sensor", "binary_sensor", "switch", "light",
    "fan", "input_boolean", "climate",
    "cover", "media_player", "number", "input_number",
    "select", "input_select",
    # Obecność / śledzenie urządzeń
    "device_tracker", "person",
}

# Encje dostępne w pikierze TTS (szerszy zakres)
TTS_DOMAINS = DASHBOARD_DOMAINS | {
    "device_tracker", "person", "zone",
    "alarm_control_panel", "lock", "vacuum",
    "button", "event",
}


# ─── Helpers ────────────────────────────────────────────────────────────────

def ha_headers():
    return {"Authorization": f"Bearer {SUPERVISOR_TOKEN}"}


def load_options():
    try:
        with open(OPTIONS_FILE) as f:
            return json.load(f)
    except Exception:
        return {"device_ip": "", "device_port": 80}


def load_dashboard():
    try:
        with open(DATA_FILE) as f:
            return json.load(f)
    except Exception:
        return {"default_screen": 0, "screens": []}


def save_dashboard(data):
    with open(DATA_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


def load_tts_rules():
    try:
        with open(TTS_FILE) as f:
            return json.load(f)
    except Exception:
        return {"rules": []}


def save_tts_rules(data):
    with open(TTS_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


# ─── TTS Watcher ────────────────────────────────────────────────────────────

class TtsWatcher:
    """
    Nasłuchuje zdarzeń state_changed z HA przez WebSocket Supervisor.
    Gdy encja zmieni stan i pasuje do reguły — wysyła tekst do panelu ESP32.
    Automatycznie reconnektuje się po utracie połączenia.
    """

    def __init__(self):
        self._rules = []
        self._lock  = threading.Lock()
        self._load()

    def _load(self):
        try:
            self._rules = load_tts_rules().get("rules", [])
        except Exception:
            self._rules = []

    def reload(self):
        """Przeładuj reguły z dysku (wywołać po zapisie przez API)."""
        with self._lock:
            self._load()

    # ── dopasowanie reguły ────────────────────────────────────────────────
    @staticmethod
    def _state_match(pattern, state):
        """'*' lub '' pasuje do wszystkiego; inaczej porównanie dosłowne."""
        return pattern in ("*", "", None) or pattern == state

    def _check_rules(self, entity_id, old_state, new_state):
        with self._lock:
            rules = list(self._rules)
        for rule in rules:
            if not rule.get("enabled", True):
                continue
            if rule.get("entity_id") != entity_id:
                continue
            if not self._state_match(rule.get("from_state", "*"), old_state):
                continue
            if not self._state_match(rule.get("to_state",   "*"), new_state):
                continue
            text = rule.get("text", "").strip()
            if text:
                threading.Thread(target=self._speak, args=(text,),
                                 daemon=True).start()

    # ── wysłanie tekstu do panelu ─────────────────────────────────────────
    def _speak(self, text):
        opts  = load_options()
        ip    = opts.get("device_ip", "")
        port  = opts.get("device_port", 80)
        if not ip:
            print("[TTS] Brak device_ip — pomijam")
            return
        try:
            r = requests.post(
                f"http://{ip}:{port}/api/speak",
                json={"text": text},
                timeout=6,
            )
            print(f"[TTS] speak → {ip}:{port}  status={r.status_code}  '{text[:50]}'")
        except Exception as e:
            print(f"[TTS] speak error: {e}")

    # ── WebSocket listener ────────────────────────────────────────────────
    def start(self):
        """Uruchom wątek WebSocket w tle."""
        def _run():
            try:
                import websocket as _ws_lib
            except ImportError:
                print("[TTS] WARN: websocket-client nie zainstalowany — "
                      "reguły TTS nieaktywne. Dodaj do requirements.txt.")
                return

            msg_id = [1]

            def on_message(ws, raw):
                try:
                    d = json.loads(raw)
                    t = d.get("type")
                    if t == "auth_required":
                        ws.send(json.dumps({
                            "type": "auth",
                            "access_token": SUPERVISOR_TOKEN,
                        }))
                    elif t == "auth_ok":
                        ws.send(json.dumps({
                            "id":         msg_id[0],
                            "type":       "subscribe_events",
                            "event_type": "state_changed",
                        }))
                        msg_id[0] += 1
                        print("[TTS] HA WebSocket — połączono i zasubskrybowano state_changed")
                    elif t == "event":
                        ev = d.get("event", {})
                        if ev.get("event_type") == "state_changed":
                            data = ev.get("data", {})
                            eid  = data.get("entity_id", "")
                            old  = (data.get("old_state") or {}).get("state", "")
                            new  = (data.get("new_state") or {}).get("state", "")
                            self._check_rules(eid, old, new)
                except Exception as exc:
                    print(f"[TTS] on_message error: {exc}")

            while True:
                try:
                    ws = _ws_lib.WebSocketApp(
                        HA_WS_URL,
                        on_message=on_message,
                        on_error=lambda ws, e: print(f"[TTS] WS błąd: {e}"),
                        on_close=lambda ws, c, m: print("[TTS] WS rozłączony"),
                    )
                    ws.run_forever(ping_interval=30, ping_timeout=10)
                except Exception as exc:
                    print(f"[TTS] Błąd połączenia: {exc}")
                print("[TTS] Reconnect za 10 s...")
                time.sleep(10)

        threading.Thread(target=_run, daemon=True, name="tts-watcher").start()


# Uruchom watcher przy starcie aplikacji
_watcher = TtsWatcher()
_watcher.start()


# ─── Flask routes — dashboard ────────────────────────────────────────────────

@app.route("/")
def index():
    ingress_path = request.headers.get("X-Ingress-Path", "")
    opts = load_options()
    return render_template("index.html", ingress_path=ingress_path, opts=opts)


@app.route("/api/entities")
def entities():
    """Encje do edytora dashboardu (filtrowane domeną)."""
    try:
        r = requests.get(f"{HA_API}/states", headers=ha_headers(), timeout=10)
        r.raise_for_status()
        result = []
        for s in r.json():
            domain = s["entity_id"].split(".")[0]
            if domain not in DASHBOARD_DOMAINS:
                continue
            attrs = s.get("attributes", {})
            result.append({
                "entity_id":           s["entity_id"],
                "state":               s["state"],
                "domain":              domain,
                "friendly_name":       attrs.get("friendly_name", s["entity_id"]),
                "unit_of_measurement": attrs.get("unit_of_measurement", ""),
                "device_class":        attrs.get("device_class", ""),
            })
        result.sort(key=lambda x: x["entity_id"])
        return jsonify(result)
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/tts_entities")
def tts_entities():
    """Encje dostępne w edytorze reguł TTS (szerszy zakres domen)."""
    try:
        r = requests.get(f"{HA_API}/states", headers=ha_headers(), timeout=10)
        r.raise_for_status()
        result = []
        for s in r.json():
            domain = s["entity_id"].split(".")[0]
            if domain not in TTS_DOMAINS:
                continue
            attrs = s.get("attributes", {})
            result.append({
                "entity_id":     s["entity_id"],
                "state":         s["state"],
                "domain":        domain,
                "friendly_name": attrs.get("friendly_name", s["entity_id"]),
            })
        result.sort(key=lambda x: x["entity_id"])
        return jsonify(result)
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/dashboard", methods=["GET"])
def get_dashboard():
    return jsonify(load_dashboard())


@app.route("/api/dashboard", methods=["POST"])
def post_dashboard():
    data = request.get_json()
    if not data:
        return jsonify({"error": "no data"}), 400
    save_dashboard(data)
    return jsonify({"ok": True})


@app.route("/api/send", methods=["POST"])
def send_to_device():
    opts = load_options()
    ip   = opts.get("device_ip", "")
    port = opts.get("device_port", 80)
    if not ip:
        return jsonify({"ok": False, "error": "Nie ustawiono device_ip w opcjach dodatku"}), 400
    data = request.get_json()
    try:
        r = requests.post(f"http://{ip}:{port}/api/dashboard", json=data, timeout=8)
        return jsonify({"ok": r.ok, "status": r.status_code})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route("/api/fetch")
def fetch_from_device():
    opts = load_options()
    ip   = opts.get("device_ip", "")
    port = opts.get("device_port", 80)
    if not ip:
        return jsonify({"error": "Nie ustawiono device_ip w opcjach dodatku"}), 400
    try:
        r = requests.get(f"http://{ip}:{port}/api/dashboard", timeout=8)
        r.raise_for_status()
        return jsonify(r.json())
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/device_status")
def device_status():
    opts = load_options()
    ip   = opts.get("device_ip", "")
    port = opts.get("device_port", 80)
    if not ip:
        return jsonify({"online": False, "ip": ""})
    try:
        r = requests.get(f"http://{ip}:{port}/api/ha", timeout=3)
        return jsonify({"online": r.ok, "ip": ip})
    except Exception:
        return jsonify({"online": False, "ip": ip})


# ─── Flask routes — TTS ──────────────────────────────────────────────────────

@app.route("/api/camera/<path:entity_id>")
def camera_snapshot(entity_id):
    """Proxy snapshot kamery HA → JPEG dla ESP32."""
    try:
        r = requests.get(f"{HA_API}/camera_proxy/{entity_id}",
                         headers=ha_headers(), timeout=8, stream=True)
        if not r.ok:
            return jsonify({"error": f"HA returned {r.status_code}"}), r.status_code
        from flask import Response
        return Response(r.content, mimetype="image/jpeg")
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/camera_entities")
def camera_entities():
    """Lista encji kamery (domain=camera) dla edytora UI."""
    try:
        r = requests.get(f"{HA_API}/states", headers=ha_headers(), timeout=10)
        r.raise_for_status()
        result = []
        for s in r.json():
            if s["entity_id"].split(".")[0] != "camera":
                continue
            attrs = s.get("attributes", {})
            result.append({
                "entity_id":     s["entity_id"],
                "friendly_name": attrs.get("friendly_name", s["entity_id"]),
            })
        result.sort(key=lambda x: x["entity_id"])
        return jsonify(result)
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/tts_rules", methods=["GET"])
def get_tts_rules():
    return jsonify(load_tts_rules())


@app.route("/api/tts_rules", methods=["POST"])
def post_tts_rules():
    data = request.get_json()
    if not data or "rules" not in data:
        return jsonify({"error": "Brak pola 'rules'"}), 400
    save_tts_rules(data)
    _watcher.reload()
    return jsonify({"ok": True})


@app.route("/api/tts_test", methods=["POST"])
def tts_test():
    """Natychmiastowe wysłanie tekstu do panelu (test reguły)."""
    data = request.get_json()
    text = (data or {}).get("text", "").strip()
    if not text:
        return jsonify({"error": "Brak tekstu"}), 400
    opts = load_options()
    ip   = opts.get("device_ip", "")
    port = opts.get("device_port", 80)
    if not ip:
        return jsonify({"error": "Brak device_ip"}), 400
    try:
        r = requests.post(f"http://{ip}:{port}/api/speak",
                          json={"text": text}, timeout=6)
        return jsonify({"ok": r.ok, "status": r.status_code})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8765, debug=False)
