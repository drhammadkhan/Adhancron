const state = {
  jobs: [],
  dirty: new Set(),
};

const jobGrid = document.getElementById("jobGrid");
const emptyState = document.getElementById("emptyState");
const statusBadge = document.getElementById("statusBadge");
const versionBadge = document.getElementById("versionBadge");
const refreshBtn = document.getElementById("refreshBtn");
const saveBtn = document.getElementById("saveBtn");
const template = document.getElementById("jobCardTemplate");
const fajrRunStatus = document.getElementById("fajrRunStatus");
const nextAdhanTitle = document.getElementById("nextAdhanTitle");
const nextAdhanDetail = document.getElementById("nextAdhanDetail");
const nextAdhanCountdown = document.getElementById("nextAdhanCountdown");
const setupStatus = document.getElementById("setupStatus");
const setupHint = document.getElementById("setupHint");
const haSettingsBadge = document.getElementById("haSettingsBadge");
const haUrlInput = document.getElementById("haUrlInput");
const haEntityInput = document.getElementById("haEntityInput");
const publicBaseUrlInput = document.getElementById("publicBaseUrlInput");
const playbackMethodInput = document.getElementById("playbackMethodInput");
const googleCastHostInput = document.getElementById("googleCastHostInput");
const homeAssistantFields = document.getElementById("homeAssistantFields");
const homeAssistantTokenFields = document.getElementById("homeAssistantTokenFields");
const googleCastFields = document.getElementById("googleCastFields");
const airplayFields = document.getElementById("airplayFields");
const airplayDeviceInput = document.getElementById("airplayDeviceInput");
const scanAirplayBtn = document.getElementById("scanAirplayBtn");
const pairAirplayBtn = document.getElementById("pairAirplayBtn");
const airplayPinFields = document.getElementById("airplayPinFields");
const airplayPinInput = document.getElementById("airplayPinInput");
const finishAirplayPairBtn = document.getElementById("finishAirplayPairBtn");
const airplayStatus = document.getElementById("airplayStatus");
const dlnaFields = document.getElementById("dlnaFields");
const dlnaDeviceInput = document.getElementById("dlnaDeviceInput");
const scanDlnaBtn = document.getElementById("scanDlnaBtn");
const dlnaStatus = document.getElementById("dlnaStatus");
const latitudeInput = document.getElementById("latitudeInput");
const longitudeInput = document.getElementById("longitudeInput");
const locationNameInput = document.getElementById("locationNameInput");
const displayStyleInput = document.getElementById("displayStyleInput");
const locationSearchInput = document.getElementById("locationSearchInput");
const searchLocationBtn = document.getElementById("searchLocationBtn");
const locationResults = document.getElementById("locationResults");
const locationHint = document.getElementById("locationHint");
const haTokenInput = document.getElementById("haTokenInput");
const saveSettingsBtn = document.getElementById("saveSettingsBtn");
const eidFitrDateInput = document.getElementById("eidFitrDateInput");
const eidAdhaDateInput = document.getElementById("eidAdhaDateInput");
const eidTakbeerStartInput = document.getElementById("eidTakbeerStartInput");
const eidTakbeerEndInput = document.getElementById("eidTakbeerEndInput");
const eidTakbeerIntervalInput = document.getElementById("eidTakbeerIntervalInput");
const eidStatus = document.getElementById("eidStatus");
const takbeerAudioStatus = document.getElementById("takbeerAudioStatus");
const takbeerAudioInput = document.getElementById("takbeerAudioInput");
const uploadTakbeerBtn = document.getElementById("uploadTakbeerBtn");
const heroEyebrow = document.getElementById("heroEyebrow");
const heroTitle = document.getElementById("heroTitle");
let airplayPairingSession = "";

function setStatus(message, type = "ghost") {
  statusBadge.className = `badge badge-${type} px-4 py-3 text-xs sm:text-sm`;
  statusBadge.textContent = message;
}

