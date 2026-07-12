# Adhancron Prayer Clock for ESP32-S3

This is the standalone ESP32 companion edition of Adhancron. It targets the
LCDWIKI ES3N28P board: an ESP32-S3 with 16 MB flash, 8 MB PSRAM, a 240x320
ILI9341 display, microSD, and an ES8311 audio codec with a speaker amplifier.

It is intentionally separate from the Docker and desktop applications. The
shared product goals are local prayer-time calculation, scheduling, MP3 storage
and serving, and optional network playback. This firmware will make the local
speaker the dependable primary adhan output.

## Current milestone: hardware check

The initial application verifies the board before building the prayer-clock UI:

- fills the LCD with distinct colour bands and vertical alignment markers;
- reports PSRAM capacity over USB serial;
- enables the audio amplifier and probes the ES8311 codec on I2C;
- mounts a FAT32 microSD card at `/sdcard`, when inserted;
- performs a Wi-Fi scan.

The ES3N28P has no touch panel. The board silkscreen is shared with the touch
variant, but the product packaging identifies this one as `2.8 Without touch`.
Device setup will therefore be browser-based, with the BOOT button reserved for
recovery behaviour rather than normal navigation.

## Board wiring

| Device | Pins |
| --- | --- |
| ILI9341 display | CS 10, DC 46, SCLK 12, MOSI 11, MISO 13, backlight 45 |
| microSD (SDIO) | CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47 |
| Audio (I2S) | MCLK 4, BCLK 5, DOUT 6, LRCK 7, DIN 8, enable 1 |
| Codec control (I2C) | SDA 16, SCL 15; ES8311 normally at `0x18` |
| Status LED | 42 |
| Battery ADC | 9 |

## Build and flash

Install ESP-IDF 5.4 or later, then run:

```bash
cd firmware/esp32-s3-prayer-clock
. "$IDF_PATH/export.sh"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem112301 flash monitor
```

The first build downloads the official ILI9341 panel driver declared in
`main/idf_component.yml`. The serial monitor should finish with `Hardware check
complete`; the screen should show five horizontal colour bands with vertical
black markers.

## Planned milestones

1. Hardware check: LCD, PSRAM, SD card, audio codec, Wi-Fi.
2. Local MP3 playback from microSD through the ES8311 codec.
3. Wi-Fi setup and a compact phone-friendly configuration page.
4. Local prayer-time calculation, timezone rules, and daily schedule storage.
5. Full-screen prayer clock, Sehri/Iftar display, and countdown experience.
6. HTTP MP3 endpoint with `HEAD` and byte-range support for compatible players.
7. Optional Home Assistant playback. Direct Google Cast is deliberately an
   experimental later milestone because there is no official ESP-IDF sender SDK.
