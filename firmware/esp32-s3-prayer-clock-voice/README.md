# Adhan Clock ESP32-S3 Voice Firmware

This is an experimental sibling firmware for the LCDWIKI ESP32-S3 2.8-inch Adhan Clock.

It keeps the normal clock, prayer-time, web UI, audio, Cast, DLNA, Ramadan and Eid features, and adds local offline voice command recognition through Espressif ESP-SR.

This remains an experimental, USB-first build. Its MultiNet setup now follows Espressif's reference initialization sequence and exits cleanly if speech recognition cannot start, allowing the prayer clock itself to continue running.

## Voice commands

The intended command set is:

- `next prayer` - switches the screen to the focus clock face.
- `play adhan` - starts adhan playback using the configured output.

This is deliberately separate from `firmware/esp32-s3-prayer-clock` so the normal firmware stays stable while the microphone path and recognition accuracy are tested on real hardware.

## Flash layout

ESP-SR needs a model partition. This variant uses a USB-first partition layout:

- one large app partition
- one FAT storage partition for saved settings and audio
- one SPIFFS `model` partition for ESP-SR model data

It does not keep the standard dual OTA slots yet. Once the voice path is proven, we can decide whether to make a smaller OTA-capable voice build.

## Build

```bash
cd firmware/esp32-s3-prayer-clock-voice
source "$HOME/esp/esp-idf-v5.4.1/export.sh"
idf.py build
```

## Flash

```bash
cd firmware/esp32-s3-prayer-clock-voice
source "$HOME/esp/esp-idf-v5.4.1/export.sh"
idf.py -p /dev/cu.usbmodem112301 flash monitor
```

The model partition is part of the ESP-IDF build output and is flashed by `idf.py flash`.
