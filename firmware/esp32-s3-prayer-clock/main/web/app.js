const byId = id => document.getElementById(id);
const page = document.body.dataset.page;
let savedCastId = '';
let savedCastName = '';
let savedDlnaUrl = '';
let savedDlnaName = '';
let dashboardReady = false;

function setNotice(element, message, error = false) {
  if (!element) return;
  element.textContent = message;
  element.classList.remove('hidden', 'error');
  if (error) element.classList.add('error');
}

async function responseText(response) {
  const message = await response.text();
  if (!response.ok) throw new Error(message || 'The device could not complete that request.');
  return message;
}

function minutesFromTime(value) {
  const parts = String(value || '').split(':').map(Number);
  return parts.length === 2 && parts.every(Number.isFinite) ? parts[0] * 60 + parts[1] : -1;
}

function formatCountdown(minutes) {
  if (minutes <= 0) return 'Starting now';
  const hours = Math.floor(minutes / 60);
  const remainder = minutes % 60;
  if (!hours) return `in ${remainder} min`;
  if (!remainder) return `in ${hours} hr`;
  return `in ${hours} hr ${remainder} min`;
}

function renderPrayerDay(status) {
  const container = byId('prayer-times');
  if (!container) return;
  container.replaceChildren();
  if (!status.prayers || !status.prayers.length) {
    const empty = document.createElement('p');
    empty.className = 'field-note';
    empty.textContent = 'Prayer times will appear after the clock and location are ready.';
    container.appendChild(empty);
    byId('next-name').textContent = '--';
    byId('next-time').textContent = '--:--';
    byId('next-countdown').textContent = 'Waiting for prayer times';
    return;
  }

  const prayerIndexes = [0, 2, 3, 4, 5];
  const currentMinute = Number.isFinite(status.device_minute) ? status.device_minute : 0;
  let nextIndex = prayerIndexes.find(index => minutesFromTime(status.prayers[index].time) >= currentMinute);
  let countdown = 0;
  if (nextIndex === undefined) {
    nextIndex = 0;
    countdown = 1440 - currentMinute + minutesFromTime(status.prayers[0].time);
  } else {
    countdown = minutesFromTime(status.prayers[nextIndex].time) - currentMinute;
  }

  status.prayers.forEach((prayer, index) => {
    const item = document.createElement('div');
    item.className = `prayer-item${index === nextIndex ? ' next' : ''}`;
    const name = document.createElement('span');
    const time = document.createElement('strong');
    name.textContent = prayer.name;
    time.textContent = prayer.time;
    item.append(name, time);
    container.appendChild(item);
  });

  byId('next-name').textContent = status.prayers[nextIndex].name;
  byId('next-time').textContent = status.prayers[nextIndex].time;
  byId('next-countdown').textContent = formatCountdown(countdown);
}

function playbackDescription(status) {
  if (status.playback_output === 'cast') return status.cast_device_name ? `Google Cast: ${status.cast_device_name}` : 'Google Cast speaker';
  if (status.playback_output === 'dlna') return status.dlna_device_name ? `Sonos / DLNA: ${status.dlna_device_name}` : 'Sonos / DLNA speaker';
  return 'Attached speaker';
}

function setChecked(name, checked) {
  const input = document.querySelector(`[name="${name}"]`);
  if (input) input.checked = Boolean(checked);
}