function setSettingsStatus(message, type = "ghost") {
  if (!haSettingsBadge) return;
  haSettingsBadge.className = `badge badge-${type} px-4 py-3 text-xs sm:text-sm`;
  haSettingsBadge.textContent = message;
}

function syncPlaybackMethod(method) {
  const homeAssistant = method === "home_assistant";
  if (homeAssistantFields) homeAssistantFields.classList.toggle("hidden", !homeAssistant);
  if (homeAssistantTokenFields) homeAssistantTokenFields.classList.toggle("hidden", !homeAssistant);
  if (googleCastFields) googleCastFields.classList.toggle("hidden", method !== "google_cast");
  if (airplayFields) airplayFields.classList.toggle("hidden", method !== "airplay");
  if (dlnaFields) dlnaFields.classList.toggle("hidden", method !== "dlna");
}

function ensureSpeakerOption(select, value, name) {
  if (!select || !value) return;
  let option = Array.from(select.options).find((item) => item.value === value);
  if (!option) {
    option = new Option(name || value, value, true, true);
    option.dataset.name = name || value;
    select.add(option);
  }
  option.selected = true;
}

function renderSettings(settings, overrideMessage = null) {
  if (!haUrlInput || !haEntityInput || !haTokenInput) return;
  const attachedOption = playbackMethodInput?.querySelector('option[value="attached"]');
  if (attachedOption && !settings.attached_playback_available && settings.playback_method !== "attached") {
    attachedOption.remove();
  }
  haUrlInput.value = settings.ha_url || "";
  haEntityInput.value = settings.ha_entity_id || "";
  if (publicBaseUrlInput) publicBaseUrlInput.value = settings.public_base_url || "";
  if (playbackMethodInput) playbackMethodInput.value = settings.playback_method || "home_assistant";
  if (googleCastHostInput) googleCastHostInput.value = settings.google_cast_host || "";
  ensureSpeakerOption(airplayDeviceInput, settings.airplay_identifier, settings.airplay_device_name);
  ensureSpeakerOption(dlnaDeviceInput, settings.dlna_location, settings.dlna_device_name);
  syncPlaybackMethod(settings.playback_method || "home_assistant");
  if (latitudeInput) latitudeInput.value = settings.latitude || "";
  if (longitudeInput) longitudeInput.value = settings.longitude || "";
  if (locationNameInput) locationNameInput.value = settings.location_name || "";
  if (displayStyleInput) displayStyleInput.value = settings.display_style || "overview";
  if (eidFitrDateInput) eidFitrDateInput.value = settings.eid_fitr_date || "";
  if (eidAdhaDateInput) eidAdhaDateInput.value = settings.eid_adha_date || "";
  if (eidTakbeerStartInput) eidTakbeerStartInput.value = settings.eid_takbeer_start || "07:00";
  if (eidTakbeerEndInput) eidTakbeerEndInput.value = settings.eid_takbeer_end || "09:00";
  if (eidTakbeerIntervalInput) eidTakbeerIntervalInput.value = settings.eid_takbeer_interval || "15";
  if (eidStatus) {
    eidStatus.textContent = settings.eid_active
      ? `${settings.eid_kind} today. The next configured takbeer will play automatically.`
      : settings.eid_fitr_date || settings.eid_adha_date
      ? `Eid al-Fitr: ${settings.eid_fitr_date || "not set"}; Eid al-Adha: ${settings.eid_adha_date || "not set"}.`
      : "Set your locally observed Eid dates to enable the greeting and takbeer schedule.";
  }
  if (takbeerAudioStatus) {
    takbeerAudioStatus.textContent = settings.takbeer_audio_available
      ? settings.takbeer_audio_custom
        ? "Custom Eid takbeer recording ready."
        : "Bundled Eid takbeer recording ready."
      : "Eid takbeer recording is unavailable.";
  }
  if (locationHint) locationHint.textContent = `Timings are calculated locally from the saved coordinates and update each day. Time zone: ${settings.timezone || "unknown"}. Location searches use OpenStreetMap; you can also enter coordinates directly.`;
  haTokenInput.value = "";
  if (settings.playback_method === "attached") {
    setSettingsStatus(overrideMessage || "Attached speaker ready", "success");
    return;
  }
  if (settings.playback_method === "google_cast") {
    const configured = Boolean(settings.google_cast_host);
    setSettingsStatus(
      overrideMessage || (configured ? "Direct Google Cast ready" : "Set Google Cast speaker address"),
      configured ? "success" : "warning",
    );
    return;
  }
  if (settings.playback_method === "airplay") {
    const configured = Boolean(settings.airplay_identifier);
    setSettingsStatus(
      overrideMessage || (configured ? `AirPlay ready: ${settings.airplay_device_name || "speaker"}` : "Choose an AirPlay speaker"),
      configured ? "success" : "warning",
    );
    return;
  }
  if (settings.playback_method === "dlna") {
    const configured = Boolean(settings.dlna_location);
    setSettingsStatus(
      overrideMessage || (configured ? `Network speaker ready: ${settings.dlna_device_name || "DLNA device"}` : "Choose a Sonos or DLNA speaker"),
      configured ? "success" : "warning",
    );
    return;
  }
  const tokenStatus = settings.ha_token_source === "saved"
    ? "Token saved to /data"
    : settings.ha_token_source === "environment"
    ? "Token from env"
    : "No token saved";
  setSettingsStatus(overrideMessage || tokenStatus, settings.ha_token_set ? "success" : "warning");
}

