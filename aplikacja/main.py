"""
HA Panel — Edytor Dashboardu
Wizualny edytor konfiguracji dla ESP32-P4 HA Panel.

Wymaga: Python 3.10+, PySide6, requests
Uruchomienie: python main.py  (lub kliknij uruchom.bat)
"""

import sys
import json
from pathlib import Path

from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QLabel, QPushButton, QLineEdit, QListWidget, QListWidgetItem,
    QFrame, QScrollArea, QGridLayout, QDialog, QFormLayout,
    QMessageBox, QFileDialog, QInputDialog, QAbstractItemView, QSizePolicy,
    QComboBox, QRadioButton, QButtonGroup, QCheckBox,
)
from PySide6.QtCore import Qt, QThread, Signal, QByteArray, QMimeData, QPoint
from PySide6.QtGui import QFont, QPainter, QPen, QColor, QBrush

from config import AppConfig
from ha_client import HaClient

# ─── Stałe (muszą odpowiadać wartościom w ha_entities.h) ─────────────────────
MAX_SCREENS = 8
MAX_TILES   = 12
MAX_ROWS    = 6

# Obszar roboczy ESP w pikselach (1280×800 minus statusbar 64px i tabbar 44px)
ESP_W = 1280
ESP_H = 692

# Siatka przyciągania przy przeciąganiu (ESP piksele)
SNAP = 16

# Minimalny rozmiar kafelka (ESP piksele)
MIN_TILE_W = 128
MIN_TILE_H = 56

# Domyślny rozmiar nowego kafelka (ESP piksele) — zajmuje ~½ szerokości ekranu
DEFAULT_TILE_W = 488
DEFAULT_TILE_H = 200


# ─── Wątek do pobierania encji ────────────────────────────────────────────────
class _LoadEntitiesThread(QThread):
    done  = Signal(list)
    error = Signal(str)

    def __init__(self, client: HaClient):
        super().__init__()
        self.client = client

    def run(self):
        try:
            self.done.emit(self.client.get_entities())
        except Exception as e:
            self.error.emit(str(e))


