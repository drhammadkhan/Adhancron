import network
import time
import ntptime
import json
import machine
from machine import Pin, SPI, I2S
from max7219 import Matrix8x8 # You will need a max7219 library

# Configuration
try:
    from wifi_secrets import WIFI_SSID, WIFI_PASSWORD as WIFI_PASS
except ImportError:
    WIFI_SSID = "YOUR_WIFI_SSID"
    WIFI_PASS = "YOUR_WIFI_PASSWORD"
SCHEDULE_FILE = "schedule.json"

# Hardware Pins
# MAX7219
SCK_PIN = 18
MOSI_PIN = 23
CS_PIN = 5
# PCM5102A I2S
I2S_BCK = 26
I2S_LRCK = 25
I2S_DIN = 22

# Audio settings
SAMPLE_RATE = 44100
BITS_PER_SAMPLE = 16

# Setup SPI for MAX7219
spi = SPI(1, baudrate=10000000, polarity=0, phase=0, sck=Pin(SCK_PIN), mosi=Pin(MOSI_PIN))
display = Matrix8x8(spi, Pin(CS_PIN), 4) # 4 modules

# Setup I2S for PCM5102A
audio_out = I2S(0, sck=Pin(I2S_BCK), ws=Pin(I2S_LRCK), sd=Pin(I2S_DIN),
                mode=I2S.TX, bits=BITS_PER_SAMPLE, format=I2S.STEREO,
                rate=SAMPLE_RATE, ibuf=10240)

def connect_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        print('Connecting to WiFi...')
        wlan.connect(WIFI_SSID, WIFI_PASS)
        while not wlan.isconnected():
            time.sleep(1)
    print('Connected:', wlan.ifconfig())

def sync_time():
    try:
        ntptime.settime()
        print("Time synced via NTP")
    except:
        print("Time sync failed")

def play_adhan():
    print("Playing Adhan...")
    try:
        with open("adhan.wav", "rb") as f:
            f.seek(44) # Skip WAV header
            chunk = bytearray(10240)
            while True:
                num_read = f.readinto(chunk)
                if num_read == 0:
                    break
                audio_out.write(chunk[:num_read])
    except Exception as e:
        print("Audio error:", e)

def scroll_text(text):
    print("Scrolling:", text)
    # Simple scroll logic (implementation depends on max7219 library used)
    for x in range(32, -len(text)*8, -1):
        display.fill(0)
        display.text(text, x, 0, 1)
        display.show()
        time.sleep(0.05)

def get_today_schedule():
    now = time.localtime()
    today_str = "{:04d}-{:02d}-{:02d}".format(now[0], now[1], now[2])
    try:
        with open(SCHEDULE_FILE, "r") as f:
            data = json.load(f)
            for day in data:
                if day["date"] == today_str:
                    return day
    except:
        pass
    return None

def main_loop():
    connect_wifi()
    sync_time()
    
    last_check_min = -1
    
    while True:
        now = time.localtime()
        current_hour = now[3]
        current_min = now[4]
        
        if current_min != last_check_min:
            last_check_min = current_min
            sched = get_today_schedule()
            
            if sched:
                # Format "H:MM" or "HH:MM"
                h_s, m_s = map(int, sched["sahoor"].split(":"))
                h_i, m_i = map(int, sched["iftar"].split(":"))
                
                # Check for Countdown (5 mins before)
                # Check for Trigger time (play Adhan)
                
                # If Sahoor time matches
                if current_hour == h_s and current_min == m_s:
                    play_adhan()
                
                # If Iftar time matches
                if current_hour == h_i and current_min == m_i:
                    play_adhan()
                    scroll_text("Allahumma laka sumtu, wa alaa rizqika aftartu.")
        
        # Default clock display
        display.fill(0)
        display.text("{:02d}:{:02d}".format(now[3], now[4]), 0, 0, 1)
        display.show()
        time.sleep(10)

if __name__ == "__main__":
    main_loop()
