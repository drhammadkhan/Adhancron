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

print("Starting Simple Display Test...")
display.brightness(1)

count = 0
while True:
    display.fill(0)
    display.text(str(count), 4, 0, 1)
    display.show()
    print("Count:", count)
    count += 1
    time.sleep(1)
    if count > 99: count = 0