function renderJobs() {
  jobGrid.innerHTML = "";
  emptyState.classList.toggle("hidden", state.jobs.length !== 0);

  state.jobs.forEach((job) => {
    const node = template.content.cloneNode(true);
    const card = node.querySelector("article");
    const title = node.querySelector(".card-title");
    const timeInput = node.querySelector('input[type="time"]');
    const toggle = node.querySelector('.toggle-success');
    const overrideToggle = node.querySelector('.override-toggle');
    const overrideDescription = node.querySelector('.override-description');
    const stateBadge = node.querySelector(".job-state");

    title.textContent = job.label;
    timeInput.value = job.time;
    toggle.checked = job.enabled;
    overrideToggle.checked = job.manual_override || false;
    syncStateBadge(stateBadge, toggle.checked);
    syncOverrideDescription(overrideDescription, overrideToggle.checked);

    timeInput.addEventListener("input", () => {
      job.time = timeInput.value;
      markJobDirty(job.id);
    });

    toggle.addEventListener("change", () => {
      job.enabled = toggle.checked;
      syncStateBadge(stateBadge, toggle.checked);
      markJobDirty(job.id);
    });

    overrideToggle.addEventListener("change", () => {
      job.manual_override = overrideToggle.checked;
      syncOverrideDescription(overrideDescription, overrideToggle.checked);
      markJobDirty(job.id);
    });

    if (state.dirty.has(job.id)) {
      card.classList.add("ring", "ring-warning", "ring-offset-2");
    }

    jobGrid.appendChild(node);
  });
  updateSaveButton();
}

function markJobDirty(jobId) {
  state.dirty.add(jobId);
  updateSaveButton();
  setStatus(`${state.dirty.size} schedule change${state.dirty.size === 1 ? "" : "s"} to save`, "warning");
}

function updateSaveButton() {
  if (!saveBtn) return;
  saveBtn.disabled = state.dirty.size === 0;
}

function syncStateBadge(node, enabled) {
  node.textContent = enabled ? "Adhan on" : "Adhan off";
  node.className = `badge badge-outline badge-lg ${enabled ? "badge-success" : "badge-error"} job-state`;
}

function syncOverrideDescription(node, manual) {
  if (!node) return;
  node.textContent = manual
    ? "Daily updates will leave this time unchanged."
    : "Daily updates can adjust this time automatically.";
}

async function loadJobs() {
  setStatus("Loading jobs...", "ghost");
  try {
    const response = await fetch("api/jobs");
    const payload = await response.json();
    if (!response.ok) {
      throw new Error(payload.detail || "Failed to load jobs");
    }
    state.jobs = payload.jobs;
    state.dirty.clear();
    renderJobs();
    setStatus("Schedule ready", "success");
  } catch (error) {
    setStatus(error.message, "error");
  }
}

