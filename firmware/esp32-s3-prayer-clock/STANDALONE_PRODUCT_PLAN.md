# Adhancron Prayer Clock: Standalone ESP32-S3 Plan

## Goal

Turn the LCDWIKI ES3N28P ESP32-S3 board into a self-contained prayer clock.
It calculates prayer times locally, displays them, plays an adhan stored on its
microSD card, and is configured through its own local web page. Docker, Home
Assistant, and Google Home are optional companion products, not requirements.

## Confirmed Hardware

- ESP32-S3, 16 MB flash and 8 MB PSRAM
- 240 x 320 ILI9341 display (no touch fitted)
- ES8311 codec, FM8002E amplifier, and connected speaker
- FAT32 microSD card
- Wi-Fi and Bluetooth-capable radio

## Delivered Foundation

- Display, PSRAM, SD card, codec, amplifier, and speaker checks
- MP3 decoding and playback from `/sdcard/adhan.mp3`
- Correct board-specific I2S wiring and 44.1 kHz audio output

## Completed Build

1. Persist Wi-Fi, location, timezone, and playback settings in NVS.
2. Provide a setup access point and local web UI when Wi-Fi has not been configured.
3. Synchronise time through NTP and apply a selected POSIX timezone rule, including DST.
4. Calculate sunrise, sunset, Fajr, Dhuhr, Asr, Maghrib, and Isha locally from latitude and longitude.
5. Display the current time, daily timetable, next prayer, and countdown.
6. Trigger the SD-card adhan locally once at each enabled prayer time.
7. Expose settings, timetable, manual playback, and MP3 upload through the web UI.
8. Recover cleanly after a power cut, missing SD card, or lost Wi-Fi.

## Private Configuration

These are deliberately not committed into firmware or source control:

- Home Wi-Fi network name and password
- Home location, resolved to latitude, longitude, and timezone by the setup page
- Preferred adhan volume

## First-Boot Setup

1. Power the device from USB with its microSD card inserted.
2. On a phone or computer, join the Wi-Fi network **Adhancron Setup**.
3. Enter the password **adhancron**.
4. Open `http://192.168.4.1` in a browser.
5. Enter the home Wi-Fi name and password, then choose **Connect and continue**.
6. After the device has joined the home network, open its page again and search for the home town, city, or postcode.
7. The device applies Adhancron's established calculation rules: Fajr 90 minutes before sunrise, Dhuhr five minutes after solar noon, standard Asr, Maghrib one minute after sunset, and the Moonsighting Committee seasonal Isha adjustment.

After restart, the device connects to the saved Wi-Fi network and begins NTP time synchronisation. The same settings page is served through the device's LAN address once it has joined the network.

If a restart or delayed NTP synchronisation lands within two minutes after an
enabled prayer time, the scheduler still plays that prayer once. This avoids a
missed adhan during a brief power interruption without replaying old prayers.

## Google Cast Scope

Local speaker playback is the reliable primary function. The ESP32 serves the
MP3 with byte-range support for external integrations. Direct Cast session
control is excluded because Google provides no ESP-IDF sender SDK; shipping an
unverified reverse-engineered client would weaken the appliance's core purpose.

## Definition of Done

The device can be powered on after a power cut, reconnect to Wi-Fi, know local
time, calculate that day's timetable, show the next prayer, and play
`/sdcard/adhan.mp3` locally at each enabled prayer time without another service.
