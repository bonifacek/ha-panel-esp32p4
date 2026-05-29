"""
Przechowuje ustawienia aplikacji (adres HA, token) w pliku ustawienia.json.
"""
import json
from pathlib import Path

SETTINGS_FILE = Path(__file__).parent / "ustawienia.json"


class AppConfig:
    def __init__(self):
        self.ha_url = ""
        self.ha_token = ""
        self._load()

    def _load(self):
        try:
            data = json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
            self.ha_url = data.get("ha_url", "")
            self.ha_token = data.get("ha_token", "")
        except Exception:
            pass  # plik nie istnieje przy pierwszym uruchomieniu

    def save(self):
        SETTINGS_FILE.write_text(
            json.dumps({"ha_url": self.ha_url, "ha_token": self.ha_token}, indent=2),
            encoding="utf-8",
        )

    def is_ready(self) -> bool:
        return bool(self.ha_url.strip() and self.ha_token.strip())
