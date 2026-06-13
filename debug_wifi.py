import network
import time
try:
    from wifi_secrets import WIFI_SSID, WIFI_PASSWORD as WIFI_PASS
except ImportError:
    WIFI_SSID = "YOUR_WIFI_SSID"
    WIFI_PASS = "YOUR_WIFI_PASSWORD"

wlan = network.WLAN(network.STA_IF)
wlan.active(True)
print("Scanning for networks...")
nets = wlan.scan()
found = False
for n in nets:
    ssid = n[0].decode()
    rssi = n[3]
    print(f"SSID: {ssid}, RSSI: {rssi}")
    if ssid == WIFI_SSID:
        found = True
        print(">>> TARGET SSID FOUND!")

if not found:
    print(">>> TARGET SSID NOT FOUND IN SCAN.")

print('Connecting to WiFi...')
wlan.connect(WIFI_SSID, WIFI_PASS)

for i in range(30):
    status = wlan.status()
    print(f"Sec {i}: Status={status}, Connected={wlan.isconnected()}")
    if wlan.isconnected():
        print("Connected!", wlan.ifconfig())
        break
    time.sleep(1)

if not wlan.isconnected():
    print("Final Failure.")
