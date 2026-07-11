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
const latitudeInput = document.getElementById("latitudeInput");
const longitudeInput = document.getElementById("longitudeInput");
const locationSearchInput = document.getElementById("locationSearchInput");
const searchLocationBtn = document.getElementById("searchLocationBtn");
const locationResults = document.getElementById("locationResults");
const locationHint = document.getElementById("locationHint");
const haTokenInput = document.getElementById("haTokenInput");
const saveSettingsBtn = document.getElementById("saveSettingsBtn");

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
  const directCast = method === "google_cast";
  if (homeAssistantFields) homeAssistantFields.classList.toggle("hidden", directCast);
  if (homeAssistantTokenFields) homeAssistantTokenFields.classList.toggle("hidden", directCast);
  if (googleCastFields) googleCastFields.classList.toggle("hidden", !directCast);
}

function renderSettings(settings, overrideMessage = null) {
  if (!haUrlInput || !haEntityInput || !haTokenInput) return;
  haUrlInput.value = settings.ha_url || "";
  haEntityInput.value = settings.ha_entity_id || "";
  if (publicBaseUrlInput) publicBaseUrlInput.value = settings.public_base_url || "";
  if (playbackMethodInput) playbackMethodInput.value = settings.playback_method || "home_assistant";
  if (googleCastHostInput) googleCastHostInput.value = settings.google_cast_host || "";
  syncPlaybackMethod(settings.playback_method || "home_assistant");
  if (latitudeInput) latitudeInput.value = settings.latitude || "";
  if (longitudeInput) longitudeInput.value = settings.longitude || "";
  if (locationHint) locationHint.textContent = `Timings are calculated locally from the saved coordinates and update each day. Container timezone: ${settings.timezone || "unknown"}. Location searches use OpenStreetMap; you can also enter coordinates directly.`;
  haTokenInput.value = "";
  if (settings.playback_method === "google_cast") {
    const configured = Boolean(settings.google_cast_host);
    setSettingsStatus(
      overrideMessage || (configured ? "Direct Google Cast ready" : "Set Google Cast speaker address"),
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
  };
  if (publicBaseUrlInput?.value.trim()) payload.public_base_url = publicBaseUrlInput.value.trim();
  if (payload.playback_method === "google_cast") {
    payload.google_cast_host = googleCastHostInput ? googleCastHostInput.value.trim() : "";
  }
  const latitude = latitudeInput ? latitudeInput.value.trim() : "";
  const longitude = longitudeInput ? longitudeInput.value.trim() : "";
  if (latitude || longitude) {
    payload.latitude = latitude;
    payload.longitude = longitude;
  }
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

    const next = data.next_adhan;
    if (next) {
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
if (playbackMethodInput) {
  playbackMethodInput.addEventListener("change", () => {
    syncPlaybackMethod(playbackMethodInput.value);
    if (playbackMethodInput.value === "google_cast") {
      setSettingsStatus("Save the Google Cast speaker address before testing", "warning");
    }
  });
}
function chooseLocation(result) {
  if (latitudeInput) latitudeInput.value = Number(result.latitude).toFixed(5);
  if (longitudeInput) longitudeInput.value = Number(result.longitude).toFixed(5);
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