async function saveJobs() {
  const dirtyJobs = state.jobs
    .filter((job) => state.dirty.has(job.id))
    .map(({ id, enabled, time, manual_override }) => ({ id, enabled, time, manual_override }));

  if (dirtyJobs.length === 0) {
    setStatus("No schedule changes to save", "info");
    return;
  }

  setStatus("Saving schedule...", "ghost");
  saveBtn.disabled = true;

  try {
    const response = await fetch("api/jobs", {
      method: "PUT",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ jobs: dirtyJobs }),
    });
    const payload = await response.json();
    if (!response.ok) {
      throw new Error(payload.detail || "Failed to save jobs");
    }
    state.jobs = payload.jobs;
    state.dirty.clear();
    renderJobs();
    updateSaveButton();
    loadDashboardStatus();
    setStatus("Schedule saved", "success");
  } catch (error) {
    setStatus(error.message, "error");
  } finally {
    saveBtn.disabled = false;
  }
}

async function loadSettings() {
  if (!haUrlInput || !haEntityInput || !haTokenInput) return;

  setSettingsStatus("Loading settings...", "ghost");
  try {
    const response = await fetch("api/settings");
    const payload = await response.json();
    if (!response.ok) {
      throw new Error(payload.detail || "Failed to load settings");
    }
    renderSettings(payload);
  } catch (error) {
    setSettingsStatus(error.message, "error");
  }
}

async function loadVersion() {
  if (!versionBadge) return;

  try {
    const response = await fetch("api/version");
    const payload = await response.json();
    if (!response.ok) {
      throw new Error(payload.detail || "Failed to load version");
    }
    versionBadge.textContent = `Version ${payload.version}`;
  } catch (error) {
    versionBadge.textContent = "Version unavailable";
  }
}

async function saveSettings() {
  if (!haUrlInput || !haEntityInput || !haTokenInput || !saveSettingsBtn) return;

  const payload = {
    playback_method: playbackMethodInput ? playbackMethodInput.value : "home_assistant",
    ha_url: haUrlInput.value,
    ha_entity_id: haEntityInput.value,
    eid_fitr_date: eidFitrDateInput ? eidFitrDateInput.value : "",
    eid_adha_date: eidAdhaDateInput ? eidAdhaDateInput.value : "",
    eid_takbeer_start: eidTakbeerStartInput ? eidTakbeerStartInput.value : "07:00",
    eid_takbeer_end: eidTakbeerEndInput ? eidTakbeerEndInput.value : "09:00",
    eid_takbeer_interval: eidTakbeerIntervalInput ? eidTakbeerIntervalInput.value : "15",
  };
  if (publicBaseUrlInput?.value.trim()) payload.public_base_url = publicBaseUrlInput.value.trim();
  if (payload.playback_method === "google_cast") {
    payload.google_cast_host = googleCastHostInput ? googleCastHostInput.value.trim() : "";
  } else if (payload.playback_method === "airplay") {
    const option = airplayDeviceInput?.options[airplayDeviceInput.selectedIndex];
    payload.airplay_identifier = airplayDeviceInput ? airplayDeviceInput.value : "";
    payload.airplay_device_name = option?.dataset.name || option?.textContent || "";
  } else if (payload.playback_method === "dlna") {
    const option = dlnaDeviceInput?.options[dlnaDeviceInput.selectedIndex];
    payload.dlna_location = dlnaDeviceInput ? dlnaDeviceInput.value : "";
    payload.dlna_device_name = option?.dataset.name || option?.textContent || "";
  }
  const latitude = latitudeInput ? latitudeInput.value.trim() : "";
  const longitude = longitudeInput ? longitudeInput.value.trim() : "";
  if (latitude || longitude) {
    payload.latitude = latitude;
    payload.longitude = longitude;
    payload.location_name = locationNameInput ? locationNameInput.value.trim() : "";
  }
  if (displayStyleInput) payload.display_style = displayStyleInput.value;
  if (haTokenInput.value.trim()) {
    payload.ha_token = haTokenInput.value;
  }

  setSettingsStatus("Saving settings...", "ghost");
  saveSettingsBtn.disabled = true;

  try {
    const response = await fetch("api/settings", {
      method: "PUT",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(payload),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.detail || "Failed to save settings");
    }
    renderSettings(data, data.prayer_times_message || "Settings saved");
    loadDashboardStatus();
  } catch (error) {
    setSettingsStatus(error.message, "error");
  } finally {
    saveSettingsBtn.disabled = false;
  }
}

