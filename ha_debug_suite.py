import os
import json
import network
import urequests
import time
import gc

try:
    from wifi_secrets import WIFI_SSID, WIFI_PASSWORD as WIFI_PASS
except ImportError:
    WIFI_SSID = "YOUR_WIFI_SSID"
    WIFI_PASS = "YOUR_WIFI_PASSWORD"


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
ADHAN_URL = "https://www.youtube.com/watch?v=eVUZV_2auTc" 

def connect():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    wlan.connect(WIFI_SSID, WIFI_PASS)
    for _ in range(20):
        if wlan.isconnected(): return True
        time.sleep(1)
    return False

def ha_call(service, data):
    url = f"http://{HA_IP}:8123/api/services/media_player/{service}"
    headers = {"Authorization": "Bearer " + HA_TOKEN, "Content-Type": "application/json"}
    try:
        print(f"Calling {service}...")
        r = urequests.post(url, json=data, headers=headers, timeout=10)
        print(f"Status: {r.status_code}")
        r.close()
        return True
    except Exception as e:
        print(f"Error calling {service}: {e}")
        return False

def test_suite():
    if not connect():
        print("WiFi Fail")
        return

    # 1. Set Volume
    ha_call("volume_set", {"entity_id": HA_ENTITY_ID, "volume_level": 0.7})
    time.sleep(1)

    # 2. Try 'url' type
    print("Testing type: url")
    ha_call("play_media", {
        "entity_id": HA_ENTITY_ID,
        "media_content_id": ADHAN_URL,
        "media_content_type": "url"
    })
    time.sleep(5)

    # 3. Try 'video' type
    print("Testing type: video")
    ha_call("play_media", {
        "entity_id": HA_ENTITY_ID,
        "media_content_id": ADHAN_URL,
        "media_content_type": "video"
    })
    time.sleep(5)

    # 4. Try 'music' type
    print("Testing type: music")
    ha_call("play_media", {
        "entity_id": HA_ENTITY_ID,
        "media_content_id": ADHAN_URL,
        "media_content_type": "music"
    })

if __name__ == "__main__":
    test_suite()