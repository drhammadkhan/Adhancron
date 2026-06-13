# CasaOS Install

The GitHub Container Registry image is not published yet, so the CasaOS template builds the Docker image directly from this GitHub repo.

## Install In CasaOS

1. Open this file on GitHub:

   ```text
   https://github.com/drhammadkhan/Adhancron/blob/main/casaos/docker-compose.yml
   ```

2. Copy the whole file.

3. In the copied text, find:

   ```text
   PUBLIC_BASE_URL: "http://YOUR_CASAOS_HOST_IP:8090"
   ```

4. Replace `YOUR_CASAOS_HOST_IP` with your CasaOS machine IP address.

   Example:

   ```text
   PUBLIC_BASE_URL: "http://192.168.1.50:8090"
   ```

5. Open CasaOS.

6. Go to **App Store**.

7. Click **Custom Install**.

8. Choose **Docker Compose** if CasaOS asks.

9. Paste the edited compose text.

10. Click **Install**.

11. Wait while CasaOS builds the Docker image. The first install can take a few minutes.

12. Open the app:

    ```text
    http://YOUR_CASAOS_HOST_IP:8090
    ```

13. In the web UI, save:

    ```text
    Home Assistant URL: http://homeassistant.local:8123
    Speaker Entity: media_player.bedroom_speaker
    Home Assistant Token: your long-lived access token
    ```

14. Click **Save Settings**.

15. Click **Play Adhan Now** to test.

## If The Build Fails In CasaOS

Use the terminal method instead.

1. Open the CasaOS terminal or SSH into the CasaOS machine.

2. Run:

   ```bash
   git clone https://github.com/drhammadkhan/Adhancron.git
   cd Adhancron
   cp .env.example .env
   nano .env
   ```

3. Set:

   ```text
   HA_URL=http://homeassistant.local:8123
   HA_ENTITY_ID=media_player.bedroom_speaker
   PUBLIC_BASE_URL=http://YOUR_CASAOS_HOST_IP:8090
   ```

4. Start it:

   ```bash
   docker compose up -d --build
   ```

5. Open:

   ```text
   http://YOUR_CASAOS_HOST_IP:8090
   ```

## Notes

- The CasaOS app builds from `https://github.com/drhammadkhan/Adhancron.git#main`.
- Persistent app data is stored at `/DATA/AppData/adhan-manager`.
- The Home Assistant token is saved inside that data folder, not in the GitHub repo.
- `PUBLIC_BASE_URL` must be reachable by Home Assistant and the speaker integration.
- If port `8090` is already used, change both the port mapping and `PUBLIC_BASE_URL`.
