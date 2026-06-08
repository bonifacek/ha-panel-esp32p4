# ESP32-P4 Home Assistant Panel

Firmware for the **Guition JC8012P4A1C** touchscreen panel (ESP32-P4, 1280×800 display)
with **Home Assistant** integration via WebSocket API. UI built on LVGL 9.x with entity tiles.

## Hardware

| Component | Description |
|-----------|-------------|
| MCU | ESP32-P4 (dual-core RISC-V 400 MHz, 32 MB PSRAM) |
| Wi-Fi / BLE | ESP32-C6 (slave module, ESP-Hosted SDIO) |
| Display | 1280×800 MIPI DSI, JD9365 driver |
| Touch | GT911 (I2C) |
| Audio | ES8311 (optional, TTS from HA) |
| BSP | Guition `common_components/bsp` |

## Features

- **LVGL Tiles** — configured via GUI, automatic layout or manual positioning
- **HA Entities** — sensors (temperature, humidity, power, CPU, illuminance), switches (light, fan, switch, input_boolean), presence (person, device_tracker), devices (device — colored dot)
- **Arc Mode** — sensors displayed as a 270° circular gauge with dynamic color
- **Touchable Switches** — icon + label change color based on ON/OFF state, no separate button
- **AP Configuration** — when Wi-Fi is not set up, starts an access point `HA-PANEL-*`, portal at `http://192.168.4.1`
- **Dashboard via HA Addon** — tile and entity editor in the browser, saved to SPIFFS on the device
- **C6 OTA** — optional Wi-Fi module firmware update (disabled by default)

## Repository Structure

```
├── main/               # ESP32-P4 firmware
│   ├── ha_entities.*   # Entity definitions and dashboard JSON parsing
│   ├── ha_service.*    # HA WebSocket client (esp-websocket-client)
│   ├── ha_config.*     # Configuration (URL, token) from NVS + web
│   ├── panel_ui.*      # LVGL UI — tiles, rows, animations
│   ├── web_config.*    # HTTP config server for Wi-Fi / HA
│   └── main.cpp        # Entry point, initialization
├── ha_addon/           # Home Assistant Addon (Python + web)
│   ├── app.py          # Flask server — HA proxy + device write
│   ├── config.yaml     # HA addon manifest
│   └── static/         # Addon UI (app.js, style.css)
├── components/         # Custom components (fonts, icons)
├── common_components/  # Vendor BSP (display, touch, audio)
├── CMakeLists.txt
├── sdkconfig.defaults
└── idf_component.yml   # Dependencies (LVGL, esp-websocket-client, cJSON…)
```

## Requirements

- **ESP-IDF v5.4** — `C:\Espressif\frameworks\esp-idf-v5.4`
- Python 3.11 (bundled with ESP-IDF)
- `idf.py` tool

## Building and Flashing

```powershell
# Activate IDF environment (Windows PowerShell)
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.4"
$env:PYTHONUTF8 = "1"
# Run activate.py or export.ps1 from ESP-IDF

idf.py set-target esp32p4
idf.py build
idf.py -p COMXX flash
```

On first boot the panel starts AP `HA-PANEL-<MAC>`.
Connect to it and configure Wi-Fi and HA address/token at `http://192.168.4.1`.

## HA Addon — Dashboard Editor

The addon is located in the `ha_addon/` directory. Install as a local addon in Home Assistant:

1. Copy `ha_addon/` to `<config>/addons/esp32p4_panel/`
2. In HA: **Settings → Add-ons → Add-on Store → ⋮ → Check for updates**
3. Install "ESP32-P4 Panel" and start it
4. In the addon options set `device_ip` — the panel's IP address on the local network

## Entity Configuration

Tiles and entities are configured via the HA addon or directly via API:
`PUT http://<panel_ip>/api/dashboard` with JSON.

Row fields (`HaRow`):

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | string | HA entity ID (e.g. `sensor.temperatura`) |
| `label` | string | Label displayed on the panel |
| `attribute` | string | Optional attribute (e.g. `current_temperature`) |
| `unit` | string | Unit (e.g. `°C`, `%`, `W`) |
| `sensor_type` | string | `temperature`, `humidity`, `cpu`, `memory`, `power`, `illuminance`, `switch`, `presence`, `device` |
| `display_mode` | string | `text` (default) or `arc` (circular gauge) |

## License

Private project / personal use. BSP code originates from Guition vendor examples.

---

Firmware dla panelu dotykowego **Guition JC8012P4A1C** (ESP32-P4, ekran 1280×800) z integracją
**Home Assistant** przez WebSocket API. Interfejs oparty na LVGL 9.x z kafelkami encji.

## Sprzęt

