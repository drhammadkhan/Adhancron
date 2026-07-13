# Adhancron Prayer Clock for ESP32-S3

This is the standalone ESP32 companion edition of Adhancron. It targets the
LCDWIKI ES3N28P board: an ESP32-S3 with 16 MB flash, 8 MB PSRAM, a 240x320
ILI9341 display, microSD, and an ES8311 audio codec with a speaker amplifier.

It is intentionally separate from the Docker and desktop applications. The
shared product goals are local prayer-time calculation, scheduling, MP3 storage
and serving, and optional network playback. This firmware will make the local
speaker the dependable primary adhan output.

## Functionality

- First-boot Wi-Fi setup network and browser onboarding.
- Recovery setup network when saved Wi-Fi cannot be reached.
- `http://adhancron.local` discovery on the home network.
- Online town/city/postcode search followed by automatic coordinate and timezone storage.
- NTP clock synchronisation and daylight-saving-aware local time.
- Local Adhancron prayer calculation: Fajr 90 minutes before sunrise, Dhuhr five minutes after solar noon, standard Asr, Maghrib/Iftar one minute after sunset, and Moonsighting Committee seasonal Isha.
- Full-screen clock with Fajr/Sehri, sunrise, Dhuhr, Asr, Iftar, Isha, next prayer, and countdown.
- Once-per-prayer scheduling with individual prayer enable switches and a
  two-minute recovery window after a reboot or delayed clock synchronisation.
- Local MP3 playback from `/sdcard/adhan.mp3` through the ES8311 speaker path.
- Browser MP3 upload, manual playback, status/timetable API, and byte-range audio serving.
- Wi-Fi reconnect, SD retry, NVS recovery, and BOOT-button factory setup reset.

The ES3N28P has no touch panel. The board silkscreen is shared with the touch
variant, but the product packaging identifies this one as `2.8 Without touch`.
Device setup will therefore be browser-based, with the BOOT button reserved for
recovery behaviour rather than normal navigation.

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
5. Use the dashboard to test playback, select automatic prayers, set volume,
   or replace the MP3.

The SD card may be empty on first setup. Once it is mounted, use the dashboard
to upload an MP3; it is stored as `/sdcard/adhan.mp3` and used for both manual
tests and scheduled playback.

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

Direct Google Cast control is not included. Google does not provide an ESP-IDF
Cast sender SDK, and an unverified protocol reimplementation would be less
dependable than the local speaker. The device does expose
`/audio/adhan.mp3` with byte-range support for external integrations.
