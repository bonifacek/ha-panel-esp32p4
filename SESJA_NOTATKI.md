# ESP32-P4 HA Panel — stan projektu (27.05.2026)

## Sprzęt
- **Płytka**: ESP32-P4 Function EV Board
- **WiFi**: ESP32-C6 jako co-procesor SPI/RPC (ESP-Hosted FW)
- **Wyświetlacz**: 1024×600, LVGL 9.1, orientacja 270°
- **Bateria**: GPIO 52, dzielnik 68kΩ+100kΩ, ADC oneshot
- **Framework**: ESP-IDF 5.4, język C++

## Architektura — pliki w `main/`

| Plik | Rola |
|---|---|
| `main.cpp` | app_main, network_task (WiFi+NTP+HA), battery_task |
| `ha_config.h/cpp` | Zapis/odczyt URL+token z NVS (namespace `supla_cfg`) |
| `ha_entities.h/cpp` | Model danych: HaScreen → HaTile → HaRow; NVS klucz `ha_dashboard` |
| `ha_service.h/cpp` | WebSocket HA API: auth, subscribe_entities, dispatch stanów |
| `panel_ui.h/cpp` | LVGL UI: status bar + lv_tabview z ekranami i kafelkami |
| `board_display.h/cpp` | Inicjalizacja BSP/LVGL, lock/unlock mutex, backlight (60 s) |
| `battery_monitor.h/cpp` | ADC GPIO52, Li-Ion krzywa 3300–4200 mV, odczyt co 30 s |
| `web_config.h/cpp` | HTTP serwer: /api/dashboard (GET/POST), /api/entities, zapis HA config |
| `polish_fonts.h` | Deklaracje fontów: polish_18/20/22/24 |

## Co działa (zaimplementowane i przetestowane)

### WebSocket / HA
- Autoryzacja: `auth_required → auth → auth_ok → subscribe_entities`
- **Kluczowa poprawka RPC WiFi**: `ping_interval_sec=2` wymusza SPI transfer co 2 s
  → `auth_required` dociera przed 10-sekundowym timeoutem HA
- Opcode filter: ignoruje ping(0x09)/pong(0x0A)/close(0x08)
- Bufor fragmentowanych wiadomości WS: 32 KB w SPIRAM
- Parametry WS: `reconnect=3000ms`, `network_timeout=1000ms`, `pingpong_timeout=8s`, `buffer=16384`

### Web GUI
- Wszystkie przyciski działają (byłby globalny błąd JS przez split stringów C — naprawiony)
- Zapis HA config → auto-restart (restart po 800 ms)
- Zapis dashboard → auto-restart
- Eksport JSON (GET /api/dashboard → plik .json)
- Import JSON (POST /api/dashboard, walidacja pola `screens`)
- Wyszukiwarka encji HA (GET /api/entities?filter=...)

### UI (LVGL)
- Status bar (64px): `[godzina+data NTP] [Home Assistant panel] [Wi-Fi|HA|BAT]`
- Trzy równe sekcje (flex_grow=1 każda)
- Zegar NTP: aktualizacja co 1 s, format `"HH:MM  DD.MM.YYYY"`, timezone Polska
- lv_tabview: zakładka = ekran, siatka kafelków 2-kolumnowa
- Kafelek: nagłówek + wiersze encji + przycisk switch (EntityKind::Switch)
- Online/offline kolorowanie kart

### NTP
- Strefa: `CET-1CEST,M3.5.0,M10.5.0/3` (Polska)
- Serwer: `pool.ntp.org`
- Start: po nawiązaniu WiFi w `network_task`
- Przed sync: wyświetla `--:--`

### Konfiguracja encji (reminder dla siebie)
- Encja bez atrybutu → pobiera pole `state` (np. `on`/`off`, temperatura)
- Encja z atrybutem → np. `temperature`, `humidity`, `wind_speed`
- Encja pogodowa `weather.xxx`: osobne wiersze dla każdego atrybutu
- `EntityKind::Switch` → pokazuje przycisk "Przełącz"

## Znane ograniczenia / do zrobienia

- [ ] Procent baterii: krzywa liniowa bez kalibracji ADC — może być niedokładny
- [ ] Maksymalnie 128 unikalnych encji w subskrypcji (można zwiększyć `visited[128]`)
- [ ] Maksymalne rozmiary: `kHaMaxScreens`, `kHaMaxTiles`, `kHaMaxRows` — patrz `ha_entities.h`
- [ ] Brak obsługi `wss://` (TLS) — wymaga certyfikatu serwera HA

## Jak wznowić sesję jutro

1. Otwórz projekt `H:\!_projekty\cloude_p4` w Claude Code (FleetView)
2. Poprzednia sesja jest zapisana — można kontynuować wątek lub zacząć nowy
3. Przy nowym wątku: wklej ten plik jako kontekst LUB powiedz "kontynuujemy pracę nad HA Panelem"
4. Pliki źródłowe są aktualne — wystarczy `idf.py build` żeby zbudować

## Kluczowe stałe / NVS

```
Namespace NVS:  "supla_cfg"
Klucze:         "ha_url", "ha_token", "ha_dashboard"
HA URL format:  "ws://192.168.x.x:8123/api/websocket"
Token:          Long-Lived Access Token z HA (Profil → Tokeny)
```
