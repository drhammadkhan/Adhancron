from machine import Pin, SPI
from max7219 import Matrix8x8
import machine

SCK_PIN = 6
MOSI_PIN = 7
CS_PIN = 2

spi = SPI(1, baudrate=1000000, polarity=0, phase=0, sck=Pin(SCK_PIN), mosi=Pin(MOSI_PIN))
cs = Pin(CS_PIN, Pin.OUT)
display = Matrix8x8(spi, cs, 4)

print("Methods on display:", dir(display))