function populateSettings(status) {
  savedCastId = status.cast_device_id || '';
  savedCastName = status.cast_device_name || '';
  savedDlnaUrl = status.dlna_device_url || '';
  savedDlnaName = status.dlna_device_name || '';

  const output = document.querySelector(`[name="output"][value="${status.playback_output || 'attached'}"]`);
  if (output) output.checked = true;
  const displayStyle = document.querySelector(`[name="display_style"][value="${status.display_style || 'detailed'}"]`);
  if (displayStyle) displayStyle.checked = true;
  const volume = byId('volume');
  if (volume) {
    volume.value = status.volume ?? 80;
    byId('volume-value').value = `${volume.value}%`;
  }
  setChecked('fajr', status.enabled_fajr);
  setChecked('dhuhr', status.enabled_dhuhr);
  setChecked('asr', status.enabled_asr);
  setChecked('maghrib', status.enabled_maghrib);
  setChecked('isha', status.enabled_isha);
  setChecked('automatic_updates', status.automatic_updates);
  byId('device-hostname').value = status.device_hostname || 'adhancron';
  updateLocalAddressPreview();
  byId('ramadan-start').value = status.ramadan_start || '';
  byId('ramadan-end').value = status.ramadan_end || '';
  byId('eid-fitr').value = status.eid_fitr || '';
  byId('eid-adha').value = status.eid_adha || '';
  byId('eid-start').value = status.eid_takbeer_start || '06:00';
  byId('eid-end').value = status.eid_takbeer_end || '12:00';
  byId('eid-interval').value = status.eid_takbeer_interval || 30;
  outputChanged();
}

async function refreshDashboard(initial = false) {
  try {
    const response = await fetch('/api/status', { cache: 'no-store' });
    if (!response.ok) throw new Error(await response.text());
    const status = await response.json();
    byId('place-name').textContent = status.location || 'Location not configured';
    byId('location-link-note').textContent = status.location ? `Currently ${status.location}` : 'Set the calculation location';
    byId('today-date').textContent = status.device_date ? `${status.device_date} at ${status.device_time}` : 'Synchronising the clock...';
    byId('playback-summary').textContent = playbackDescription(status);
    byId('ramadan-status').textContent = status.ramadan_message || 'Ramadan status unavailable';
    byId('eid-status').textContent = status.eid_message || 'Eid status unavailable';
    byId('adhan-audio-status').textContent = status.adhan_audio_available ? 'Recording ready in internal storage.' : 'No adhan recording saved.';
    byId('takbeer-audio-status').textContent = status.takbeer_audio_available ? 'Recording ready in internal storage.' : 'No Eid takbeer recording saved.';
    byId('firmware-status').textContent = `Version ${status.firmware_version || 'unknown'} - ${status.update_status || 'status unavailable'}`;
    byId('footer-version').textContent = `Firmware ${status.firmware_version || 'unknown'}`;
    byId('update-button').disabled = Boolean(status.update_running);

    const battery = byId('battery-chip');
    if (status.battery_available) {
      battery.textContent = `${status.battery_charging ? 'Charging ' : ''}${status.battery_percentage}%`;
      battery.classList.remove('hidden');
    } else {
      battery.classList.add('hidden');
    }
    renderPrayerDay(status);
    if (initial) populateSettings(status);
    dashboardReady = true;
  } catch (error) {
    byId('connection-label').textContent = 'Reconnecting';
    byId('connection-label').previousElementSibling.style.background = '#d5a84a';
    if (!dashboardReady) setNotice(byId('test-result'), error.message, true);
  }
}

function outputChanged() {
  const selected = document.querySelector('[name="output"]:checked');
  if (!selected) return;
  const cast = selected.value === 'cast';
  const dlna = selected.value === 'dlna';
  byId('cast-controls').classList.toggle('hidden', !cast);
  byId('dlna-controls').classList.toggle('hidden', !dlna);
  byId('cast-device').disabled = !cast;
  byId('cast-name').disabled = !cast;
  byId('dlna-device').disabled = !dlna;
  byId('dlna-name').disabled = !dlna;
}

function selectedName(select, target) {
  const option = select.options[select.selectedIndex];
  target.value = option?.dataset.name || '';
}

