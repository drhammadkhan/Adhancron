# Adhan Clock for ESP32-S3

This is the standalone ESP32 companion edition of Adhancron. It targets the
LCDWIKI ES3N28P board: an ESP32-S3 with 16 MB flash, 8 MB PSRAM, a 240x320
ILI9341 display, microSD, and an ES8311 audio codec with a speaker amplifier.

It is intentionally separate from the Docker and desktop applications while
remaining part of the same repository. The firmware is a complete appliance:
it calculates and schedules prayer times, stores and serves its own MP3, drives
the screen and attached speaker, and can control Google Cast or Sonos/DLNA directly. It does
not require Docker, Home Assistant, or another computer after setup.

## Functionality

- First-boot Wi-Fi setup network and browser onboarding.
- Recovery setup network when saved Wi-Fi cannot be reached.
- A user-selectable `.local` address on the home network, defaulting to
  `http://adhancron.local`.
- Online town/city/postcode search followed by automatic coordinate and timezone storage.
- NTP clock synchronisation and daylight-saving-aware local time.
- Local Adhancron prayer calculation: Fajr 90 minutes before sunrise, Dhuhr five minutes after solar noon, standard Asr, Maghrib one minute after sunset, and Moonsighting Committee seasonal Isha.
- Two selectable LVGL clock faces: **Prayer list**, with an `HH:MM:SS`
  clock and complete daily timetable; and **Focus**, a high-contrast minimal layout that
  gives the current time and next prayer greater prominence while retaining a
  compact high-contrast prayer summary. Both show the saved location, countdown, device
  address, and connectivity/audio status.
- User-configured Ramadan start and end dates. During the inclusive 29- or 30-day period, the display adds the Ramadan day number while retaining the canonical Fajr and Maghrib prayer names. The Web UI separately explains when Sehri ends and Iftar begins.
- A seconds-accurate event panel during the final ten minutes before Fajr and Maghrib, showing `SEHRI ENDS IN` or `IFTAR IN` while the prayer table continues to identify the prayers correctly.
- User-defined Eid al-Fitr and Eid al-Adha dates. On either date the display
  shows `EID MUBARAK`, then presents the next takbeer slot from ten minutes
  before the configured playback window.
- Configurable Eid takbeer start time, finish time, and 5-120 minute repeat
  interval. Takbeer is an additional Eid event and does not suppress the five
  normal prayer adhans.
- Fixed-width clock digits that do not move as the seconds change.
- The device IP address in a centred footer so the browser dashboard is easy to find.
- Once-per-prayer scheduling with individual prayer enable switches and a
  two-minute recovery window after a reboot or delayed clock synchronisation.
- Local MP3 playback from the 8 MB internal flash storage partition through the ES8311 speaker path.
- Optional direct Google Cast and Sonos/UPnP-DLNA playback, with local discovery and automatic fallback to the attached speaker if the selected network receiver is unavailable.
- A responsive, mobile-first device dashboard showing the next prayer, countdown, full daily timetable, current playback destination, battery state, and quick audio tests.
- A guided Wi-Fi and location setup flow, with advanced settings collapsed into focused playback, schedule, Ramadan, Eid, recordings, and device sections.
- Separate browser upload and manual playback controls for adhan and Eid
  takbeer, status/timetable API, and byte-range audio serving for both files.
- The original 65-second Eid takbeer recording is bundled with the firmware and
  seeds internal storage when no user recording exists. User replacements are
  preserved by future updates.
- Automatic one-time import of `/sdcard/adhan.mp3` or `/sdcard/takbeer.mp3`
  when the corresponding internal recording is empty.
- Wi-Fi reconnect, optional SD import, NVS recovery, and BOOT-button factory setup reset.

The ES3N28P has no touch panel. The board silkscreen is shared with the touch
variant, but the product packaging identifies this one as `2.8 Without touch`.
Device setup will therefore be browser-based, with the BOOT button reserved for
recovery behaviour rather than normal navigation.

## How It Works

