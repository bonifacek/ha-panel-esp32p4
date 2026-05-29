"""
Komunikacja z Home Assistant przez REST API.
URL wejściowy to WebSocket URL (ws:// lub wss://) — konwertujemy na http/https.
"""
import requests


class HaClient:
    def __init__(self, ws_url: str, token: str):
        # Konwertuj ws:// → http://, wss:// → https://
        if ws_url.startswith("wss://"):
            base = "https://" + ws_url[6:]
        elif ws_url.startswith("ws://"):
            base = "http://" + ws_url[5:]
        else:
            base = ws_url

        # Usuń suffix /api/websocket
        if "/api/websocket" in base:
            base = base[: base.index("/api/websocket")]

        self.base_url = base.rstrip("/")
        self.headers = {
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        }

    def test_connection(self) -> tuple[bool, str]:
        """Sprawdza czy HA odpowiada. Zwraca (sukces, komunikat)."""
        try:
            r = requests.get(
                f"{self.base_url}/api/", headers=self.headers, timeout=6
            )
            if r.status_code == 200:
                return True, "Połączono pomyślnie!"
            elif r.status_code == 401:
                return False, "Błąd 401 — nieprawidłowy token."
            else:
                return False, f"HA odpowiedział kodem HTTP {r.status_code}."
        except requests.exceptions.ConnectionError:
            return False, "Nie można połączyć się z HA — sprawdź adres IP i port."
        except requests.exceptions.Timeout:
            return False, "Przekroczono czas oczekiwania (timeout)."
        except Exception as e:
            return False, f"Błąd: {e}"

    def get_entities(self) -> list[dict]:
        """
        Pobiera wszystkie encje z /api/states.
        Zwraca listę słowników: entity_id, label, domain, state.
        """
        r = requests.get(
            f"{self.base_url}/api/states", headers=self.headers, timeout=20
        )
        r.raise_for_status()

        entities = []
        for state in r.json():
            eid = state.get("entity_id", "")
            domain = eid.split(".")[0] if "." in eid else ""
            label = (
                state.get("attributes", {}).get("friendly_name", "")
                or eid
            )
            entities.append(
                {
                    "entity_id": eid,
                    "label": label,
                    "domain": domain,
                    "state": str(state.get("state", "")),
                }
            )

        entities.sort(key=lambda e: e["entity_id"])
        return entities