async function scanCastDevices() {
  const select = byId('cast-device');
  const result = byId('cast-result');
  select.replaceChildren(new Option('Scanning...', ''));
  result.textContent = 'Looking for Google Cast speakers...';
  try {
    const response = await fetch('/api/cast-devices', { cache: 'no-store' });
    if (!response.ok) throw new Error(await response.text());
    const data = await response.json();
    select.replaceChildren(new Option('Choose a speaker', ''));
    data.devices.forEach(device => {
      const option = new Option(`${device.name}${device.group ? ' (group)' : ''}${device.model ? ` - ${device.model}` : ''}`, device.id);
      option.dataset.name = device.name;
      option.selected = device.id === savedCastId;
      select.add(option);
    });
    if (savedCastId && !data.devices.some(device => device.id === savedCastId)) {
      const option = new Option(`${savedCastName || 'Saved speaker'} (currently unavailable)`, savedCastId);
      option.dataset.name = savedCastName;
      option.selected = true;
      select.add(option);
    }
    selectedName(select, byId('cast-name'));
    result.textContent = data.devices.length ? `${data.devices.length} Cast speaker${data.devices.length === 1 ? '' : 's'} found.` : 'No Google Cast speakers found.';
  } catch (error) {
    result.textContent = error.message;
  }
}

async function scanDlnaDevices() {
  const select = byId('dlna-device');
  const result = byId('dlna-result');
  select.replaceChildren(new Option('Scanning...', ''));
  result.textContent = 'Looking for Sonos and DLNA speakers...';
  try {
    const response = await fetch('/api/dlna-devices', { cache: 'no-store' });
    if (!response.ok) throw new Error(await response.text());
    const data = await response.json();
    select.replaceChildren(new Option('Choose a speaker', ''));
    data.devices.forEach(device => {
      const option = new Option(`${device.name}${device.model ? ` - ${device.model}` : ''}`, device.location);
      option.dataset.name = device.name;
      option.selected = device.location === savedDlnaUrl;
      select.add(option);
    });
    if (savedDlnaUrl && !data.devices.some(device => device.location === savedDlnaUrl)) {
      const option = new Option(`${savedDlnaName || 'Saved speaker'} (currently unavailable)`, savedDlnaUrl);
      option.dataset.name = savedDlnaName;
      option.selected = true;
      select.add(option);
    }
    selectedName(select, byId('dlna-name'));
    result.textContent = data.devices.length ? `${data.devices.length} compatible speaker${data.devices.length === 1 ? '' : 's'} found.` : 'No compatible speakers found.';
  } catch (error) {
    result.textContent = error.message;
  }
}

async function testPlayback(track, button) {
  const result = byId('test-result');
  button.disabled = true;
  setNotice(result, track === 'takbeer' ? 'Starting Eid takbeer...' : 'Starting adhan...');
  try {
    const response = await fetch(track === 'takbeer' ? '/api/play/takbeer' : '/api/play', { method: 'POST' });
    setNotice(result, await responseText(response));
  } catch (error) {
    setNotice(result, error.message, true);
  } finally {
    button.disabled = false;
  }
}

async function checkUpdate() {
  const button = byId('update-button');
  button.disabled = true;
  setNotice(byId('update-result'), 'Checking for a verified update...');
  try {
    const response = await fetch('/api/update', { method: 'POST' });
    setNotice(byId('update-result'), await responseText(response));
  } catch (error) {
    setNotice(byId('update-result'), error.message, true);
    button.disabled = false;
  }
}

async function uploadAudio(kind, button) {
  const input = byId(kind === 'takbeer' ? 'takbeer-audio' : 'adhan-audio');
  const result = byId(kind === 'takbeer' ? 'takbeer-upload' : 'adhan-upload');
  if (!input.files[0]) {
    setNotice(result, 'Choose an MP3 file first.', true);
    return;
  }
  button.disabled = true;
  setNotice(result, 'Uploading recording...');
  try {
    const response = await fetch(kind === 'takbeer' ? '/api/audio/takbeer' : '/api/audio', { method: 'POST', body: input.files[0] });
    setNotice(result, await responseText(response));
    await refreshDashboard();
  } catch (error) {
    setNotice(result, error.message, true);
  } finally {
    button.disabled = false;
  }
}

function showSaveDock() {
  if (!dashboardReady) return;
  const dock = byId('save-dock');
  dock.classList.remove('hidden', 'saved', 'error');
  byId('save-message').textContent = 'You have unsaved changes';
}

function updateLocalAddressPreview() {
  const hostname = byId('device-hostname')?.value.trim().toLowerCase() || 'adhancron';
  byId('local-address-preview').textContent = `${hostname}.local`;
}