1. On first boot, the clock creates the **Adhancron Setup** Wi-Fi network.
2. The user joins it from a phone or computer, opens `192.168.4.1`, chooses a scanned Wi-Fi network, and saves its password.
3. After joining the home network, the clock exposes the same full dashboard at `http://adhancron.local` and shows both its `.local` name and numerical IP address at the bottom of the display. The `.local` name can be changed under **Device > Local address**; access by numerical IP continues to work.
4. The user searches for a location or enters coordinates. The clock saves the coordinates, timezone, and display name, then calculates prayer times locally each day.
5. At each enabled prayer, the scheduler starts the saved output: the attached speaker, selected Google Cast receiver, or selected Sonos/DLNA receiver.
6. On either user-configured Eid date, the scheduler also plays the separate
   takbeer recording at every slot in the selected time window. Each slot fires
   once, including the start and finish times when they align with the interval.
   A two-minute recovery window covers brief reboots or delayed clock sync.
7. For network playback, the ESP32 controls the Cast V2 or standard UPnP AVTransport receiver and serves `/audio/adhan.mp3` or `/audio/takbeer.mp3` from internal flash with HTTP range support. If discovery or playback fails, it automatically uses the attached speaker.
8. If the home network later becomes unavailable, the prayer display and local schedule continue running. After 20 seconds the recovery setup network appears, while the clock keeps its saved location and settings.

## Display architecture

The interface uses LVGL 9 through Espressif's `esp_lvgl_port` and the native
ILI9341 `esp_lcd` driver. The component versions are pinned in
`main/idf_component.yml` so builds remain reproducible. Two 240 x 32 RGB565 DMA
buffers allow anti-aliased rendering without reserving a full-screen internal
framebuffer. Both clock faces are defined in `main/display_ui.c` and share the
same calculated prayer state; prayer scheduling,
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

## Power and battery

