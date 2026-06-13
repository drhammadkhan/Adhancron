import os
import json
import urequests
HA_URL = "http://homeassistant.local:8123/api/services/media_player/play_media"
def _load_ha_token():
    token = os.getenv("HA_TOKEN")
    if token:
        return token
    try:
        with open(os.path.expanduser("~/.config/home-assistant/config.json"), "r", encoding="utf-8") as f:
            return json.load(f)["token"]
    except Exception as exc:
        raise RuntimeError("Set HA_TOKEN or configure ~/.config/home-assistant/config.json") from exc

HA_TOKEN = _load_ha_token()

HA_ENTITY_ID = "media_player.bedroom_speaker"
ADHAN_URL = "https://www.youtube.com/watch?v=eVUZV_2auTc"

headers = {
    "Authorization": "Bearer " + HA_TOKEN,
    "Content-Type": "application/json"
}
data = {
    "entity_id": HA_ENTITY_ID,
    "media_content_id": ADHAN_URL,
    "media_content_type": "url"
}
print("Testing HA trigger...")
try:
    response = urequests.post(HA_URL, json=data, headers=headers)
    print("Status:", response.status_code)
    print("Response:", response.text)
    response.close()
except Exception as e:
    print("Error:", e)
