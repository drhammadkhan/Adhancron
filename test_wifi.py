import network
import time
try:
    from wifi_secrets import WIFI_SSID, WIFI_PASSWORD as WIFI_PASS
except ImportError:
    WIFI_SSID = "YOUR_WIFI_SSID"
    WIFI_PASS = "YOUR_WIFI_PASSWORD"

wlan = network.WLAN(network.STA_IF)
wlan.active(True)
if not wlan.isconnected():
    print('Connecting to WiFi...')
    wlan.connect(WIFI_SSID, WIFI_PASS)
    for _ in range(20):
        if wlan.isconnected():
            break
        print("Status:", wlan.status())
        time.sleep(1)

if wlan.isconnected():
    print("Connected!", wlan.ifconfig())
else:
    print("Failed. Final Status:", wlan.status())
