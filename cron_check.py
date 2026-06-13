import json
import time
import os
import requests
from datetime import datetime

# --- CONFIGURATION ---
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
ADHAN_URL = "http://YOUR_DOCKER_HOST_IP:8000/audio/adhan_final.mp3"
TAKBEER_URL = "http://YOUR_DOCKER_HOST_IP:8000/audio/takbeer.mp3"
SCHEDULE_FILE = "/app/schedule.json"
STATE_FILE = "/app/cron_state.json"
EID_DATE = "2026-03-20"

def trigger_audio(url, volume=0.8):
    headers = {"Authorization": f"Bearer {HA_TOKEN}", "Content-Type": "application/json"}
    # Set volume
    requests.post(HA_URL.replace("play_media", "volume_set"), 
                  json={"entity_id": HA_ENTITY_ID, "volume_level": volume}, 
                  headers=headers, timeout=5)
    # Play media
    requests.post(HA_URL, 
                  json={"entity_id": HA_ENTITY_ID, "media_content_id": url, "media_content_type": "music"}, 
                  headers=headers, timeout=10)
    print(f"[{datetime.now()}] Triggered: {url}")

def main():
    now = datetime.now()
    today_str = now.strftime("%Y-%m-%d")
    h, m = now.hour, now.minute
    
    # Load state to avoid double-triggering
    state = {}
    if os.path.exists(STATE_FILE):
        with open(STATE_FILE, "r") as f:
            state = json.load(f)
    
    last_trigger = state.get("last_trigger_min", "")
    current_min_str = now.strftime("%Y-%m-%d %H:%M")
    
    if last_trigger == current_min_str:
        return # Already triggered this minute

    # Eid Logic
    if today_str == EID_DATE:
        if h == 4 and m == 35:
            trigger_audio(ADHAN_URL)
            state["last_trigger_min"] = current_min_str
        elif 7 <= h <= 9 and m % 15 == 0:
            if h < 9 or (h == 9 and m == 0):
                trigger_audio(TAKBEER_URL)
                state["last_trigger_min"] = current_min_str
    
    # Daily Schedule Logic
    else:
        try:
            with open(SCHEDULE_FILE, "r") as f:
                schedule = json.load(f)
                for day in schedule:
                    if day["date"] == today_str:
                        # Check Sahoor/Iftar
                        s_h, s_m = map(int, day["sahoor"].split(":"))
                        i_h, i_m = map(int, day["iftar"].split(":"))
                        if (h == s_h and m == s_m) or (h == i_h and m == i_m):
                            trigger_audio(ADHAN_URL)
                            state["last_trigger_min"] = current_min_str
                            break
        except Exception as e:
            print(f"Error reading schedule: {e}")

    # Save state
    with open(STATE_FILE, "w") as f:
        json.dump(state, f)

if __name__ == "__main__":
    main()