async function scanSpeakers(kind) {
  const airplay = kind === "airplay";
  const select = airplay ? airplayDeviceInput : dlnaDeviceInput;
  const status = airplay ? airplayStatus : dlnaStatus;
  const savedValue = select?.value || "";
  if (!select || !status) return;
  select.innerHTML = `<option value="">Scanning...</option>`;
  status.textContent = "Looking for speakers on this network...";
  try {
    const response = await fetch(`api/speakers/${kind}`, { cache: "no-store" });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.detail || "Speaker scan failed");
    select.innerHTML = `<option value="">Choose a speaker</option>`;
    payload.devices.forEach((device) => {
      const value = airplay ? device.identifier : device.location;
      const option = new Option(
        `${device.name}${device.model ? ` - ${device.model}` : ""}`,
        value,
      );
      option.dataset.name = device.name;
      option.dataset.pairingRequired = device.pairing_required ? "true" : "false";
      if (value === savedValue) option.selected = true;
      select.add(option);
    });
    if (savedValue && !Array.from(select.options).some((item) => item.value === savedValue)) {
      ensureSpeakerOption(select, savedValue, "Saved speaker (currently unavailable)");
    }
    status.textContent = payload.devices.length
      ? `${payload.devices.length} compatible speaker${payload.devices.length === 1 ? "" : "s"} found.`
      : "No compatible speakers were found. Confirm the speaker is online and on this network.";
  } catch (error) {
    select.innerHTML = `<option value="">Scan unavailable</option>`;
    status.textContent = error.message;
  }
}

async function startAirplayPairing() {
  if (!airplayDeviceInput?.value || !airplayStatus) {
    if (airplayStatus) airplayStatus.textContent = "Choose an AirPlay speaker first.";
    return;
  }
  airplayStatus.textContent = "Starting AirPlay pairing...";
  try {
    const response = await fetch("api/speakers/airplay/pair/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ identifier: airplayDeviceInput.value }),
    });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.detail || "Unable to start AirPlay pairing");
    if (payload.paired || !payload.pairing_required) {
      airplayStatus.textContent = "This AirPlay speaker is ready and does not require a new PIN.";
      airplayPinFields?.classList.add("hidden");
      return;
    }
    airplayPairingSession = payload.session_id;
    airplayPinFields?.classList.remove("hidden");
    airplayStatus.textContent = payload.device_provides_pin
      ? "Enter the PIN shown by the speaker or TV."
      : "Enter the displayed PIN on the receiver, then type the same PIN here.";
    airplayPinInput?.focus();
  } catch (error) {
    airplayStatus.textContent = error.message;
  }
}

async function finishAirplayPairing() {
  if (!airplayPairingSession || !airplayPinInput?.value.trim() || !airplayStatus) return;
  airplayStatus.textContent = "Finishing AirPlay pairing...";
  try {
    const response = await fetch("api/speakers/airplay/pair/finish", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        session_id: airplayPairingSession,
        pin: airplayPinInput.value.trim(),
      }),
    });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.detail || "AirPlay pairing failed");
    airplayPairingSession = "";
    airplayPinInput.value = "";
    airplayPinFields?.classList.add("hidden");
    airplayStatus.textContent = "AirPlay pairing complete. Save Settings to use this speaker.";
  } catch (error) {
    airplayStatus.textContent = error.message;
  }
}

