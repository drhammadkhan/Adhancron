import network
import time
wlan = network.WLAN(network.STA_IF)
wlan.active(True)
print("Scanning...")
nets = wlan.scan()
for n in nets:
    try:
        print(n[0].decode())
    except:
        print(n[0])
