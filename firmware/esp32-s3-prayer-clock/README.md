# Adhan Clock for ESP32-S3

This is the standalone ESP32 companion edition of Adhancron. It targets the
LCDWIKI ES3N28P board: an ESP32-S3 with 16 MB flash, 8 MB PSRAM, a 240x320
ILI9341 display, microSD, and an ES8311 audio codec with a speaker amplifier.

It is intentionally separate from the Docker and desktop applications while
remaining part of the same repository. The firmware is a complete appliance:
it calculates and schedules prayer times, stores and serves its own MP3, drives
the screen and attached speaker, and can control Google Cast directly. It does
not require Docker, Home Assistant, or another computer after setup.

## Functionality

- First-boot Wi-Fi setup network and browser onboarding.
- Recovery setup network when saved Wi-Fi cannot be reached.
- `http://adhancron.local` discovery on the home network.
- Online town/city/postcode search followed by automatic coordinate and timezone storage.
- NTP clock synchronisation and daylight-saving-aware local time.
- Local Adhancron prayer calculation: Fajr 90 minutes before sunrise, Dhuhr five minutes after solar noon, standard Asr, Maghrib one minute after sunset, and Moonsighting Committee seasonal Isha.
- Full-screen LVGL dashboard with an `HH:MM:SS` clock, date, saved location, Fajr, sunrise, Dhuhr, Asr, Maghrib, Isha, next prayer, countdown, and Wi-Fi/audio status.
- Fixed-width clock digits that do not move as the seconds change.
- The device IP address in a centred footer so the browser dashboard is easy to find.
- Once-per-prayer scheduling with individual prayer enable switches and a
  two-minute recovery window after a reboot or delayed clock synchronisation.
- Local MP3 playback from the 8 MB internal flash storage partition through the ES8311 speaker path.
- Optional direct Google Cast playback, with speaker discovery on the local network and automatic fallback to the attached speaker if Cast is unavailable.
- Browser MP3 upload, manual playback, status/timetable API, and byte-range audio serving.
- Automatic one-time import of `/sdcard/adhan.mp3` when internal audio is empty.
- Wi-Fi reconnect, optional SD import, NVS recovery, and BOOT-button factory setup reset.

The ES3N28P has no touch panel. The board silkscreen is shared with the touch
variant, but the product packaging identifies this one as `2.8 Without touch`.
Device setup will therefore be browser-based, with the BOOT button reserved for
recovery behaviour rather than normal navigation.

## How It Works

1. On first boot, the clock creates the **Adhancron Setup** Wi-Fi network.
2. The user joins it from a phone or computer, opens `192.168.4.1`, chooses a scanned Wi-Fi network, and saves its password.
3. After joining the home network, the clock exposes the same full dashboard at `http://adhancron.local` and shows its numerical IP address at the bottom of the display.
4. The user searches for a location or enters coordinates. The clock saves the coordinates, timezone, and display name, then calculates prayer times locally each day.
5. At each enabled prayer, the scheduler starts the saved output: either the attached speaker or the selected Google Cast receiver.
6. For Cast playback, the ESP32 discovers the receiver, starts the Default Media Receiver, and serves `/audio/adhan.mp3` from internal flash with HTTP range support. If discovery or playback fails, it automatically uses the attached speaker.
7. If the home network later becomes unavailable, the prayer display and local schedule continue running. After 20 seconds the recovery setup network appears, while the clock keeps its saved location and settings.

## Display architecture

The interface uses LVGL 9 through Espressif's `esp_lvgl_port` and the native
ILI9341 `esp_lcd` driver. The component versions are pinned in
`main/idf_component.yml` so builds remain reproducible. Two 240 x 32 RGB565 DMA
buffers allow anti-aliased rendering without reserving a full-screen internal
framebuffer. The dashboard is defined in `main/display_ui.c`; prayer scheduling,
audio playback, Wi-Fi, and the web server remain independent of the renderer.

## Board wiring

| Device | Pins |
| --- | --- |
| ILI9341 display | CS 10, DC 46, SCLK 12, MOSI 11, MISO 13, backlight 45 |
| microSD (SDIO) | CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47 |
| Audio (I2S) | MCLK 4, BCLK 5, speaker data out 8, LRCK 7, microphone data in 6, enable 1 |
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

The first build downloads the declared ESP-IDF components. A successful boot
shows the setup instructions or prayer clock on the display.

## First Setup

1. Join **Adhancron Setup** with password `adhancron`.
2. Open `http://192.168.4.1`, choose a scanned home Wi-Fi network, and enter
   its password. Hidden and open networks are also supported.
3. After restart, open `http://adhancron.local`.
4. Search for the home town, city, or postcode and save it.
5. Use the dashboard to choose the attached speaker or a discovered Google
   Cast speaker, test playback, select automatic prayers, set volume, or
   replace the MP3.

The dashboard also shows connection state, saved location, today's calculated
times, audio-storage state, and the currently selected playback output. All
normal configuration is available on both the home network and the recovery
setup network.

The dashboard stores an uploaded MP3 in the device's dedicated 8 MB internal
flash partition. It is used for manual tests, scheduled playback, and the
`/audio/adhan.mp3` endpoint. A microSD card is optional: when internal audio is
empty, the firmware imports `/sdcard/adhan.mp3` from an inserted FAT32 card and
keeps the card's copy as a backup. The card can be removed after import.

Hold BOOT for three seconds during startup to clear saved setup. If home Wi-Fi
cannot be reached for 20 seconds, the recovery setup network is enabled without
erasing settings.

## Verification

The host prayer-time fixture test compares London and Oshkosh dates in January,
June, and December against the Python Adhancron implementation:

```bash
cc -std=c11 -D_DEFAULT_SOURCE -I main tests/prayer_times_host_test.c main/prayer_times.c -lm -o /tmp/adhancron-prayer-test
/tmp/adhancron-prayer-test
```

For Google Cast playback, the ESP32 discovers compatible receivers with mDNS,
launches Google's Default Media Receiver over the local Cast V2 connection,
and gives it the device's own `/audio/adhan.mp3` URL. The receiver and prayer
clock must be on networks that can reach one another. The saved receiver is
rediscovered before every adhan so DHCP address changes are harmless. If the
receiver is offline or playback cannot be confirmed, the attached speaker is
used automatically instead.