async function loadFajrStatus() {
  if (!fajrRunStatus) return;

  try {
    const response = await fetch("api/fajr-status");
    const data = await response.json();

    if (!response.ok) {
      throw new Error(data.detail || "Failed to load Fajr status");
    }

    if (!data.lastRun) {
      fajrRunStatus.className = "mt-3 rounded-xl bg-warning/15 border border-warning/30 px-4 py-3 text-sm text-warning-content";
      fajrRunStatus.textContent = "Prayer-time updates have not run yet. Save your location in Settings to create the schedule.";
      return;
    }

    const lastRunDate = new Date(data.lastRun);
    const localTime = lastRunDate.toLocaleString();
    const ageMs = Date.now() - lastRunDate.getTime();
    const stale = Number.isFinite(ageMs) && ageMs > 24 * 60 * 60 * 1000;

    const icon = !data.ok ? "⚠️" : stale ? "⚠️" : "✓";
    const stateText = !data.ok
      ? "Prayer-time update needs attention"
      : stale
      ? "Prayer times need an update"
      : "Prayer times are up to date";

    const styleClass = !data.ok
      ? "bg-error/15 border-error/30 text-slate-800"
      : stale
      ? "bg-warning/15 border-warning/30 text-slate-800"
      : "bg-success/15 border-success/30 text-slate-800";

    fajrRunStatus.className = `mt-3 rounded-xl px-4 py-3 text-sm border ${styleClass}`;
    fajrRunStatus.textContent = `${icon} ${stateText} — last checked ${localTime}.`;
  } catch (error) {
    fajrRunStatus.className = "mt-3 rounded-xl bg-error/15 border border-error/30 px-4 py-3 text-sm text-slate-800";
    fajrRunStatus.textContent = `Unable to check prayer-time updates: ${error.message}`;
  }
}

function setupChip(label, ready) {
  const chip = document.createElement("span");
  chip.className = `badge px-3 py-3 text-sm ${ready ? "badge-success" : "badge-warning"}`;
  chip.textContent = `${ready ? "Ready" : "Needs setup"}: ${label}`;
  return chip;
}

function formatCountdown(when) {
  const remainingMs = new Date(when).getTime() - Date.now();
  if (!Number.isFinite(remainingMs) || remainingMs <= 0) return "Soon";
  const totalMinutes = Math.ceil(remainingMs / 60000);
  const hours = Math.floor(totalMinutes / 60);
  const minutes = totalMinutes % 60;
  if (hours === 0) return `in ${minutes} min`;
  return `in ${hours}h ${minutes}m`;
}

async function loadDashboardStatus() {
  if (!nextAdhanTitle || !setupStatus) return;
  try {
    const response = await fetch("api/dashboard-status");
    const data = await response.json();
    if (!response.ok) throw new Error(data.detail || "Unable to load dashboard status");

    const eid = data.eid || {};
    const showTakbeer = eid.active && eid.next_takbeer && eid.seconds_until_takbeer <= 10 * 60;
    if (heroEyebrow) heroEyebrow.textContent = eid.active ? eid.kind : "Daily prayer times and adhan playback";
    if (heroTitle) heroTitle.textContent = eid.active ? "Eid Mubarak." : "Your home prayer schedule.";
    const next = data.next_adhan;
    if (showTakbeer) {
      const when = new Date(eid.next_takbeer);
      nextAdhanTitle.textContent = `Eid Takbeer at ${when.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}`;
      nextAdhanDetail.textContent = `${eid.kind} on your configured speaker. Normal prayer adhans remain enabled.`;
      nextAdhanCountdown.textContent = formatCountdown(eid.next_takbeer);
    } else if (next) {
      nextAdhanTitle.textContent = `${next.label} at ${next.time}`;
      nextAdhanDetail.textContent = `${next.is_tomorrow ? "Tomorrow" : "Today"} on your configured speaker.`;
      nextAdhanCountdown.textContent = formatCountdown(next.when);
    } else {
      nextAdhanTitle.textContent = "No adhan is scheduled";
      nextAdhanDetail.textContent = "Turn on at least one prayer in the schedule below.";
      nextAdhanCountdown.textContent = "";
    }

    const setup = data.setup;
    setupStatus.innerHTML = "";
    setupStatus.append(
      setupChip("Location", setup.location_ready),
      setupChip(setup.playback_label, setup.playback_ready),
      setupChip("Prayer schedule", setup.schedule_ready),
    );
    const ready = setup.location_ready && setup.playback_ready && setup.schedule_ready;
    setupHint.textContent = ready
      ? `${setup.enabled_prayers} daily adhans are enabled.`
      : "Open Settings to finish the items marked Needs setup.";
  } catch (error) {
    nextAdhanTitle.textContent = "Schedule status unavailable";
    nextAdhanDetail.textContent = error.message;
    nextAdhanCountdown.textContent = "";
    setupStatus.innerHTML = "";
    setupHint.textContent = "Refresh the dashboard to try again.";
  }
}

