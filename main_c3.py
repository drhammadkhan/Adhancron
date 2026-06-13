import os
import network
import time
import ntptime
import json
import machine
import gc
import urequests
from machine import Pin, SPI
from max7219 import Matrix8x8

# --- CONFIGURATION ---
try:
    from wifi_secrets import WIFI_SSID, WIFI_PASSWORD as WIFI_PASS
except ImportError:
    WIFI_SSID = "YOUR_WIFI_SSID"
    WIFI_PASS = "YOUR_WIFI_PASSWORD"
SCHEDULE_FILE = "schedule.json"
EID_DATE = "2026-03-20"

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
ADHAN_URL = "media-source://media_source/local/adhan_final.mp3" 
TAKBEER_URL = "media-source://media_source/local/takbeer.mp3"

SCK_PIN = 6
MOSI_PIN = 7
CS_PIN = 2

# SPI Setup
spi = SPI(1, baudrate=1000000, polarity=0, phase=0, sck=Pin(SCK_PIN), mosi=Pin(MOSI_PIN))
cs = Pin(CS_PIN, Pin.OUT)
display = Matrix8x8(spi, cs, 4)

def trigger_audio(url):
    # Audio triggering moved to laptop cron for reliability
    print("DEBUG: Audio trigger bypassed for ESP32:", url)
    return

def connect_and_sync():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    wlan.connect(WIFI_SSID, WIFI_PASS)
    timeout = 20
    while not wlan.isconnected() and timeout > 0:
        time.sleep(1)
        timeout -= 1
    if wlan.isconnected():
        try:
            ntptime.host = "uk.pool.ntp.org"
            ntptime.settime()
            return True
        except:
            return True 
    return False

def get_today_schedule():
    now = time.localtime()
    today_str = "{:04d}-{:02d}-{:02d}".format(now[0], now[1], now[2])
    try:
        with open(SCHEDULE_FILE, "r") as f:
            data = json.load(f)
            for index, day in enumerate(data):
                if day.get("date") == today_str:
                    return day, index + 1
    except:
        pass
    return None, None

def scroll_text(text, b_level=0):
    display.brightness(b_level)
    for x in range(32, -len(text)*8, -1):
        display.fill(0)
        display.text(text, x, 0, 1)
        display.show()
        time.sleep(0.04)
    display.brightness(0)

def draw_clock(h, m, blink):
    display.fill(0)
    sh = "{:02d}".format(h)
    sm = "{:02d}".format(m)
    display.framebuf.text(sh[0], 0, 0, 1)
    display.framebuf.text(sh[1], 7, 0, 1)
    display.framebuf.text(sm[0], 17, 0, 1)
    display.framebuf.text(sm[1], 24, 0, 1)
    if blink:
        display.framebuf.pixel(15, 2, 1)
        display.framebuf.pixel(15, 5, 1)
    display.show()

def main_loop():
    connect_and_sync()
    display.brightness(0)
    current_mode = "TIME"
    last_sec = -1
    
    last_trigger_min = -1
    
    while True:
        gc.collect()
        now = time.localtime()
        today_str = "{:04d}-{:02d}-{:02d}".format(now[0], now[1], now[2])
        is_eid = (today_str == EID_DATE)
        
        h, m, s = now[3], now[4], now[5]
        if s == last_sec:
            time.sleep(0.1)
            continue
        last_sec = s
        if s % 10 == 0:
            print("Time: {:02d}:{:02d}:{:02d} - Free Mem: {}".format(h, m, s, gc.mem_free()))

        # --- AUDIO TRIGGERS ---
        if m != last_trigger_min:
            if is_eid:
                # Adhan at 4:35 AM on Eid day
                if h == 4 and m == 35:
                    trigger_audio(ADHAN_URL)
                    last_trigger_min = m
                # Takbeer every 15 mins between 7am and 9am
                if 7 <= h <= 9 and m % 15 == 0:
                    if h < 9 or (h == 9 and m == 0):
                        trigger_audio(TAKBEER_URL)
                        last_trigger_min = m
            else:
                sched, day_num = get_today_schedule()
                if sched:
                    h_s, m_s = map(int, sched["sahoor"].split(":"))
                    h_i, m_i = map(int, sched["iftar"].split(":"))
                    if (h == h_s and m == m_s) or (h == h_i and m == m_i):
                        trigger_audio(ADHAN_URL)
                        last_trigger_min = m

        # --- DISPLAY MODES ---
        new_mode = "TIME"
        if is_eid:
            # Scroll for 30s every 30s
            # 30-59s phase: Scroll "Eid Mubarak"
            if 30 <= s < 60:
                new_mode = "EID_SCROLL"
        else:
            sched, day_num = get_today_schedule()
            if sched:
                h_s, m_s = map(int, sched["sahoor"].split(":"))
                h_i, m_i = map(int, sched["iftar"].split(":"))
                
                # Check for Iftar Dua (2 minutes after Iftar)
                cur_total = h * 60 + m
                iftar_total = h_i * 60 + m_i
                
                if iftar_total <= cur_total < iftar_total + 2:
                    new_mode = "IFTAR_DUA"
                else:
                    diff_s = ((h_s*60+m_s)*60) - ((h*60+m)*60+s)
                    diff_i = ((h_i*60+m_i)*60) - ((h*60+m)*60+s)
                    if 0 < diff_s <= 300 or 0 < diff_i <= 300:
                        new_mode = "COUNTDOWN"
                        diff = diff_s if (0 < diff_s <= 300) else diff_i
                    elif 30 <= s < 45 and day_num:
                        new_mode = "DAY"

        # --- EXECUTE DISPLAY ---
        if new_mode == "IFTAR_DUA":
            scroll_text("Allahumma laka sumtu, wa alaa rizqika aftartu.")
            current_mode = "IFTAR_DUA"
        elif new_mode == "EID_SCROLL":
            if current_mode != "EID_SCROLL":
                scroll_text("EID MUBARAK!", b_level=15)
                current_mode = "EID_SCROLL"
            display.fill(0)
            display.show()
        elif new_mode == "COUNTDOWN":
            dm, ds = diff // 60, diff % 60
            text = "{:d}:{:02d}".format(dm, ds)
            display.fill(0)
            if len(text) <= 4:
                display.framebuf.text(text, 0, 0, 1)
            else:
                display.framebuf.text(text, -4, 0, 1)
            display.show()
            current_mode = "COUNTDOWN"
        elif new_mode == "DAY":
            if current_mode != "DAY":
                scroll_text("DAY {}".format(day_num))
                current_mode = "DAY"
            display.fill(0)
            display.text("D{}".format(day_num), 8, 0, 1)
            display.show()
        else:
            current_mode = "TIME"
            draw_clock(h, m, s % 2 == 0)

if __name__ == "__main__":
    main_loop()