async function saveDashboard(event) {
  event.preventDefault();
  const form = event.currentTarget;
  const dock = byId('save-dock');
  const button = dock.querySelector('button');
  button.disabled = true;
  byId('save-message').textContent = 'Saving settings...';
  try {
    const response = await fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams(new FormData(form))
    });
    await responseText(response);
    dock.classList.add('saved');
    byId('save-message').textContent = 'Saved. The clock is restarting...';
  } catch (error) {
    dock.classList.add('error');
    byId('save-message').textContent = error.message;
    button.disabled = false;
  }
}

function clearRamadan() {
  byId('ramadan-start').value = '';
  byId('ramadan-end').value = '';
  byId('ramadan-status').textContent = 'Save changes to turn Ramadan mode off.';
  showSaveDock();
}

function clearEid() {
  byId('eid-fitr').value = '';
  byId('eid-adha').value = '';
  byId('eid-status').textContent = 'Save changes to clear both Eid dates.';
  showSaveDock();
}

async function scanNetworks() {
  const select = byId('ssid');
  const result = byId('wifi-result');
  select.replaceChildren(new Option('Scanning...', ''));
  result.textContent = 'Looking for nearby Wi-Fi networks...';
  try {
    const response = await fetch('/api/networks', { cache: 'no-store' });
    if (!response.ok) throw new Error(await response.text());
    const data = await response.json();
    const networks = Array.isArray(data.networks) ? data.networks : [];
    select.replaceChildren(new Option('Choose your home network', ''));
    networks.forEach(network => select.add(new Option(`${network.ssid} - ${network.secure ? 'secured' : 'open'} - ${network.signal}`, network.ssid)));
    result.textContent = networks.length ? `${networks.length} network${networks.length === 1 ? '' : 's'} found.` : 'No networks found. Try scanning again or enter a hidden network.';
  } catch (error) {
    select.replaceChildren(new Option('Scan unavailable', ''));
    result.textContent = error.message;
  }
}

function useManualNetwork(value) {
  const select = byId('ssid');
  let option = byId('manual-option');
  if (!option) {
    option = new Option('Hidden network', '');
    option.id = 'manual-option';
    select.add(option);
  }
  option.value = value;
  option.textContent = value || 'Hidden network';
  if (value) option.selected = true;
}

function usePlace(place) {
  const label = place.location_name || [place.name, place.admin1, place.country].filter(Boolean).join(', ');
  byId('latitude').value = place.latitude;
  byId('longitude').value = place.longitude;
  byId('timezone').value = place.timezone || '';
  byId('location-name').value = label;
  byId('found').value = '1';
  byId('save-location').disabled = false;
  byId('location-result').textContent = 'Location confirmed. You can now save it.';
  byId('confirmed-place').textContent = label;
  byId('confirmed-coordinates').textContent = `${Number(place.latitude).toFixed(4)}, ${Number(place.longitude).toFixed(4)}${place.timezone ? ` - ${place.timezone}` : ''}`;
  byId('location-confirmation').classList.remove('hidden');
}

function locationChanged() {
  byId('found').value = '';
  byId('save-location').disabled = true;
  byId('location-confirmation').classList.add('hidden');
}

async function findPlace() {
  const query = byId('place').value.trim();
  const result = byId('location-result');
  const postcode = query.toUpperCase().replace(/\s+/g, '');
  locationChanged();
  result.textContent = 'Searching...';
  if (!query) {
    result.textContent = 'Enter a town, city or UK postcode.';
    return;
  }
  try {
    if (/^[A-Z]{1,2}\d[A-Z\d]?\d[A-Z]{2}$/.test(postcode)) {
      const response = await fetch(`https://api.postcodes.io/postcodes/${encodeURIComponent(postcode)}`);
      if (response.ok) {
        const data = await response.json();
        if (data.result) {
          usePlace({ latitude: data.result.latitude, longitude: data.result.longitude, timezone: 'Europe/London', name: data.result.postcode, admin1: data.result.admin_district || data.result.region, country: data.result.country });
          return;
        }
      }
    }
    const response = await fetch(`https://geocoding-api.open-meteo.com/v1/search?count=5&language=en&format=json&name=${encodeURIComponent(query)}`);
    if (!response.ok) throw new Error('Location search is temporarily unavailable.');
    const data = await response.json();
    if (!data.results?.[0]) throw new Error('Location not found. Check the spelling or try your nearest town.');
    usePlace(data.results[0]);
  } catch (error) {
    result.textContent = error.message;
  }
}