async function uploadTakbeer() {
  const file = takbeerAudioInput?.files?.[0];
  if (!file) {
    setSettingsStatus("Choose an Eid takbeer MP3 first", "warning");
    return;
  }
  if (!uploadTakbeerBtn) return;
  uploadTakbeerBtn.disabled = true;
  setSettingsStatus("Uploading Eid takbeer...", "ghost");
  try {
    const response = await fetch("api/audio/takbeer", { method: "POST", body: file });
    const data = await response.json();
    if (!response.ok) throw new Error(data.detail || "Unable to upload the takbeer recording");
    setSettingsStatus("Eid takbeer recording saved", "success");
    if (takbeerAudioInput) takbeerAudioInput.value = "";
    await loadSettings();
  } catch (error) {
    setSettingsStatus(error.message, "error");
  } finally {
    uploadTakbeerBtn.disabled = false;
  }
}

async function loadPrayerTimes() {
  const tableBody = document.getElementById("prayerTableBody");
  const prayerCards = document.getElementById("prayerCards");
  if (!tableBody && !prayerCards) return;

  try {
    const response = await fetch("api/prayer-times");
    const data = await response.json();
    if (data.error) throw new Error(data.error);

    if (tableBody) tableBody.innerHTML = "";
    if (prayerCards) prayerCards.innerHTML = "";

    // Sort logic: 2026 -> months in order -> days in order
    const months = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
    
    Object.keys(data).sort().forEach(year => {
      const yearData = data[year];
      months.forEach(month => {
        if (!yearData[month]) return;
        const monthData = yearData[month];
        
        Object.keys(monthData).sort((a, b) => parseInt(a) - parseInt(b)).forEach(day => {
          const times = monthData[day];
          const row = document.createElement("tr");
          row.className = "hover";
          row.innerHTML = `
            <td class="font-medium text-slate-700">${day} ${month} ${year}</td>
            <td>${times.Fajr}</td>
            <td>${times.Dhuhr}</td>
            <td>${times.Asr}</td>
            <td>${times.Maghrib}</td>
            <td>${times.Isha}</td>
          `;
          if (tableBody) tableBody.appendChild(row);

          const card = document.createElement("article");
          card.className = "rounded-2xl border border-white/70 bg-white/70 p-4 shadow";
          card.innerHTML = `
            <h3 class="font-semibold text-slate-800 mb-2">${day} ${month} ${year}</h3>
            <div class="grid grid-cols-2 gap-y-1 text-sm">
              <span class="text-slate-500">Fajr</span><span class="font-medium">${times.Fajr}</span>
              <span class="text-slate-500">Dhuhr</span><span class="font-medium">${times.Dhuhr}</span>
              <span class="text-slate-500">Asr</span><span class="font-medium">${times.Asr}</span>
              <span class="text-slate-500">Maghrib</span><span class="font-medium">${times.Maghrib}</span>
              <span class="text-slate-500">Isha</span><span class="font-medium">${times.Isha}</span>
            </div>
          `;
          if (prayerCards) prayerCards.appendChild(card);
        });
      });
    });
  } catch (error) {
    if (tableBody) tableBody.innerHTML = `<tr><td colspan="6" class="text-center py-8 text-error">${error.message}</td></tr>`;
    if (prayerCards) {
      prayerCards.innerHTML = `<div class="rounded-2xl border border-error/30 bg-error/10 p-4 text-sm text-error">${error.message}</div>`;
    }
  }
}

