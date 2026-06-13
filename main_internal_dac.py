import network
import time
import ntptime
import json
import machine
from machine import Pin, SPI, DAC
from max7219 import Matrix8x8

# Configuration
try:
    from wifi_secrets import WIFI_SSID, WIFI_PASSWORD as WIFI_PASS
except ImportError:
    WIFI_SSID = "YOUR_WIFI_SSID"
    WIFI_PASS = "YOUR_WIFI_PASSWORD"
SCHEDULE_FILE = "schedule.json"

# Hardware Pins - MAX7219
SCK_PIN = 18
MOSI_PIN = 23
CS_PIN = 5

# Internal DAC Pin (GPIO 25 or 26)
# GPIO 25 is DAC channel 1
dac = DAC(Pin(25))

# Setup SPI for MAX7219
spi = SPI(1, baudrate=10000000, polarity=0, phase=0, sck=Pin(SCK_PIN), mosi=Pin(MOSI_PIN))
display = Matrix8x8(spi, Pin(CS_PIN), 4)

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

def play_adhan_internal_dac():
    """
    Simplified audio playback for Internal DAC.
    Note: Internal DAC is 8-bit, so we downsample the 16-bit WAV on the fly.
    """
    print("Playing Adhan via Internal DAC (GPIO 25)...")
    try:
        with open("adhan.wav", "rb") as f:
            f.seek(44) # Skip WAV header
            # Internal DAC expects values 0-255
            # We read 16-bit samples (2 bytes) and shift them
            while True:
                data = f.read(2)
                if not data:
                    break
                # Convert signed 16-bit to unsigned 8-bit
                sample = int.from_bytes(data, 'little', signed=True)
                val = (sample + 32768) >> 8
                dac.write(val)
                # Timing for 44.1kHz is extremely tight in pure MicroPython
                # This may sound 'slow' or 'fast' without a hardware timer.
    except Exception as e:
        print("Audio error:", e)

def scroll_text(text):
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
                h_s, m_s = map(int, sched["sahoor"].split(":"))
                h_i, m_i = map(int, sched["iftar"].split(":"))
                
                if current_hour == h_s and current_min == m_s:
                    play_adhan_internal_dac()
                
                if current_hour == h_i and current_min == m_i:
                    play_adhan_internal_dac()
                    scroll_text("Allahumma laka sumtu, wa alaa rizqika aftartu.")
        
        display.fill(0)
        display.text("{:02d}:{:02d}".format(now[3], now[4]), 0, 0, 1)
        display.show()
        time.sleep(10)

if __name__ == "__main__":
    main_loop()
