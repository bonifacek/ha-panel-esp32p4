# Guition SUPLA MQTT Panel for ESP32-P4

Firmware startowy dla panelu Guition `JC8012P4A1C_I_W` / ESP32-P4 z ekranem dotykowym.
Projekt bazuje na przykladach producenta oraz BSP z `common_components`.

## Funkcje

- inicjalizacja LCD i dotyku przez BSP producenta,
- Wi-Fi przez ESP32-C6 / `esp_wifi_remote` / ESP-Hosted,
- MQTT przez `esp-mqtt`,
- kafelki LVGL dla czujnikow i przelacznikow,
- publikowanie komend ON/OFF na topic-i SUPLA,
- konfiguracja Wi-Fi i MQTT w `idf.py menuconfig`.

## Konfiguracja

1. Ustaw target:

   ```bat
   idf.py set-target esp32p4
   ```

2. Wejdz w menu:

   ```bat
   idf.py menuconfig
   ```

3. Ustaw:

   - `SUPLA MQTT Panel -> Wi-Fi SSID`
   - `SUPLA MQTT Panel -> Wi-Fi password`
   - `SUPLA MQTT Panel -> MQTT broker URI`
   - `SUPLA MQTT Panel -> MQTT username/password`

Jesli SSID zostawisz puste, firmware uruchomi AP `SUPLA-PANEL-*` z captive portalem pod `http://192.168.4.1`.

## Encje SUPLA

Encje sa zdefiniowane w `main/supla_entities.h`. Dla kazdej pozycji ustaw:

- `label` - nazwa widoczna na ekranie,
- `kind` - `Sensor` albo `Switch`,
- `state_topic` - topic, z ktorego panel czyta stan,
- `command_topic` - topic komendy dla przelacznikow,
- `on_payload` / `off_payload` - payload wysylany po dotknieciu kafelka.

Przelaczniki SUPLA uzywaja topicu:

```text
supla/.../devices/<device_id>/channels/<channel_id>/execute_action
```

oraz payloadow:

```text
TURN_ON
TURN_OFF
```

## Budowanie

```bat
idf.py build
idf.py -p COMx flash monitor
```
