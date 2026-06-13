import network
import time
import ntptime
import gc
from machine import Pin, SPI
from max7219 import Matrix8x8

try:
    from wifi_secrets import WIFI_SSID, WIFI_PASSWORD as WIFI_PASS
except ImportError:
    WIFI_SSID = "YOUR_WIFI_SSID"
    WIFI_PASS = "YOUR_WIFI_PASSWORD"


# --- HARDWARE PINS (ESP32-C3) ---
SCK_PIN = 6
MOSI_PIN = 7
CS_PIN = 2

spi = SPI(1, baudrate=1000000, polarity=0, phase=0, sck=Pin(SCK_PIN), mosi=Pin(MOSI_PIN))
cs = Pin(CS_PIN, Pin.OUT)
display = Matrix8x8(spi, cs, 4)

def log_display(txt):
    print("LOG:", txt)
    display.fill(0)
    display.text(txt, 0, 0, 1)
    display.show()

log_display("WIFI")
wlan = network.WLAN(network.STA_IF)
wlan.active(True)
print("Connecting to YOUR_WIFI_SSID...")
wlan.connect(WIFI_SSID, WIFI_PASS)

timeout = 30
while not wlan.isconnected() and timeout > 0:
    print(f"Waiting... {timeout}, Status: {wlan.status()}")
    time.sleep(1)
    timeout -= 1

if wlan.isconnected():
    log_display("OK")
    time.sleep(1)
    log_display("SYNC")
    try:
        print("NTP sync start")
        # Try a specific server
        ntptime.host = "uk.pool.ntp.org"
        ntptime.settime()
        print("NTP sync success")
        log_display("DONE")
    except Exception as e:
        print("NTP error:", e)
        log_display("ERR")
else:
    log_display("FAIL")

print("Finished.")
while True:
    time.sleep(1)