from machine import Pin, SPI
import max7219
import time

spi = SPI(1, baudrate=1000000, polarity=0, phase=0, sck=Pin(6), mosi=Pin(7))
cs = Pin(2, Pin.OUT)
display = max7219.Matrix8x8(spi, cs, 4)

def test():
    msg = "12:34"
    print("Testing static text...")
    display.fill(0)
    display.text(msg, 0, 0, 1)
    display.show()
    time.sleep(2)
    
    print("Testing scrolling clock...")
    for x in range(32, -40, -1):
        display.fill(0)
        display.text(msg, x, 0, 1)
        display.show()
        time.sleep(0.05)

if __name__ == "__main__":
    test()
