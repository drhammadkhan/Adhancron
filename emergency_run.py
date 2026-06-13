from machine import Pin, SPI
import time
from max7219 import Matrix8x8

# --- HARDWARE PINS (ESP32-C3) ---
SCK_PIN = 6
MOSI_PIN = 7
CS_PIN = 2

# SPI Setup
spi = SPI(1, baudrate=1000000, polarity=0, phase=0, sck=Pin(SCK_PIN), mosi=Pin(MOSI_PIN))
cs = Pin(CS_PIN, Pin.OUT)
display = Matrix8x8(spi, cs, 4)

print("Starting Emergency Reset Script...")
display.brightness(0)
display.fill(0)
display.show()
time.sleep(1)

display.brightness(1)
display.text("LOAD", 0, 0, 1)
display.show()
print("Display initialized")

import network

try:
    from wifi_secrets import WIFI_SSID, WIFI_PASSWORD as WIFI_PASS
except ImportError:
    WIFI_SSID = "YOUR_WIFI_SSID"
    WIFI_PASS = "YOUR_WIFI_PASSWORD"

wlan = network.WLAN(network.STA_IF)
wlan.active(True)
print("Connecting WiFi...")
wlan.connect(WIFI_SSID, WIFI_PASS)

for i in range(15):
    if wlan.isconnected():
        print("WiFi OK")
        display.fill(0)
        display.text("OK", 8, 0, 1)
        display.show()
        break
    time.sleep(1)

if not wlan.isconnected():
    print("WiFi FAIL")
    display.fill(0)
    display.text("FAIL", 0, 0, 1)
    display.show()
else:
    import ntptime
    print("Syncing time...")
    try:
        ntptime.settime()
        print("Time OK")
        now = time.localtime()
        display.fill(0)
        display.text("{:02d}:{:02d}".format(now[3], now[4]), 0, 0, 1)
        display.show()
    except:
        print("Sync FAIL")
        display.fill(0)
        display.text("TIME", 0, 0, 1)
        display.show()

print("Script finished.")