Use a protected single-cell 3.7 V LiPo with the board's battery connector. The
[LCDWIKI board manual](https://www.lcdwiki.com/res/ES3C28P/2.8inch_IPS_ESP32-S3_ES3C28P_ES3N28P_User_Manual.pdf)
shows a TP4054 charger with a 3.3 kOhm programming resistor and specifies a
charge current of approximately 290-300 mA. While USB is connected, USB VBUS
powers the board and the charger charges the cell. The Q3 power-path MOSFET
isolates the system load from the battery; removing USB automatically lets the
battery power the board without a firmware-controlled switchover.

The published schematic does not show a dedicated cell-protection IC, and the
firmware does not currently shut the device down at a low battery voltage. A
protected LiPo is therefore required to provide over-discharge protection; do
not rely on the TP4054 charger or the on-screen percentage for cell protection.

The status bar reads the battery through GPIO 9 using ESP-IDF's calibrated ADC
driver and the board's 200 kOhm/200 kOhm divider. It shows a battery symbol and
estimated percentage whenever a 3.7 V LiPo is connected, changes colour at 15%,
and uses a charging bolt after a sustained voltage rise is detected. This is a
voltage estimate rather than a fuel-gauge measurement, so load, temperature,
and battery chemistry can move the displayed percentage.

The TP4054 charging-status output is not connected to the ESP32. Charging is
therefore inferred from the filtered voltage trend and can take about one minute
to appear after USB power is connected, or about two minutes when the clock has
just started and its ADC baseline is still settling. The dashboard and
`/api/status` expose the same percentage, measured voltage, full estimate, and
detected charging state for diagnostics.

## Install Firmware

### Browser installer (recommended)

Use a Windows, macOS, or Linux computer with Google Chrome or Microsoft Edge:

1. Open [Install Adhan Clock](https://drhammadkhan.github.io/Adhancron/).
2. Confirm that the board is the LCDWIKI ES3N28P with 16 MB flash and 8 MB PSRAM.
3. Connect it directly with a USB data cable.
4. Select **Install firmware**, choose the ESP32-S3 serial device, and wait for installation to reach 100%.
5. Continue with [First Setup](#first-setup).

Phones, tablets, Firefox, and Safari cannot perform the browser installation.
A normal installation preserves saved Wi-Fi, location, settings, and adhan
audio by writing only the bootloader, partition table, OTA bookkeeping, and
application regions. A full-device erase removes them. If the serial device is
not listed, hold BOOT, tap RESET, release RESET, then release BOOT and retry.

The installer also provides the merged firmware image, SHA-256 checksum, and a
manual esptool guide. GitHub Actions rebuilds all of these from source whenever
the ESP32 firmware changes on `main`.

This browser installation is required once to add the OTA partition layout and
rollback-capable bootloader. Future application releases are checked every six
hours over HTTPS and installed automatically when Wi-Fi is online, no adhan is
playing, and no enabled prayer is within ten minutes. The dashboard shows the
installed version, allows automatic updates to be disabled, and offers a manual
**Check and install now** action. Interrupted or unusable updates retain or roll
back to the previous application. The 8 MB audio partition remains at its
original address and is not overwritten by an OTA application update.

### Build from source

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
3. After restart, open the saved local address. Its default is
   `http://adhancron.local`; the numerical IP shown on the display always
   remains available.
4. Search for the home town, city, or postcode and save it.
5. Use the dashboard to choose the attached speaker, a discovered Google Cast
   speaker, or a Sonos/UPnP-DLNA speaker; choose the **Prayer list** or **Focus**
   clock face; test playback, select automatic prayers, set volume, choose
   the first and final fasting days for Ramadan, configure both Eid dates and
   the takbeer window, or replace either MP3.

The dashboard also shows connection state, saved location, today's calculated
times, audio-storage state, and the currently selected playback output. All
normal configuration is available on both the home network and the recovery
setup network.

The dashboard stores the adhan and Eid takbeer MP3s independently in the
device's dedicated 8 MB internal flash partition. They are used for manual
tests, scheduled playback, and the `/audio/adhan.mp3` and
`/audio/takbeer.mp3` endpoints. The bundled takbeer is installed automatically
when that slot is empty. A microSD card is optional: when either internal file
is missing, the firmware imports `/sdcard/adhan.mp3` or `/sdcard/takbeer.mp3`
from an inserted FAT32 card and keeps the card's copy as a backup. The card can
be removed after import.

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

The Ramadan fixture test covers date validation, leap years, year boundaries,
29- and 30-day periods, upcoming status, and inclusive day numbering:

```bash
cc -std=c11 -D_DEFAULT_SOURCE -I main tests/ramadan_host_test.c main/ramadan.c -o /tmp/adhancron-ramadan-test
/tmp/adhancron-ramadan-test
```

The Eid fixture test covers both Eid types, date matching, inclusive start and
finish slots, interval alignment, two-minute recovery, invalid schedules, and
next-slot calculation:

```bash
cc -std=c11 -D_DEFAULT_SOURCE -I main tests/eid_host_test.c main/eid.c main/ramadan.c -o /tmp/adhancron-eid-test
/tmp/adhancron-eid-test
```

Battery percentage and charging-trend logic can be checked without ESP-IDF:

```bash
cc -std=c11 -I main tests/battery_status_host_test.c main/battery_status.c -o /tmp/adhancron-battery-test
/tmp/adhancron-battery-test
```

The embedded web assets have a lightweight host-side structure and size check:

```bash
python3 -m unittest tests/web_assets_host_test.py -v
node --check main/web/app.js
```

For Google Cast playback, the ESP32 discovers compatible receivers with mDNS,
launches Google's Default Media Receiver over the local Cast V2 connection,
and gives it the device's own adhan or takbeer audio URL. The receiver and prayer
clock must be on networks that can reach one another. The saved receiver is
rediscovered before every adhan so DHCP address changes are harmless. If the
receiver is offline or playback cannot be confirmed, the attached speaker is
used automatically instead.

For Sonos and UPnP/DLNA playback, the ESP32 discovers MediaRenderer devices
with SSDP, reads their standard device description, and sends
`SetAVTransportURI` followed by `Play`. The speaker then fetches the MP3
directly from the clock. The selected device-description URL is saved in NVS;
if it is unavailable at prayer time, the attached speaker is used instead.
