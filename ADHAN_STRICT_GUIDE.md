# Ramadhan Adhan System Notes

## Audio Trigger Command
To play the Adhan on the **Bedroom Speaker** at a specific volume (0.0 to 1.0), use:
`python3 projects/ramadhan_esp32/trigger_ha.py http://YOUR_DOCKER_HOST_IP:8002/adhan_final.mp3 <volume>`

## Why this works
1. **HA URL**: `http://homeassistant.local:8123` (Home Assistant)
2. **Audio URL**: `http://YOUR_DOCKER_HOST_IP:8002/adhan_final.mp3` (Served by `range_server.py` on this laptop).
3. **Range Support**: Google Home requires the `Range` header support provided by `range_server.py`. Using `media-source://` or direct file paths often fails.

## Cron Job Template
Name: `Adhan [Time]`
Payload: `Run the trigger script with the HTTP URL and volume 1.0`
