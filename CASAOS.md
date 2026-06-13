# CasaOS Install

There are two practical ways to install this on CasaOS.

## Option A: Install From A Published GitHub Image

This is the best long-term setup once you have published a Docker image to GitHub Container Registry. If you want automatic GHCR publishing later, refresh GitHub CLI auth with the workflow scope and add a Docker publish workflow.

1. Push this repo to GitHub.
2. Publish a Docker image to GitHub Container Registry as:

   ```text
   ghcr.io/drhammadkhan/adhancron:latest
   ```

3. Confirm the package exists at:

   ```text
   ghcr.io/drhammadkhan/adhancron:latest
   ```

4. In this repo, open `casaos/docker-compose.yml`.
5. Replace:

   ```text
   PUBLIC_BASE_URL: "http://YOUR_CASAOS_HOST_IP:8090"
   ```

   with the LAN URL Home Assistant can reach, for example:

   ```text
   PUBLIC_BASE_URL: "http://YOUR_CASAOS_HOST_IP:8090"
   ```

6. In CasaOS, open **App Store** -> **Custom Install**.
7. Paste the contents of `casaos/docker-compose.yml`.
8. Install the app.
9. Open:

    ```text
    http://YOUR_CASAOS_HOST_IP:8090
    ```

10. In the web UI, save your Home Assistant token, URL, and speaker entity.

## Option B: Build Directly On The CasaOS Machine

Use this if you do not want to publish an image yet.

1. Open CasaOS terminal/SSH.
2. Clone the repo:

   ```bash
   git clone https://github.com/drhammadkhan/Adhancron.git
   cd Adhancron
   ```

3. Create an `.env` file:

   ```bash
   cp .env.example .env
   nano .env
   ```

4. Set at least:

   ```text
   HA_URL=http://homeassistant.local:8123
   HA_ENTITY_ID=media_player.bedroom_speaker
   PUBLIC_BASE_URL=http://YOUR_CASAOS_HOST_IP:8090
   ```

5. Start it:

   ```bash
   docker compose up -d --build
   ```

6. Open the web UI and save your Home Assistant token:

   ```text
   http://YOUR_CASAOS_HOST_IP:8090
   ```

## Important Notes

- `PUBLIC_BASE_URL` must be reachable by Home Assistant and the speaker integration.
- The Home Assistant token is saved in `/data/adhan_settings.json` inside the container.
- The CasaOS template maps persistent data to `/DATA/AppData/adhan-manager`.
- If port `8090` is already used, change both the port mapping and `PUBLIC_BASE_URL`.