refreshBtn.addEventListener("click", () => {
  loadJobs();
  loadSettings();
  loadVersion();
  loadPrayerTimes();
  loadFajrStatus();
  loadDashboardStatus();
});
saveBtn.addEventListener("click", saveJobs);
if (saveSettingsBtn) saveSettingsBtn.addEventListener("click", saveSettings);
if (uploadTakbeerBtn) uploadTakbeerBtn.addEventListener("click", uploadTakbeer);
if (scanAirplayBtn) scanAirplayBtn.addEventListener("click", () => scanSpeakers("airplay"));
if (scanDlnaBtn) scanDlnaBtn.addEventListener("click", () => scanSpeakers("dlna"));
if (pairAirplayBtn) pairAirplayBtn.addEventListener("click", startAirplayPairing);
if (finishAirplayPairBtn) finishAirplayPairBtn.addEventListener("click", finishAirplayPairing);
if (playbackMethodInput) {
  playbackMethodInput.addEventListener("change", () => {
    syncPlaybackMethod(playbackMethodInput.value);
    if (playbackMethodInput.value === "google_cast") {
      setSettingsStatus("Save the Google Cast speaker address before testing", "warning");
    } else if (playbackMethodInput.value === "airplay") {
      setSettingsStatus("Scan, choose, and pair an AirPlay speaker before testing", "warning");
    } else if (playbackMethodInput.value === "dlna") {
      setSettingsStatus("Scan and choose a Sonos or DLNA speaker before testing", "warning");
    }
  });
}
function chooseLocation(result) {
  if (latitudeInput) latitudeInput.value = Number(result.latitude).toFixed(5);
  if (longitudeInput) longitudeInput.value = Number(result.longitude).toFixed(5);
  if (locationNameInput) locationNameInput.value = result.name || "";
  if (locationResults) {
    locationResults.innerHTML = "";
    locationResults.classList.add("hidden");
  }
  setSettingsStatus("Location selected. Save Settings to generate timings.", "success");
}

function showLocationResults(results) {
  if (!locationResults) return;
  locationResults.innerHTML = "";
  locationResults.classList.remove("hidden");
  if (results.length === 0) {
    locationResults.textContent = "No matching location found. Try a more specific search or enter coordinates directly.";
    return;
  }
  results.forEach((result) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "btn btn-outline h-auto min-h-12 w-full justify-start whitespace-normal px-3 py-2 text-left text-sm normal-case";
    button.textContent = result.name;
    button.addEventListener("click", () => chooseLocation(result));
    locationResults.appendChild(button);
  });
}

async function searchLocation() {
  const query = locationSearchInput?.value.trim() || "";
  if (query.length < 2) {
    setSettingsStatus("Enter a postcode, town, or address to search", "warning");
    locationSearchInput?.focus();
    return;
  }
  if (!searchLocationBtn) return;
  searchLocationBtn.disabled = true;
  setSettingsStatus("Finding locations...", "ghost");
  try {
    const response = await fetch("api/location-search", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ query }),
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.detail || "Unable to search for a location");
    showLocationResults(data.results || []);
    setSettingsStatus(data.results?.length ? "Choose the matching location" : "No matching location found", data.results?.length ? "success" : "warning");
  } catch (error) {
    setSettingsStatus(error.message, "error");
  } finally {
    searchLocationBtn.disabled = false;
  }
}

if (searchLocationBtn) searchLocationBtn.addEventListener("click", searchLocation);
if (locationSearchInput) {
  locationSearchInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      searchLocation();
    }
  });
}

loadJobs();
loadSettings();
loadVersion();
loadPrayerTimes();
loadFajrStatus();
loadDashboardStatus();
