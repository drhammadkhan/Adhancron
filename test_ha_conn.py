import socket
import network
import time

try:
    from wifi_secrets import WIFI_SSID, WIFI_PASSWORD as WIFI_PASS
except ImportError:
    WIFI_SSID = "YOUR_WIFI_SSID"
    WIFI_PASS = "YOUR_WIFI_PASSWORD"
HA_IP = "homeassistant.local"
HA_PORT = 8123

wlan = network.WLAN(network.STA_IF)
wlan.active(True)
wlan.connect(WIFI_SSID, WIFI_PASS)
while not wlan.isconnected():
    time.sleep(0.5)
print("WiFi connected")

print("Testing connection to HA at", HA_IP)
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect((HA_IP, HA_PORT))
    print("Successfully connected to HA port 8123")
    s.close()
except Exception as e:
    print("Could not connect to HA:", e)