# ─── Wątek wysyłania/pobierania dashboardu z panelu ──────────────────────────
class _PanelThread(QThread):
    """GET lub POST /api/dashboard na ESP32-P4."""
    done  = Signal(dict)   # GET: odebrany JSON
    ok    = Signal(str)    # POST: komunikat sukcesu
    error = Signal(str)

    def __init__(self, ip: str, payload: dict | None = None):
        super().__init__()
        self.ip      = ip.strip().rstrip("/")
        self.payload = payload   # None = GET, dict = POST

    def run(self):
        import urllib.request, urllib.error
        url = f"http://{self.ip}/api/dashboard"
        try:
            if self.payload is None:
                req = urllib.request.Request(url)
                with urllib.request.urlopen(req, timeout=8) as r:
                    data = json.loads(r.read().decode())
                self.done.emit(data)
            else:
                body = json.dumps(self.payload, ensure_ascii=False).encode()
                req  = urllib.request.Request(
                    url, data=body,
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                with urllib.request.urlopen(req, timeout=8) as r:
                    self.ok.emit(r.read().decode())
        except urllib.error.URLError as e:
            self.error.emit(str(e.reason))
        except Exception as e:
            self.error.emit(str(e))


# ─── Lista encji z niestandardowym drag MIME ──────────────────────────────────
class EntityListWidget(QListWidget):
    """QListWidget pakujący dane encji do MIME przy przeciąganiu."""

    def mimeData(self, items):
        mime = super().mimeData(items)
        if items:
            ent = items[0].data(Qt.UserRole)
            if ent:
                mime.setData(
                    "application/x-ha-entity",
                    QByteArray(json.dumps(ent, ensure_ascii=False).encode("utf-8")),
                )
        return mime


# ─── Kafelek na canvasie ──────────────────────────────────────────────────────
class TileWidget(QFrame):
    """
    Jeden kafelek na canvasie.
    Obsługuje:
      • kliknięcie (zaznaczenie)
      • przeciąganie (przesuwanie — cała powierzchnia kafelka)
      • zmianę rozmiaru (uchwyt ▪ w prawym dolnym rogu)
      • drag-and-drop encji z listy
    Współrzędne tile_data["x/y/w/h"] są w pikselach ESP (0–1280 × 0–692).
    """
    HANDLE = 14  # rozmiar uchwytu resize [px canvas]

    def __init__(self, tile_data: dict, tile_idx: int,
                 get_scale,           # callable() → float
                 on_click,            # callback(tile_idx)
                 on_drop,             # callback(tile_idx, entity_dict)
                 on_move,             # callback(tile_idx, esp_x, esp_y)
                 on_resize,           # callback(tile_idx, esp_w, esp_h)
                 get_state=None,      # callable(entity_id) → str|None
                 parent=None):
        super().__init__(parent)
        self.tile_data  = tile_data
        self.tile_idx   = tile_idx
        self._get_scale = get_scale
        self._on_click  = on_click
        self._on_drop   = on_drop
        self._on_move   = on_move
        self._on_resize = on_resize
        self._get_state = get_state
        self._selected  = False

        self._drag_mode  = None        # None | 'move' | 'resize'
        self._press_loc  = QPoint()    # pozycja kliknięcia w lokalnych coords
        self._press_wpos = QPoint()    # pozycja widgetu przy kliknięciu
        self._press_wsz  = (0, 0)      # rozmiar widgetu przy kliknięciu

        self.setAcceptDrops(True)
        self.setMouseTracking(True)

        self._lay = QVBoxLayout(self)
        self._lay.setContentsMargins(8, 6, 8, 6)
        self._lay.setSpacing(2)
        self._rebuild()

    # ── przeliczniki ESP ↔ canvas ─────────────────────────────────────────────
    def _sc(self) -> float:
        return max(0.01, self._get_scale())

    def apply_geometry(self):
        """Pozycjonuje kafelek wg tile_data i aktualnej skali canvasu."""
        sc = self._sc()
        self.move(int(self.tile_data.get("x", 0) * sc),
                  int(self.tile_data.get("y", 0) * sc))
        self.resize(max(MIN_TILE_W, int(self.tile_data.get("w", DEFAULT_TILE_W) * sc)),
                    max(MIN_TILE_H, int(self.tile_data.get("h", DEFAULT_TILE_H) * sc)))

    # ── wygląd ────────────────────────────────────────────────────────────────

    @staticmethod
    def _sensor_icon_color(sensor_type: str, state: str | None) -> tuple[str, str]:
        """Zwraca (ikona, kolor_hex) pasujące do wyglądu firmware."""
        if sensor_type == "temperature":
            icon = "🌡"
            try:
                v = float(state) if state else None
            except ValueError:
                v = None
            if v is None:     color = "#607080"
            elif v > 28:      color = "#e06050"
            elif v > 24:      color = "#e0a050"
            elif v > 18:      color = "#50c8a0"
            else:             color = "#6090e0"
            return icon, color
        elif sensor_type == "humidity":
            return "💧", "#6090e0"
        elif sensor_type == "power":
            return "⚡", "#e0c050"
        elif sensor_type in ("cpu", "memory"):
            icon = "🖥" if sensor_type == "cpu" else "💾"
            try:
                v = float(state) if state else None
            except ValueError:
                v = None
            if v is None:    color = "#607080"
            elif v > 80:     color = "#e06050"
            elif v > 50:     color = "#e0a050"
            else:            color = "#50c880"
            return icon, color
        elif sensor_type == "illuminance":
            return "💡", "#e0e080"
        else:
            return "•", "#9ab8c8"

    def _build_row_widget(self, row: dict) -> QWidget:
        """Tworzy widget pojedynczego wiersza encji — z prawdziwym stanem."""
        is_switch = row.get("sensor_type") == "switch"
        eid   = row.get("entity_id", "")
        label = row.get("label") or eid
        unit  = row.get("unit", "")
        state = self._get_state(eid) if self._get_state else None

        w = QWidget()
        w.setStyleSheet("background:transparent;")
        lay = QHBoxLayout(w)
        lay.setContentsMargins(0, 1, 0, 1)
        lay.setSpacing(4)

        if is_switch:
            is_on = (state in ("on", "true", "1", "home", "playing", "open")
                     if state is not None else None)
            btn = QFrame()
            btn.setFixedHeight(26)
            if is_on is None:
                bg, fg, txt = "#1a2a38", "#405060", f"⬤  {label}  —"
            elif is_on:
                bg, fg, txt = "#0C4838", "#2ddf99", f"⬤  {label}  WŁĄCZONY"
            else:
                bg, fg, txt = "#1E2A35", "#e06060", f"⬤  {label}  WYŁĄCZONY"
            btn.setStyleSheet(
                f"background:{bg}; border-radius:6px; border:none;")
            btn_lay = QHBoxLayout(btn)
            btn_lay.setContentsMargins(8, 0, 8, 0)
            lbl = QLabel(txt)
            lbl.setStyleSheet(
                f"color:{fg}; font-size:10px; font-weight:bold;")
            btn_lay.addWidget(lbl)
            lay.addWidget(btn, stretch=1)
        else:
            icon, color = self._sensor_icon_color(
                row.get("sensor_type", "generic"), state)
            if state is not None:
                value_str = f"{state}{' ' + unit if unit else ''}"
                text = f"{icon} {label}:  {value_str}"
            else:
                text = f"{icon} {label}:  —"
            lbl = QLabel(text)
            lbl.setStyleSheet(f"color:{color}; font-size:10px;")
            lbl.setWordWrap(False)
            lay.addWidget(lbl)
            lay.addStretch()

        return w

    def _rebuild(self):
        while self._lay.count():
            item = self._lay.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

        border = "#0f9b8e" if self._selected else "#2a4a5a"
        bg     = "#0d3d4f" if self._selected else "#162535"
        self.setStyleSheet(f"""
            TileWidget {{ background:{bg}; border:2px solid {border};
                          border-radius:8px; }}
            QLabel {{ background:transparent; }}
        """)

        rows        = self.tile_data.get("rows", [])
        layout_mode = self.tile_data.get("layout", "vertical")

        name_lbl = QLabel(self.tile_data.get("label") or "—")
        name_lbl.setStyleSheet(
            "color:#7ecfc5; font-size:12px; font-weight:bold;")
        name_lbl.setWordWrap(False)

        if layout_mode == "horizontal" and rows:
            outer = QHBoxLayout()
            outer.setContentsMargins(0, 0, 0, 0)
            outer.setSpacing(8)
            outer.addWidget(name_lbl)

            sep = QFrame()
            sep.setFrameShape(QFrame.VLine)
            sep.setStyleSheet("background:#2a4050; border:none; min-width:1px; max-width:1px;")
            outer.addWidget(sep)

            rows_col = QVBoxLayout()
            rows_col.setSpacing(2)
            rows_col.setContentsMargins(0, 0, 0, 0)
            for row in rows:
                rows_col.addWidget(self._build_row_widget(row))
            rows_col.addStretch()
            outer.addLayout(rows_col)
            outer.addStretch()
            self._lay.addLayout(outer)
        else:
            self._lay.addWidget(name_lbl)
            sep = QFrame()
            sep.setFrameShape(QFrame.HLine)
            sep.setStyleSheet("background:#2a4050; border:none; max-height:1px;")
            self._lay.addWidget(sep)
            for row in rows:
                self._lay.addWidget(self._build_row_widget(row))

        self._lay.addStretch()
        if not rows:
            hint = QLabel("↓ upuść encję tutaj")
            hint.setStyleSheet("color:#2a4a5a; font-size:9px;")
            hint.setAlignment(Qt.AlignCenter)
            self._lay.addWidget(hint)

    def set_selected(self, s: bool):
        if self._selected != s:
            self._selected = s
            self._rebuild()

    def update_data(self, d: dict):
        self.tile_data = d
        self._rebuild()

    # ── kursor ────────────────────────────────────────────────────────────────
    def _in_handle(self, pos: QPoint) -> bool:
        return (pos.x() >= self.width()  - self.HANDLE and
                pos.y() >= self.height() - self.HANDLE)

    # ── zdarzenia myszy ───────────────────────────────────────────────────────
    def mousePressEvent(self, ev):
        if ev.button() == Qt.LeftButton:
            self._press_loc  = ev.pos()
            self._press_wpos = self.pos()
            self._press_wsz  = (self.width(), self.height())
            self._drag_mode  = 'resize' if self._in_handle(ev.pos()) else 'move'
            if self._drag_mode == 'move':
                self._on_click(self.tile_idx)
        super().mousePressEvent(ev)

    def mouseMoveEvent(self, ev):
        sc     = self._sc()
        canvas = self.parent()

        if self._drag_mode is None:
            self.setCursor(Qt.SizeFDiagCursor if self._in_handle(ev.pos())
                           else Qt.SizeAllCursor)
            return

        if self._drag_mode == 'move':
            delta = ev.pos() - self._press_loc
            cx = self._press_wpos.x() + delta.x()
            cy = self._press_wpos.y() + delta.y()
            if canvas:
                cx = max(0, min(cx, canvas.width()  - self.width()))
                cy = max(0, min(cy, canvas.height() - self.height()))
            ex = max(0, round(cx / sc / SNAP) * SNAP)
            ey = max(0, round(cy / sc / SNAP) * SNAP)
            self.move(int(ex * sc), int(ey * sc))
            self.tile_data["x"] = ex
            self.tile_data["y"] = ey
            self._on_move(self.tile_idx, ex, ey)

        elif self._drag_mode == 'resize':
            delta = ev.pos() - self._press_loc
            cw = self._press_wsz[0] + delta.x()
            ch = self._press_wsz[1] + delta.y()
            if canvas:
                cw = min(cw, canvas.width()  - self.x())
                ch = min(ch, canvas.height() - self.y())
            ew = max(MIN_TILE_W, round(cw / sc / SNAP) * SNAP)
            eh = max(MIN_TILE_H, round(ch / sc / SNAP) * SNAP)
            self.resize(int(ew * sc), int(eh * sc))
            self.tile_data["w"] = ew
            self.tile_data["h"] = eh
            self._on_resize(self.tile_idx, ew, eh)

        super().mouseMoveEvent(ev)

    def mouseReleaseEvent(self, ev):
        self._drag_mode = None
        self.setCursor(Qt.SizeFDiagCursor if self._in_handle(ev.pos())
                       else Qt.SizeAllCursor)
        super().mouseReleaseEvent(ev)

    # ── drag-and-drop encji ───────────────────────────────────────────────────
    def dragEnterEvent(self, ev):
        if ev.mimeData().hasFormat("application/x-ha-entity"):
            ev.acceptProposedAction()
        else:
            ev.ignore()

    def dropEvent(self, ev):
        if ev.mimeData().hasFormat("application/x-ha-entity"):
            raw    = bytes(ev.mimeData().data("application/x-ha-entity"))
            entity = json.loads(raw.decode("utf-8"))
            self._on_drop(self.tile_idx, entity)
            ev.acceptProposedAction()
        self._rebuild()

    # ── uchwyt resize (rysowanie) ──────────────────────────────────────────────
    def paintEvent(self, ev):
        super().paintEvent(ev)
        p = QPainter(self)
        c = QColor("#0f9b8e") if self._selected else QColor("#334455")
        p.setBrush(QBrush(c))
        p.setPen(Qt.NoPen)
        sz = self.HANDLE
        p.drawRect(self.width() - sz, self.height() - sz, sz, sz)
        p.end()


# ─── Canvas z siatką ──────────────────────────────────────────────────────────
class DashboardCanvas(QWidget):
    """
    Obszar reprezentujący ekran ESP (1024×492 px obszar roboczy).
    Rysuje siatkę pomocniczą i ramkę obszaru ESP.
    TileWidget-y są bezpośrednimi dziećmi — pozycjonowanie absolutne.
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(500, 230)
        self._show_grid = True

    def scale(self) -> float:
        """Skala: piksele ESP → piksele canvasu."""
        return self.width() / ESP_W if self.width() > 0 else 1.0

    def set_show_grid(self, show: bool):
        self._show_grid = show
        self.update()

    def paintEvent(self, ev):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor(8, 17, 26))

        if self._show_grid:
            sc = self.scale()
            # Drobna siatka (co SNAP px ESP)
            step_s = max(4, int(SNAP * sc))
            p.setPen(QPen(QColor(18, 38, 52), 1))
            for x in range(0, self.width() + 1, step_s):
                p.drawLine(x, 0, x, self.height())
            for y in range(0, self.height() + 1, step_s):
                p.drawLine(0, y, self.width(), y)
            # Grubsza siatka (co 64px ESP)
            step_L = max(8, int(64 * sc))
            p.setPen(QPen(QColor(26, 54, 72), 1))
            for x in range(0, self.width() + 1, step_L):
                p.drawLine(x, 0, x, self.height())
            for y in range(0, self.height() + 1, step_L):
                p.drawLine(0, y, self.width(), y)

        # Ramka obszaru ESP (teal)
        sc = self.scale()
        p.setPen(QPen(QColor(15, 155, 142), 2))
        p.drawRect(0, 0, int(ESP_W * sc) - 1, int(ESP_H * sc) - 1)
        p.end()

    def resizeEvent(self, ev):
        super().resizeEvent(ev)
        # Zachowaj proporcje ekranu ESP
        new_h = int(self.width() * ESP_H / ESP_W)
        self.setFixedHeight(max(200, new_h))
        # Repozycjonuj wszystkie kafelki
        for child in self.children():
            if isinstance(child, TileWidget):
                child.apply_geometry()


# ─── Główne okno ──────────────────────────────────────────────────────────────
class MainWindow(QMainWindow):

    def __init__(self):
        super().__init__()
        self.config = AppConfig()
        self.ha_client: HaClient | None = None
        self.entities: list[dict] = []
        self.entity_states: dict[str, str] = {}   # entity_id → bieżący stan
        self._load_thread: _LoadEntitiesThread | None = None
        self._refresh_thread: _LoadEntitiesThread | None = None

        self.dashboard: dict = {"default_screen": 0, "screens": []}
        self.current_screen: int = 0
        self.selected_tile: int  = -1
        self._tile_widgets: list[TileWidget] = []

        from PySide6.QtCore import QTimer
        self._refresh_timer = QTimer(self)
        self._refresh_timer.timeout.connect(self._auto_refresh_states)
        self._refresh_timer.start(30_000)  # co 30s odśwież stany

        # Referencje do widgetów właściwości układu kafelka
        self._layout_group: QButtonGroup | None = None
        self._layout_radio_v: QRadioButton | None = None
        self._layout_radio_h: QRadioButton | None = None

        self.setWindowTitle("HA Panel — Edytor Dashboardu")
        self.resize(1400, 820)
        self._build_ui()
        self._apply_style()

        if self.config.is_ready():
            self.ha_client = HaClient(self.config.ha_url, self.config.ha_token)
            self._set_status("✓ Dane połączenia wczytane — kliknij 'Pobierz encje z HA'", ok=True)
            self.btn_connect.setText("⚡ Połączono")

    # ══════════════════════════════════════════════════════════════════════════
    # Budowa UI
    # ══════════════════════════════════════════════════════════════════════════

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        root.addWidget(self._build_toolbar())

        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self._build_entities_panel())
        splitter.addWidget(self._build_canvas_panel())
        splitter.addWidget(self._build_properties_panel())
        splitter.setSizes([230, 900, 280])
        splitter.setCollapsible(0, False)
        splitter.setCollapsible(1, False)
        splitter.setCollapsible(2, False)
        root.addWidget(splitter)

    # ── Pasek narzędzi ────────────────────────────────────────────────────────
    def _build_toolbar(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("toolbar")
        lay = QHBoxLayout(bar)
        lay.setContentsMargins(14, 8, 14, 8)
        lay.setSpacing(8)

        title = QLabel("<b>HA Panel — Edytor Dashboardu</b>")
        title.setStyleSheet("font-size: 15px;")
        lay.addWidget(title)
        lay.addStretch()

        self.lbl_status = QLabel("Nie połączono z HA")
        self.lbl_status.setObjectName("lbl_status")

        btn_import = QPushButton("📂  Importuj JSON")
        btn_import.clicked.connect(self._import_json)

        btn_export = QPushButton("💾  Exportuj JSON")
        btn_export.clicked.connect(self._export_json)

        btn_send = QPushButton("📡  Wyślij na panel")
        btn_send.setObjectName("btn_primary")
        btn_send.setToolTip("Wyślij dashboard do ESP32-P4 (POST /api/dashboard)")
        btn_send.clicked.connect(self._send_to_panel)

        btn_fetch = QPushButton("⬇  Pobierz z panelu")
        btn_fetch.setToolTip("Pobierz dashboard z ESP32-P4 (GET /api/dashboard)")
        btn_fetch.clicked.connect(self._fetch_from_panel)

        self.btn_connect = QPushButton("⚡  Połącz z HA")
        self.btn_connect.setObjectName("btn_primary")
        self.btn_connect.clicked.connect(self._show_connect_dialog)

        for w in [self.lbl_status, btn_import, btn_export,
                  btn_send, btn_fetch, self.btn_connect]:
            lay.addWidget(w)

        return bar

    # ── Panel lewy: encje ─────────────────────────────────────────────────────
    def _build_entities_panel(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("side_panel")
        panel.setMinimumWidth(210)
        lay = QVBoxLayout(panel)
        lay.setContentsMargins(10, 10, 10, 10)
        lay.setSpacing(6)

        lay.addWidget(QLabel("<b>Encje z Home Assistant</b>"))

        self.search_box = QLineEdit()
        self.search_box.setPlaceholderText("🔍  Filtruj encje...")
        self.search_box.textChanged.connect(self._filter_entities)
        lay.addWidget(self.search_box)

        self.entity_list = EntityListWidget()
        self.entity_list.setDragEnabled(True)
        self.entity_list.setDragDropMode(QAbstractItemView.DragOnly)
        self.entity_list.setSelectionMode(QAbstractItemView.SingleSelection)
        self.entity_list.setToolTip(
            "Przeciągnij encję na kafelek\n"
            "lub dwukliknij aby dodać do zaznaczonego kafelka"
        )
        self.entity_list.itemDoubleClicked.connect(self._entity_double_clicked)
        lay.addWidget(self.entity_list)

        btn_load = QPushButton("🔄  Pobierz encje z HA")
        btn_load.clicked.connect(self._load_entities)
        lay.addWidget(btn_load)

        btn_refresh = QPushButton("⟳  Odśwież stany")
        btn_refresh.setToolTip("Pobierz aktualne wartości encji i odśwież podgląd kafelków")
        btn_refresh.clicked.connect(self._load_entities)
        lay.addWidget(btn_refresh)

        self.lbl_entity_count = QLabel("")
        self.lbl_entity_count.setStyleSheet("font-size: 10px; color: #607080;")
        lay.addWidget(self.lbl_entity_count)

        return panel

    # ── Panel środkowy: canvas ────────────────────────────────────────────────
    def _build_canvas_panel(self) -> QWidget:
        panel = QWidget()
        lay = QVBoxLayout(panel)
        lay.setContentsMargins(8, 8, 8, 8)
        lay.setSpacing(6)

        # Pasek nawigacji ekranami
        nav = QWidget()
        nav_lay = QHBoxLayout(nav)
        nav_lay.setContentsMargins(0, 0, 0, 0)
        nav_lay.setSpacing(6)

        self.btn_prev = QPushButton("◀")
        self.btn_prev.setFixedWidth(32)
        self.btn_prev.clicked.connect(lambda: self._switch_screen(-1))

        self.lbl_screen = QLabel("—")
        self.lbl_screen.setObjectName("lbl_screen")
        self.lbl_screen.setMinimumWidth(160)

        self.btn_next = QPushButton("▶")
        self.btn_next.setFixedWidth(32)
        self.btn_next.clicked.connect(lambda: self._switch_screen(+1))

        btn_rename  = QPushButton("✏️")
        btn_rename.setFixedWidth(36)
        btn_rename.setToolTip("Zmień nazwę ekranu")
        btn_rename.clicked.connect(self._rename_screen)

        btn_default = QPushButton("★")
        btn_default.setFixedWidth(36)
        btn_default.setToolTip("Ustaw jako domyślny ekran")
        btn_default.clicked.connect(self._set_default_screen)

        btn_add_screen = QPushButton("＋ Ekran")
        btn_add_screen.clicked.connect(self._add_screen)

        btn_del_screen = QPushButton("🗑 Ekran")
        btn_del_screen.clicked.connect(self._delete_screen)

        btn_add_tile = QPushButton("＋ Kafelek")
        btn_add_tile.setObjectName("btn_primary")
        btn_add_tile.clicked.connect(self._add_tile)

        self.chk_grid = QCheckBox("Siatka")
        self.chk_grid.setChecked(True)
        self.chk_grid.setToolTip("Pokaż/ukryj siatkę pomocniczą")
        self.chk_grid.toggled.connect(self._toggle_grid)

        self.lbl_screen_info = QLabel("")
        self.lbl_screen_info.setStyleSheet("color: #607080; font-size: 10px;")

        for w in [self.btn_prev, self.lbl_screen, self.btn_next,
                  btn_rename, btn_default,
                  btn_add_screen, btn_del_screen, btn_add_tile,
                  self.chk_grid]:
            nav_lay.addWidget(w)
        nav_lay.addStretch()
        nav_lay.addWidget(self.lbl_screen_info)
        lay.addWidget(nav)

        # Canvas w ScrollArea
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setObjectName("canvas_scroll")
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)

        self.canvas = DashboardCanvas()
        scroll.setWidget(self.canvas)
        lay.addWidget(scroll)

        lbl_hint = QLabel(
            "💡 Przeciągnij kafelek aby przesunąć  •  "
            "▪ prawy dolny róg = zmień rozmiar  •  "
            f"Siatka: {SNAP}px ESP")
        lbl_hint.setStyleSheet("color:#405060; font-size:10px;")
        lay.addWidget(lbl_hint)

        return panel

    # ── Panel prawy: właściwości ───────────────────────────────────────────────
    def _build_properties_panel(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("side_panel")
        panel.setMinimumWidth(260)
        lay = QVBoxLayout(panel)
        lay.setContentsMargins(10, 10, 10, 10)
        lay.setSpacing(8)

        lay.addWidget(QLabel("<b>Właściwości kafelka</b>"))

        # Nazwa kafelka
        lay.addWidget(QLabel("Nazwa kafelka:"))
        self.tile_name_edit = QLineEdit()
        self.tile_name_edit.setPlaceholderText("np. Salon, Kotłownia...")
        self.tile_name_edit.setEnabled(False)
        self.tile_name_edit.textChanged.connect(self._on_tile_name_changed)
        lay.addWidget(self.tile_name_edit)

        # Układ kafelka
        layout_frame = QFrame()
        layout_frame.setObjectName("row_frame")
        lf_lay = QVBoxLayout(layout_frame)
        lf_lay.setContentsMargins(8, 6, 8, 6)
        lf_lay.setSpacing(4)
        lf_lay.addWidget(QLabel("Układ wyświetlania:"))

        self._layout_radio_v = QRadioButton("↕  Pionowy — nazwa u góry, encje pod spodem")
        self._layout_radio_h = QRadioButton("↔  Poziomy — nazwa po lewej, encje obok siebie")
        self._layout_radio_v.setEnabled(False)
        self._layout_radio_h.setEnabled(False)
        self._layout_radio_v.setChecked(True)

        self._layout_group = QButtonGroup(layout_frame)
        self._layout_group.addButton(self._layout_radio_v, 0)
        self._layout_group.addButton(self._layout_radio_h, 1)
        self._layout_group.idClicked.connect(self._on_layout_changed)

        lf_lay.addWidget(self._layout_radio_v)
        lf_lay.addWidget(self._layout_radio_h)
        lay.addWidget(layout_frame)

        # Lista wierszy (encji)
        lay.addWidget(QLabel("Wiersze (encje w kafelku):"))

        rows_scroll = QScrollArea()
        rows_scroll.setWidgetResizable(True)
        rows_scroll.setMinimumHeight(200)

        self.rows_container = QWidget()
        self.rows_layout = QVBoxLayout(self.rows_container)
        self.rows_layout.setContentsMargins(2, 2, 2, 2)
        self.rows_layout.setSpacing(4)
        self.rows_layout.addStretch()

        rows_scroll.setWidget(self.rows_container)
        lay.addWidget(rows_scroll)

        # Usuń kafelek
        btn_del_tile = QPushButton("🗑  Usuń ten kafelek")
        btn_del_tile.setObjectName("btn_danger")
        btn_del_tile.clicked.connect(self._delete_tile)
        lay.addWidget(btn_del_tile)

        lay.addStretch()

        hint = QLabel(
            "💡 Jak dodać encję do kafelka:\n"
            "1. Kliknij kafelek na canvasie\n"
            "2. Przeciągnij encję z listy\n"
            "   — lub —\n"
            "   Dwukliknij encję na liście"
        )
        hint.setStyleSheet("color: #607080; font-size: 10px;")
        hint.setWordWrap(True)
        lay.addWidget(hint)

        return panel

    # ══════════════════════════════════════════════════════════════════════════
    # Styl aplikacji
    # ══════════════════════════════════════════════════════════════════════════

    def _apply_style(self):
        self.setStyleSheet("""
            QMainWindow, QWidget          { background: #131f2a; color: #c8dce8;
                                            font-size: 13px; font-family: Segoe UI; }

            #toolbar                      { background: #0d1820;
                                            border-bottom: 1px solid #223040; }
            #side_panel                   { background: #0f1c28;
                                            border-right: 1px solid #1e3040; }
            #canvas_scroll                { background: #0a1520; border: none; }

            QLabel                        { background: transparent; }
            #lbl_screen                   { font-size: 15px; font-weight: bold;
                                            color: #7ecfc5; }
            #lbl_status                   { color: #607080; font-size: 11px; }

            QLineEdit                     { background: #0a1520;
                                            border: 1px solid #1e3a4a;
                                            border-radius: 5px; padding: 6px;
                                            color: #c8dce8; }
            QLineEdit:focus               { border-color: #0f9b8e; }
            QLineEdit:disabled            { color: #3a5060; }

            QPushButton                   { background: #1e3040;
                                            border: 1px solid #2e4a5a;
                                            border-radius: 5px; padding: 6px 12px;
                                            color: #c8dce8; }
            QPushButton:hover             { background: #2e4050; }
            QPushButton:pressed           { background: #0e2030; }

            #btn_primary                  { background: #0f6b63;
                                            border-color: #1a8070; }
            #btn_primary:hover            { background: #1a8070; }

            #btn_danger                   { background: #6b1515;
                                            border-color: #8a2020; }
            #btn_danger:hover             { background: #8a2020; }

            QListWidget                   { background: #0a1520;
                                            border: 1px solid #1e3a4a;
                                            border-radius: 5px; }
            QListWidget::item             { padding: 4px 8px; border-radius: 3px; }
            QListWidget::item:hover       { background: #1a2f40; }
            QListWidget::item:selected    { background: #0f4a60; }

            QScrollArea                   { border: none; }
            QScrollBar:vertical           { background: #0a1520; width: 8px;
                                            border-radius: 4px; }
            QScrollBar::handle:vertical   { background: #2a4050;
                                            border-radius: 4px; min-height: 20px; }
            QSplitter::handle             { background: #1e3040; }

            QComboBox                     { background: #0a1520;
                                            border: 1px solid #1e3a4a;
                                            border-radius: 5px; padding: 5px 8px;
                                            color: #c8dce8; }
            QComboBox:focus               { border-color: #0f9b8e; }
            QComboBox::drop-down          { border: none; width: 20px; }
            QComboBox QAbstractItemView   { background: #0f1c28;
                                            border: 1px solid #1e3a4a;
                                            selection-background-color: #0f4a60;
                                            color: #c8dce8; }

            QRadioButton                  { spacing: 6px; }
            QRadioButton::indicator       { width: 14px; height: 14px;
                                            border-radius: 7px;
                                            border: 2px solid #2e4a5a;
                                            background: #0a1520; }
            QRadioButton::indicator:checked { background: #0f9b8e;
                                              border-color: #0f9b8e; }
            QCheckBox                     { spacing: 6px; }
            QCheckBox::indicator          { width: 14px; height: 14px;
                                            border-radius: 3px;
                                            border: 2px solid #2e4a5a;
                                            background: #0a1520; }
            QCheckBox::indicator:checked  { background: #0f9b8e;
                                            border-color: #0f9b8e; }

            #row_frame                    { background: #0a1520;
                                            border: 1px solid #1e3a4a;
                                            border-radius: 6px; }
        """)

    # ══════════════════════════════════════════════════════════════════════════
    # Panel encji
    # ══════════════════════════════════════════════════════════════════════════

    def _get_entity_state(self, entity_id: str) -> str | None:
        return self.entity_states.get(entity_id)

    def _auto_refresh_states(self):
        """Co 30s cicho odświeża stany encji jeśli jest połączenie."""
        if not self.ha_client or not self.entities:
            return
        if self._refresh_thread and self._refresh_thread.isRunning():
            return
        self._refresh_thread = _LoadEntitiesThread(self.ha_client)
        self._refresh_thread.done.connect(self._on_states_refreshed)
        self._refresh_thread.start()

    def _on_states_refreshed(self, entities: list):
        self.entity_states = {e["entity_id"]: e["state"] for e in entities}
        for tw in self._tile_widgets:
            tw.update_data(tw.tile_data)

    def _set_status(self, text: str, ok: bool = False):
        color = "#2a9b8e" if ok else "#607080"
        self.lbl_status.setStyleSheet(f"color: {color}; font-size: 11px;")
        self.lbl_status.setText(text)

    def _filter_entities(self, text: str):
        text = text.lower()
        for i in range(self.entity_list.count()):
            item = self.entity_list.item(i)
            item.setHidden(text not in item.text().lower())

    def _load_entities(self):
        if not self.ha_client:
            QMessageBox.warning(self, "Brak połączenia",
                "Najpierw połącz się z Home Assistant.")
            return
        self._set_status("Pobieranie encji z HA...")
        self.lbl_entity_count.setText("Ładowanie...")
        self._load_thread = _LoadEntitiesThread(self.ha_client)
        self._load_thread.done.connect(self._on_entities_loaded)
        self._load_thread.error.connect(self._on_entities_error)
        self._load_thread.start()

    def _on_entities_loaded(self, entities: list):
        self.entities = entities
        self.entity_states = {e["entity_id"]: e["state"] for e in entities}
        self.entity_list.clear()
        for ent in entities:
            label = ent.get("label", ent["entity_id"])
            item  = QListWidgetItem(f"{label}\n{ent['entity_id']}")
            item.setData(Qt.UserRole, ent)
            item.setToolTip(f"{ent['entity_id']}\nStan: {ent.get('state', '?')}")
            self.entity_list.addItem(item)
        self._set_status(f"✓ Połączono — {len(entities)} encji", ok=True)
        self.lbl_entity_count.setText(f"Łącznie: {len(entities)}")
        # Odśwież podgląd kafelków z nowymi stanami
        for tw in self._tile_widgets:
            tw.update_data(tw.tile_data)

    def _on_entities_error(self, msg: str):
        self._set_status("❌ Błąd połączenia z HA")
        QMessageBox.critical(self, "Błąd pobierania encji", msg)

    def _entity_double_clicked(self, item: QListWidgetItem):
        if self.selected_tile < 0:
            QMessageBox.information(self, "Brak zaznaczonego kafelka",
                "Kliknij najpierw na kafelek na canvasie,\n"
                "a potem dwukliknij encję.")
            return
        self._add_entity_to_tile(self.selected_tile, item.data(Qt.UserRole))

    # ══════════════════════════════════════════════════════════════════════════
    # Canvas
    # ══════════════════════════════════════════════════════════════════════════

    def _toggle_grid(self, show: bool):
        self.canvas.set_show_grid(show)

    def _current_screen(self) -> dict | None:
        screens = self.dashboard.get("screens", [])
        if not screens or self.current_screen >= len(screens):
            return None
        return screens[self.current_screen]

    def _get_tile(self, idx: int) -> dict | None:
        screen = self._current_screen()
        if not screen:
            return None
        tiles = screen.get("tiles", [])
        return tiles[idx] if 0 <= idx < len(tiles) else None

    def _render_canvas(self):
        # Usuń stare kafelki z canvasu
        for tw in self._tile_widgets:
            tw.setParent(None)
            tw.deleteLater()
        self._tile_widgets.clear()

        screens = self.dashboard.get("screens", [])
        screen  = self._current_screen()

        if not screens or not screen:
            self.lbl_screen.setText("— brak ekranów —")
            self.lbl_screen_info.setText("")
            self.canvas.update()
            return

        is_default = self.current_screen == self.dashboard.get("default_screen", 0)
        name = screen.get("name", f"Ekran {self.current_screen + 1}")
        self.lbl_screen.setText(name + ("  ★" if is_default else ""))
        self.lbl_screen_info.setText(
            f"Ekran {self.current_screen + 1}/{len(screens)}"
            f"  •  {len(screen.get('tiles', []))} kafelków")

        for idx, tile in enumerate(screen.get("tiles", [])):
            # Upewnij się że kafelek ma pozycję/rozmiar
            self._ensure_tile_geometry(tile, idx)

            tw = TileWidget(
                tile, idx,
                get_scale  = self.canvas.scale,
                on_click   = self._select_tile,
                on_drop    = self._add_entity_to_tile,
                on_move    = self._on_tile_move,
                on_resize  = self._on_tile_resize,
                get_state  = self._get_entity_state,
                parent     = self.canvas,
            )
            tw.set_selected(idx == self.selected_tile)
            tw.apply_geometry()
            tw.show()
            self._tile_widgets.append(tw)

        self.canvas.update()

    def _ensure_tile_geometry(self, tile: dict, idx: int):
        """Przypisuje domyślną pozycję/rozmiar jeśli brak w danych."""
        if "x" not in tile or tile["x"] < 0:
            col = idx % 2
            row = idx // 2
            tile["x"] = col * (DEFAULT_TILE_W + 16) + 8
            tile["y"] = row * (DEFAULT_TILE_H + 16) + 8
        if not tile.get("w"):
            tile["w"] = DEFAULT_TILE_W
        if not tile.get("h"):
            tile["h"] = DEFAULT_TILE_H

    def _select_tile(self, idx: int):
        self.selected_tile = idx
        for i, tw in enumerate(self._tile_widgets):
            tw.set_selected(i == idx)
        tile = self._get_tile(idx)
        if tile:
            self._show_properties(tile)

    def _on_tile_move(self, tile_idx: int, esp_x: int, esp_y: int):
        """Wywoływane przez TileWidget po każdym ruchu."""
        # Dane już zaktualizowane przez TileWidget bezpośrednio w tile_data
        pass  # można tu dodać snapping do grida lub walidację

    def _on_tile_resize(self, tile_idx: int, esp_w: int, esp_h: int):
        """Wywoływane przez TileWidget po zmianie rozmiaru."""
        pass  # dane zaktualizowane bezpośrednio

    # Domeny HA traktowane automatycznie jako przekaźniki (muszą pasować do is_switch_domain w ha_entities.cpp)
    _SWITCH_DOMAINS = {"switch", "light", "fan", "input_boolean", "automation"}

    def _add_entity_to_tile(self, tile_idx: int, entity: dict):
        tile = self._get_tile(tile_idx)
        if tile is None:
            return
        rows = tile.setdefault("rows", [])
        if len(rows) >= MAX_ROWS:
            QMessageBox.warning(self, "Limit wierszy",
                f"Kafelek może mieć maksymalnie {MAX_ROWS} wierszy.")
            return
        # Auto-wykryj przekaźnik na podstawie domeny encji
        eid = entity["entity_id"]
        domain = eid.split(".")[0] if "." in eid else ""
        auto_type = "switch" if domain in self._SWITCH_DOMAINS else "generic"
        rows.append({
            "entity_id":   eid,
            "label":       entity.get("label", eid),
            "attribute":   "",
            "unit":        "",
            "sensor_type": auto_type,
        })
        if tile_idx < len(self._tile_widgets):
            self._tile_widgets[tile_idx].update_data(tile)
        if self.selected_tile == tile_idx:
            self._show_properties(tile)

    # ══════════════════════════════════════════════════════════════════════════
    # Nawigacja ekranami
    # ══════════════════════════════════════════════════════════════════════════

    def _switch_screen(self, delta: int):
        n = len(self.dashboard.get("screens", []))
        if n == 0:
            return
        self.current_screen = (self.current_screen + delta) % n
        self.selected_tile  = -1
        self._render_canvas()
        self._clear_properties()

    def _add_screen(self):
        screens = self.dashboard.setdefault("screens", [])
        if len(screens) >= MAX_SCREENS:
            QMessageBox.warning(self, "Limit",
                f"Maksymalna liczba ekranów to {MAX_SCREENS}.")
            return
        name, ok = QInputDialog.getText(self, "Nowy ekran", "Nazwa ekranu:")
        if ok and name.strip():
            screens.append({"name": name.strip(), "tiles": []})
            self.current_screen = len(screens) - 1
            self.selected_tile  = -1
            self._render_canvas()
            self._clear_properties()

    def _delete_screen(self):
        screens = self.dashboard.get("screens", [])
        if not screens:
            return
        name = screens[self.current_screen].get("name", "ten ekran")
        if QMessageBox.question(self, "Usuń ekran",
                f"Usunąć ekran '{name}'?\nOperacja jest nieodwracalna.",
                QMessageBox.Yes | QMessageBox.No) != QMessageBox.Yes:
            return
        screens.pop(self.current_screen)
        self.current_screen = min(self.current_screen, max(0, len(screens) - 1))
        if self.dashboard.get("default_screen", 0) >= len(screens):
            self.dashboard["default_screen"] = 0
        self.selected_tile = -1
        self._render_canvas()
        self._clear_properties()

    def _rename_screen(self):
        screen = self._current_screen()
        if not screen:
            return
        name, ok = QInputDialog.getText(
            self, "Zmień nazwę ekranu", "Nowa nazwa:",
            text=screen.get("name", ""))
        if ok and name.strip():
            screen["name"] = name.strip()
            self._render_canvas()

    def _set_default_screen(self):
        if not self.dashboard.get("screens"):
            return
        self.dashboard["default_screen"] = self.current_screen
        self._render_canvas()
        self._set_status(
            f"★ Ekran '{self._current_screen().get('name','')}' ustawiony jako domyślny.",
            ok=True)

    # ══════════════════════════════════════════════════════════════════════════
    # Operacje na kafelkach
    # ══════════════════════════════════════════════════════════════════════════

    def _add_tile(self):
        screen = self._current_screen()
        if screen is None:
            QMessageBox.information(self, "Brak ekranu",
                "Najpierw dodaj ekran przyciskiem '＋ Ekran'.")
            return
        tiles = screen.setdefault("tiles", [])
        if len(tiles) >= MAX_TILES:
            QMessageBox.warning(self, "Limit",
                f"Maksymalna liczba kafelków na ekranie to {MAX_TILES}.")
            return

        idx = len(tiles)
        col = idx % 2
        row = idx // 2
        new_tile = {
            "label":  f"Kafelek {idx + 1}",
            "layout": "vertical",
            "x": col * (DEFAULT_TILE_W + 16) + 8,
            "y": row * (DEFAULT_TILE_H + 16) + 8,
            "w": DEFAULT_TILE_W,
            "h": DEFAULT_TILE_H,
            "rows": [],
        }
        tiles.append(new_tile)
        self._render_canvas()
        self._select_tile(idx)

    def _delete_tile(self):
        if self.selected_tile < 0:
            return
        tile = self._get_tile(self.selected_tile)
        if tile is None:
            return
        name = tile.get("label", "ten kafelek")
        if QMessageBox.question(self, "Usuń kafelek",
                f"Usunąć kafelek '{name}'?",
                QMessageBox.Yes | QMessageBox.No) != QMessageBox.Yes:
            return
        self._current_screen()["tiles"].pop(self.selected_tile)
        self.selected_tile = -1
        self._render_canvas()
        self._clear_properties()

    # ══════════════════════════════════════════════════════════════════════════
    # Panel właściwości
    # ══════════════════════════════════════════════════════════════════════════

    def _clear_properties(self):
        self.tile_name_edit.setEnabled(False)
        self.tile_name_edit.blockSignals(True)
        self.tile_name_edit.clear()
        self.tile_name_edit.blockSignals(False)

        if self._layout_radio_v:
            self._layout_radio_v.setEnabled(False)
            self._layout_radio_h.setEnabled(False)
            self._layout_radio_v.setChecked(True)

        while self.rows_layout.count() > 1:
            item = self.rows_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

    def _show_properties(self, tile: dict):
        self._clear_properties()

        self.tile_name_edit.setEnabled(True)
        self.tile_name_edit.blockSignals(True)
        self.tile_name_edit.setText(tile.get("label", ""))
        self.tile_name_edit.blockSignals(False)

        if self._layout_radio_v:
            self._layout_radio_v.setEnabled(True)
            self._layout_radio_h.setEnabled(True)
            layout_mode = tile.get("layout", "vertical")
            self._layout_group.blockSignals(True)
            self._layout_radio_v.setChecked(layout_mode == "vertical")
            self._layout_radio_h.setChecked(layout_mode == "horizontal")
            self._layout_group.blockSignals(False)

        for row_idx, row in enumerate(tile.get("rows", [])):
            self._add_row_widget(row_idx, row)

    def _add_row_widget(self, row_idx: int, row: dict):
        frame = QFrame()
        frame.setObjectName("row_frame")
        flay = QVBoxLayout(frame)
        flay.setContentsMargins(8, 6, 8, 6)
        flay.setSpacing(4)

        # ── entity_id + przycisk usuń ─────────────────────────────────────────
        top = QHBoxLayout()
        lbl_eid = QLabel(row.get("entity_id", ""))
        lbl_eid.setStyleSheet(
            "font-family: monospace; font-size: 10px; color: #7ecfc5;")
        lbl_eid.setWordWrap(True)
        btn_del = QPushButton("✕")
        btn_del.setFixedSize(22, 22)
        btn_del.setObjectName("btn_danger")
        btn_del.clicked.connect(lambda _=False, r=row_idx: self._delete_row(r))
        top.addWidget(lbl_eid)
        top.addWidget(btn_del)
        flay.addLayout(top)

        # ── Typ encji: Sensor / Przekaźnik ───────────────────────────────────
        is_relay = (row.get("sensor_type") == "switch")

        type_row = QWidget()
        type_row_lay = QHBoxLayout(type_row)
        type_row_lay.setContentsMargins(0, 0, 0, 0)
        type_row_lay.setSpacing(8)

        radio_sensor = QRadioButton("📊  Sensor")
        radio_relay  = QRadioButton("🔌  Przekaźnik")
        radio_sensor.setChecked(not is_relay)
        radio_relay.setChecked(is_relay)

        entity_type_grp = QButtonGroup(type_row)
        entity_type_grp.addButton(radio_sensor, 0)
        entity_type_grp.addButton(radio_relay,  1)

        type_row_lay.addWidget(radio_sensor)
        type_row_lay.addWidget(radio_relay)
        type_row_lay.addStretch()
        flay.addWidget(type_row)

        # ── Etykieta (zawsze widoczna) ────────────────────────────────────────
        lbl_edit = QLineEdit(row.get("label", ""))
        lbl_edit.setPlaceholderText("Etykieta (wyświetlana na ekranie)")
        lbl_edit.textChanged.connect(
            lambda text, r=row_idx: self._on_row_changed(r, "label", text))
        flay.addWidget(lbl_edit)

        # ── Kontrolki sensora (ukrywane gdy przekaźnik) ───────────────────────
        sensor_box = QWidget()
        sensor_lay = QVBoxLayout(sensor_box)
        sensor_lay.setContentsMargins(0, 0, 0, 0)
        sensor_lay.setSpacing(4)

        unit_lay = QHBoxLayout()
        unit_lay.addWidget(QLabel("Jednostka:"))
        unit_edit = QLineEdit(row.get("unit", ""))
        unit_edit.setPlaceholderText("np. °C, %, W")
        unit_edit.setMaximumWidth(90)
        unit_edit.textChanged.connect(
            lambda text, r=row_idx: self._on_row_changed(r, "unit", text))
        unit_lay.addWidget(unit_edit)
        unit_lay.addStretch()
        sensor_lay.addLayout(unit_lay)

        attr_lay = QHBoxLayout()
        attr_lay.addWidget(QLabel("Atrybut HA:"))
        attr_edit = QLineEdit(row.get("attribute", ""))
        attr_edit.setPlaceholderText("zostaw puste = stan główny")
        attr_edit.setToolTip(
            "Zostaw puste aby pokazywać stan główny encji.\n"
            "Wpisz nazwę atrybutu HA np. 'temperature', 'humidity'.")
        attr_edit.textChanged.connect(
            lambda text, r=row_idx: self._on_row_changed(r, "attribute", text))
        attr_lay.addWidget(attr_edit)
        sensor_lay.addLayout(attr_lay)

        _SENSOR_TYPES = [
            ("generic",     "Ogólny (brak ikony)"),
            ("temperature", "Temperatura 🌡  (termometr, kolory dynamiczne)"),
            ("humidity",    "Wilgotność 💧  (kropla, kolory dynamiczne)"),
            ("cpu",         "CPU % (bez ikony)"),
            ("memory",      "Pamięć % (bez ikony)"),
            ("power",       "Moc / Energia ⚡  (błyskawica)"),
            ("illuminance", "Oświetlenie 💡  (żarówka)"),
        ]
        stype_lay = QHBoxLayout()
        stype_lay.addWidget(QLabel("Typ sensora:"))
        type_combo = QComboBox()
        for key, lbl in _SENSOR_TYPES:
            type_combo.addItem(lbl, key)
        current_type = row.get("sensor_type", "generic")
        if current_type == "switch":
            current_type = "generic"
        for i in range(type_combo.count()):
            if type_combo.itemData(i) == current_type:
                type_combo.setCurrentIndex(i)
                break
        type_combo.currentIndexChanged.connect(
            lambda idx, combo=type_combo, r=row_idx:
                self._on_row_changed(r, "sensor_type", combo.itemData(idx)))
        stype_lay.addWidget(type_combo)
        sensor_lay.addLayout(stype_lay)

        flay.addWidget(sensor_box)

        # ── Podgląd przycisku przekaźnika (widoczny gdy przekaźnik) ──────────
        relay_box = QFrame()
        relay_box.setObjectName("relay_preview")
        relay_box.setStyleSheet("""
            #relay_preview {
                background: #1E2A35;
                border-radius: 10px;
                border: 1px solid #2A3A4A;
                min-height: 52px;
                max-height: 52px;
            }
        """)
        rb_lay = QHBoxLayout(relay_box)
        rb_lay.setContentsMargins(12, 4, 12, 4)
        rb_lay.setSpacing(10)

        circle_lbl = QLabel("●")
        circle_lbl.setStyleSheet(
            "font-size: 18px; color: #3A5060; background: transparent; "
            "border: none;")
        circle_lbl.setFixedWidth(22)

        btn_text_lbl = QLabel("— brak danych —")
        btn_text_lbl.setStyleSheet(
            "color: #607080; font-size: 12px; background: transparent; "
            "border: none;")
        btn_text_lbl.setAlignment(Qt.AlignCenter)

        rb_lay.addWidget(circle_lbl)
        rb_lay.addWidget(btn_text_lbl, 1)

        note_lbl = QLabel("Kliknięcie na ESP32 przełączy stan")
        note_lbl.setStyleSheet("color: #405060; font-size: 9px;")
        note_lbl.setAlignment(Qt.AlignCenter)

        relay_container = QWidget()
        rc_lay = QVBoxLayout(relay_container)
        rc_lay.setContentsMargins(0, 0, 0, 0)
        rc_lay.setSpacing(2)
        rc_lay.addWidget(relay_box)
        rc_lay.addWidget(note_lbl)

        flay.addWidget(relay_container)

        # Ustaw widoczność na starcie
        sensor_box.setVisible(not is_relay)
        relay_container.setVisible(is_relay)

        # ── Reagowanie na zmianę typu ─────────────────────────────────────────
        def _on_entity_type_changed(btn_id: int):
            sw = (btn_id == 1)
            sensor_box.setVisible(not sw)
            relay_container.setVisible(sw)
            self._on_row_changed(
                row_idx, "sensor_type",
                "switch" if sw else type_combo.itemData(type_combo.currentIndex()))

        entity_type_grp.idClicked.connect(_on_entity_type_changed)

        self.rows_layout.insertWidget(self.rows_layout.count() - 1, frame)

    def _on_tile_name_changed(self, text: str):
        tile = self._get_tile(self.selected_tile)
        if tile is not None:
            tile["label"] = text
            if self.selected_tile < len(self._tile_widgets):
                self._tile_widgets[self.selected_tile].update_data(tile)

    def _on_layout_changed(self, btn_id: int):
        tile = self._get_tile(self.selected_tile)
        if tile is not None:
            tile["layout"] = "vertical" if btn_id == 0 else "horizontal"
            if self.selected_tile < len(self._tile_widgets):
                self._tile_widgets[self.selected_tile].update_data(tile)

    def _on_row_changed(self, row_idx: int, field: str, value: str):
        tile = self._get_tile(self.selected_tile)
        if tile and row_idx < len(tile.get("rows", [])):
            tile["rows"][row_idx][field] = value

    def _delete_row(self, row_idx: int):
        tile = self._get_tile(self.selected_tile)
        if tile and row_idx < len(tile.get("rows", [])):
            tile["rows"].pop(row_idx)
            if self.selected_tile < len(self._tile_widgets):
                self._tile_widgets[self.selected_tile].update_data(tile)
            self._show_properties(tile)

    # ══════════════════════════════════════════════════════════════════════════
    # Dialog połączenia z HA
    # ══════════════════════════════════════════════════════════════════════════

    def _show_connect_dialog(self):
        dlg = QDialog(self)
        dlg.setWindowTitle("Połączenie z Home Assistant")
        dlg.setMinimumWidth(500)
        lay = QVBoxLayout(dlg)
        lay.setContentsMargins(20, 20, 20, 20)
        lay.setSpacing(12)

        lay.addWidget(QLabel("<b>Połącz edytor z Home Assistant</b>"))
        lay.addWidget(QLabel(
            "Po połączeniu będziesz mógł pobierać encje HA i widzieć ich aktualne stany."))

        form = QWidget()
        fl = QFormLayout(form)
        fl.setSpacing(8)

        url_edit = QLineEdit(self.config.ha_url)
        url_edit.setPlaceholderText("ws://192.168.1.100:8123/api/websocket")
        token_edit = QLineEdit(self.config.ha_token)
        token_edit.setEchoMode(QLineEdit.Password)
        token_edit.setPlaceholderText("eyJ0eXAiOiJKV1Qi...")

        ip_edit = QLineEdit(self.config.panel_ip)
        ip_edit.setPlaceholderText("np. 192.168.1.143")

        fl.addRow("WebSocket URL:", url_edit)
        fl.addRow("Token dostępu:", token_edit)
        fl.addRow("IP panelu ESP:", ip_edit)
        lay.addWidget(form)

        hint = QLabel(
            "URL: HA → Ustawienia → System → Sieć\n"
            "Token: Profil użytkownika → Tokeny dostępu → Utwórz token\n"
            "IP panelu: adres ESP32-P4 w sieci lokalnej")
        hint.setStyleSheet("color: #607080; font-size: 11px;")
        lay.addWidget(hint)

        status_lbl = QLabel("")
        status_lbl.setWordWrap(True)
        lay.addWidget(status_lbl)

        btns = QHBoxLayout()
        btn_test   = QPushButton("🔌  Testuj połączenie")
        btn_save   = QPushButton("✓  Zapisz i połącz")
        btn_save.setObjectName("btn_primary")
        btn_cancel = QPushButton("Anuluj")

        btns.addWidget(btn_test)
        btns.addStretch()
        btns.addWidget(btn_cancel)
        btns.addWidget(btn_save)
        lay.addLayout(btns)

        def do_test():
            url   = url_edit.text().strip()
            token = token_edit.text().strip()
            if not url or not token:
                status_lbl.setText("❌ Podaj URL i token.")
                return
            status_lbl.setText("Testuję...")
            dlg.repaint()
            ok, msg = HaClient(url, token).test_connection()
            status_lbl.setText(("✓  " if ok else "❌  ") + msg)

        def do_save():
            url   = url_edit.text().strip()
            token = token_edit.text().strip()
            if not url or not token:
                status_lbl.setText("❌ Podaj URL i token.")
                return
            self.config.ha_url   = url
            self.config.ha_token = token
            self.config.panel_ip = ip_edit.text().strip()
            self.config.save()
            self.ha_client = HaClient(url, token)
            self.btn_connect.setText("⚡  Połączono")
            self._set_status("✓ Połączono z HA — kliknij 'Pobierz encje z HA'", ok=True)
            dlg.accept()

        btn_test.clicked.connect(do_test)
        btn_save.clicked.connect(do_save)
        btn_cancel.clicked.connect(dlg.reject)
        dlg.exec()

    # ══════════════════════════════════════════════════════════════════════════
    # Import / Export JSON
    # ══════════════════════════════════════════════════════════════════════════

    def _import_json(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Importuj konfigurację dashboardu", "", "Pliki JSON (*.json)")
        if not path:
            return
        try:
            data = json.loads(Path(path).read_text(encoding="utf-8"))
            if "screens" not in data or not isinstance(data["screens"], list):
                raise ValueError("Nieprawidłowy format — brak pola 'screens'.")
            self.dashboard      = data
            self.current_screen = 0
            self.selected_tile  = -1
            self._render_canvas()
            self._clear_properties()
            n = len(data["screens"])
            QMessageBox.information(self, "Import udany",
                f"Wczytano konfigurację:\n• {n} ekranów\n\nŹródło: {path}")
        except Exception as e:
            QMessageBox.critical(self, "Błąd importu", str(e))

    def _export_json(self):
        screens = self.dashboard.get("screens", [])
        if not screens:
            QMessageBox.warning(self, "Brak danych",
                "Nie masz jeszcze żadnych ekranów do eksportu.")
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Exportuj konfigurację dashboardu", "ha-panel.json",
            "Pliki JSON (*.json)")
        if not path:
            return
        try:
            out = json.dumps(self.dashboard, ensure_ascii=False, indent=2)
            Path(path).write_text(out, encoding="utf-8")
            tile_count = sum(len(s.get("tiles", [])) for s in screens)
            QMessageBox.information(self, "Export udany",
                f"Zapisano konfigurację:\n"
                f"• {len(screens)} ekranów\n"
                f"• {tile_count} kafelków łącznie\n\n"
                f"Plik: {path}\n\n"
                f"Użyj 'Wyślij na panel' aby wgrać bezpośrednio na urządzenie.")
        except Exception as e:
            QMessageBox.critical(self, "Błąd eksportu", str(e))

    # ══════════════════════════════════════════════════════════════════════════
    # Komunikacja z panelem ESP32-P4
    # ══════════════════════════════════════════════════════════════════════════

    def _get_panel_ip(self) -> str | None:
        ip = self.config.panel_ip.strip()
        if not ip:
            ip, ok = QInputDialog.getText(
                self, "IP panelu", "Podaj adres IP ESP32-P4:",
                text="192.168.1.")
            if not ok or not ip.strip():
                return None
            self.config.panel_ip = ip.strip()
            self.config.save()
        return self.config.panel_ip

    def _send_to_panel(self):
        screens = self.dashboard.get("screens", [])
        if not screens:
            QMessageBox.warning(self, "Brak danych",
                "Nie masz jeszcze żadnych ekranów do wysłania.")
            return
        ip = self._get_panel_ip()
        if not ip:
            return
        self._set_status(f"Wysyłanie do {ip}...")
        self._panel_thread = _PanelThread(ip, payload=self.dashboard)
        self._panel_thread.ok.connect(self._on_panel_send_ok)
        self._panel_thread.error.connect(self._on_panel_error)
        self._panel_thread.start()

    def _fetch_from_panel(self):
        ip = self._get_panel_ip()
        if not ip:
            return
        self._set_status(f"Pobieranie z {ip}...")
        self._panel_thread = _PanelThread(ip, payload=None)
        self._panel_thread.done.connect(self._on_panel_fetch_done)
        self._panel_thread.error.connect(self._on_panel_error)
        self._panel_thread.start()

    def _on_panel_send_ok(self, msg: str):
        self._set_status("✓ Dashboard wysłany na panel — restart w toku", ok=True)
        QMessageBox.information(self, "Sukces", f"Dashboard wysłany.\n\n{msg}")

    def _on_panel_fetch_done(self, data: dict):
        if "screens" not in data or not isinstance(data["screens"], list):
            QMessageBox.critical(self, "Błąd", "Otrzymano nieprawidłowy JSON.")
            return
        self.dashboard      = data
        self.current_screen = 0
        self.selected_tile  = -1
        self._render_canvas()
        self._clear_properties()
        n = len(data["screens"])
        self._set_status(f"✓ Pobrano z panelu — {n} ekranów", ok=True)
        QMessageBox.information(self, "Pobrano z panelu",
            f"Wczytano konfigurację z urządzenia:\n• {n} ekranów")

    def _on_panel_error(self, msg: str):
        self._set_status("❌ Błąd komunikacji z panelem")
        QMessageBox.critical(self, "Błąd połączenia z panelem",
            f"Nie udało się połączyć z ESP32-P4.\n\n{msg}\n\n"
            "Sprawdź:\n• IP w ustawieniach\n• Czy panel jest w sieci\n• Czy WiFi działa")


# ─── Start ────────────────────────────────────────────────────────────────────
def main():
    app = QApplication(sys.argv)
    app.setApplicationName("HA Panel Editor")
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