async function useCoordinates() {
  const latitude = Number(byId('latitude').value);
  const longitude = Number(byId('longitude').value);
  const result = byId('location-result');
  if (!Number.isFinite(latitude) || !Number.isFinite(longitude) || latitude < -89 || latitude > 89 || longitude < -180 || longitude > 180) {
    result.textContent = 'Enter a latitude from -89 to 89 and longitude from -180 to 180.';
    return;
  }
  result.textContent = 'Checking coordinates...';
  let timezone = '';
  try {
    const response = await fetch(`https://api.open-meteo.com/v1/forecast?latitude=${latitude}&longitude=${longitude}&timezone=auto&forecast_days=1`);
    if (response.ok) timezone = (await response.json()).timezone || '';
  } catch (_) {}
  const coordinates = `${latitude.toFixed(4)}, ${longitude.toFixed(4)}`;
  usePlace({ latitude, longitude, timezone, location_name: coordinates });
}

function initDashboard() {
  document.querySelectorAll('[name="output"]').forEach(input => input.addEventListener('change', outputChanged));
  byId('cast-device').addEventListener('change', () => selectedName(byId('cast-device'), byId('cast-name')));
  byId('dlna-device').addEventListener('change', () => selectedName(byId('dlna-device'), byId('dlna-name')));
  byId('scan-cast').addEventListener('click', scanCastDevices);
  byId('scan-dlna').addEventListener('click', scanDlnaDevices);
  byId('volume').addEventListener('input', event => byId('volume-value').value = `${event.target.value}%`);
  document.querySelectorAll('[data-play]').forEach(button => button.addEventListener('click', () => testPlayback(button.dataset.play, button)));
  document.querySelectorAll('[data-upload]').forEach(button => button.addEventListener('click', () => uploadAudio(button.dataset.upload, button)));
  byId('clear-ramadan').addEventListener('click', clearRamadan);
  byId('clear-eid').addEventListener('click', clearEid);
  byId('update-button').addEventListener('click', checkUpdate);
  byId('device-hostname').addEventListener('input', updateLocalAddressPreview);
  const form = byId('settings-form');
  form.addEventListener('input', showSaveDock);
  form.addEventListener('change', showSaveDock);
  form.addEventListener('submit', saveDashboard);
  refreshDashboard(true).then(() => {
    const selected = document.querySelector('[name="output"]:checked')?.value;
    if (selected === 'cast') scanCastDevices();
    if (selected === 'dlna') scanDlnaDevices();
  });
  setInterval(() => refreshDashboard(false), 30000);
}

function initWifi() {
  byId('scan-wifi').addEventListener('click', scanNetworks);
  byId('manual-ssid').addEventListener('input', event => useManualNetwork(event.target.value));
  byId('toggle-password').addEventListener('click', () => {
    const input = byId('wifi-password');
    input.type = input.type === 'password' ? 'text' : 'password';
  });
  scanNetworks();
}

function initLocation() {
  byId('find-place').addEventListener('click', findPlace);
  byId('place').addEventListener('input', locationChanged);
  byId('place').addEventListener('keydown', event => { if (event.key === 'Enter') { event.preventDefault(); findPlace(); } });
  byId('latitude').addEventListener('input', locationChanged);
  byId('longitude').addEventListener('input', locationChanged);
  byId('use-coordinates').addEventListener('click', useCoordinates);
  byId('setup-volume').addEventListener('input', event => byId('setup-volume-value').value = `${event.target.value}%`);
}

if (page === 'dashboard') initDashboard();
if (page === 'wifi') initWifi();
if (page === 'location') initLocation();
