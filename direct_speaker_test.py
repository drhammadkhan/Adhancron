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


# Home Assistant Details
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

def run_test():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        print("Connecting to WiFi...")
        wlan.connect(WIFI_SSID, WIFI_PASS)
        while not wlan.isconnected():
            time.sleep(1)
    
    print("WiFi Connected. Sending Adhan trigger to Home Assistant...")
    gc.collect()
    
    headers = {
        "Authorization": "Bearer " + HA_TOKEN,
        "Content-Type": "application/json"
    }
    data = {
        "entity_id": HA_ENTITY_ID,
        "media_content_id": ADHAN_URL,
        "media_content_type": "music"
    }
    
    try:
        r = urequests.post(HA_URL, json=data, headers=headers, timeout=15)
        print("Status Code:", r.status_code)
        print("Response:", r.text)
        r.close()
    except Exception as e:
        print("Error during POST:", e)

if __name__ == "__main__":
    run_test()