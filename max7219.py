from machine import Pin, SPI
import framebuf

class Matrix8x8:
    def __init__(self, spi, cs, num):
        self.spi = spi
        self.cs = cs
        self.num = num
        self.buffer = bytearray(8 * num)
        # Use MONO_HLSB for standard horizontal mapping
        self.framebuf = framebuf.FrameBuffer(self.buffer, 8 * num, 8, framebuf.MONO_HLSB)
        self.init()

    def _write(self, command, data):
        self.cs(0)
        for i in range(self.num):
            self.spi.write(bytearray([command, data]))
        self.cs(1)

    def init(self):
        for command, data in (
            (0x0f, 0x00), # display test off
            (0x0b, 0x07), # scan limit all
            (0x09, 0x00), # decode off
            (0x0c, 0x01), # shutdown off
            (0x0a, 0x02), # brightness
        ):
            self._write(command, data)

    def brightness(self, value):
        self._write(0x0a, value & 0x0f)

    def fill(self, col):
        self.framebuf.fill(col)

    def text(self, string, x, y, col=1):
        self.framebuf.text(string, x, y, col)

    def show(self):
        # Handle 180-degree rotation (upside down fix)
        # We iterate through rows and flip the data sent to the MAX7219
        for row in range(8):
            self.cs(0)
            # The matrix expects row 1-8. If it's upside down, we reverse row order.
            # Row index 1-8. We map framebuffer row (7-row) to MAX7219 row (row+1)
            target_row = row + 1
            fb_row = 7 - row
            
            for i in range(self.num - 1, -1, -1):
                data = 0
                for col in range(8):
                    # Reverse the bit order (column flip) for the 180 rotation
                    if self.framebuf.pixel(i * 8 + col, fb_row):
                        data |= 1 << col # Changed from (7-col) to col to flip horizontal
                self.spi.write(bytearray([target_row, data]))
            self.cs(1)