| Komponent | Opis |
|-----------|------|
| MCU | ESP32-P4 (dual-core RISC-V 400 MHz, 32 MB PSRAM) |
| Wi-Fi / BLE | ESP32-C6 (moduł slave, ESP-Hosted SDIO) |
| Wyświetlacz | 1280×800 MIPI DSI, sterownik JD9365 |
| Dotyk | GT911 (I2C) |
| Dźwięk | ES8311 (opcjonalnie, TTS z HA) |
| BSP | Guition `common_components/bsp` |

## Funkcje

- **Kafelki LVGL** — konfigurowane przez GUI, automatyczny układ lub ręczne pozycjonowanie
- **Encje HA** — sensory (temperatura, wilgotność, moc, CPU, oświetlenie), przełączniki (light, fan, switch, input_boolean), obecność (person, device_tracker), urządzenia (device — kolorowa kropka)
- **Tryb Arc** — sensory wyświetlane jako wskaźnik kołowy 270° z dynamicznym kolorem
- **Przełączniki dotykowe** — ikonka + etykieta zmieniają kolor na stan ON/OFF bez osobnego przycisku
- **Konfiguracja przez AP** — przy braku Wi-Fi uruchamia punkt dostępowy `HA-PANEL-*`, portal pod `http://192.168.4.1`
- **Dashboard przez HA Addon** — edytor kafelków i encji w przeglądarce, zapis do SPIFFS na urządzeniu
- **OTA C6** — opcjonalna aktualizacja firmware modułu Wi-Fi (domyślnie wyłączona)

## Struktura repozytorium

```
├── main/               # Firmware ESP32-P4
│   ├── ha_entities.*   # Definicje encji i parsowanie JSON dashboardu
│   ├── ha_service.*    # Klient WebSocket HA (esp-websocket-client)
│   ├── ha_config.*     # Konfiguracja (URL, token) z NVS + web
│   ├── panel_ui.*      # UI LVGL — kafelki, wiersze, animacje
│   ├── web_config.*    # HTTP serwer konfiguracji Wi-Fi / HA
│   └── main.cpp        # Punkt wejścia, inicjalizacja
├── ha_addon/           # Home Assistant Addon (Python + web)
│   ├── app.py          # Serwer Flask — proxy do HA + zapis na urządzenie
│   ├── config.yaml     # Manifest addonu HA
│   └── static/         # UI addonu (app.js, style.css)
├── components/         # Komponenty własne (fonts, ikony)
├── common_components/  # BSP producenta (display, touch, audio)
├── CMakeLists.txt
├── sdkconfig.defaults
└── idf_component.yml   # Zależności (LVGL, esp-websocket-client, cJSON…)
```

## Wymagania

- **ESP-IDF v5.4** — `C:\Espressif\frameworks\esp-idf-v5.4`
- Python 3.11 (dołączony z ESP-IDF)
- Narzędzie `idf.py`

## Budowanie i flashowanie

```powershell
# Aktywacja środowiska IDF (Windows PowerShell)
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.4"
$env:PYTHONUTF8 = "1"
# Uruchom activate.py lub skrypt export.ps1 z ESP-IDF


idf.py set-target esp32p4
idf.py build
idf.py -p COMXX flash
```

Po pierwszym uruchomieniu panel uruchomi AP `HA-PANEL-<MAC>`.
Połącz się z nim i skonfiguruj Wi-Fi oraz adres/token HA pod `http://192.168.4.1`.

## HA Addon — edytor dashboardu

Addon dostępny w katalogu `ha_addon/`. Instalacja jako lokalny addon w Home Assistant:

1. Skopiuj katalog `ha_addon/` do `<config>/addons/esp32p4_panel/`
2. W HA: **Ustawienia → Dodatki → Sklep z dodatkami → ⋮ → Sprawdź aktualizacje**
3. Zainstaluj „ESP32-P4 Panel" i uruchom
4. W opcjach addonu ustaw `device_ip` — adres IP panelu w sieci lokalnej

## Konfiguracja encji

Kafelki i encje konfiguruje się przez addon HA lub bezpośrednio przez API:
`PUT http://<panel_ip>/api/dashboard` z JSON-em.

Pola wiersza (`HaRow`):

| Pole | Typ | Opis |
|------|-----|------|
| `entity_id` | string | ID encji w HA (np. `sensor.temperatura`) |
| `label` | string | Etykieta wyświetlana na panelu |
| `attribute` | string | Opcjonalny atrybut (np. `current_temperature`) |
| `unit` | string | Jednostka (np. `°C`, `%`, `W`) |
| `sensor_type` | string | `temperature`, `humidity`, `cpu`, `memory`, `power`, `illuminance`, `switch`, `presence`, `device` |
| `display_mode` | string | `text` (domyślnie) lub `arc` (wskaźnik kołowy) |

## Licencja

Projekt prywatny / do użytku własnego. Kod BSP pochodzi z przykładów producenta Guition.
