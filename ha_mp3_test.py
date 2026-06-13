import os
import json
import network
import urequests
import time

HA_IP = "homeassistant.local"
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
MP3_URL = "https://www.islamcan.com/audio/adhan/azan1.mp3"

def ha_call(data):
    url = f"http://{HA_IP}:8123/api/services/media_player/play_media"
    headers = {"Authorization": "Bearer " + HA_TOKEN, "Content-Type": "application/json"}
    r = urequests.post(url, json=data, headers=headers, timeout=10)
    print(f"Status: {r.status_code}")
    r.close()

print("Testing with direct MP3 link...")
ha_call({
    "entity_id": HA_ENTITY_ID,
    "media_content_id": MP3_URL,
    "media_content_type": "music"
})
