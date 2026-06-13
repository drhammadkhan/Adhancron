const state = {
  jobs: [],
  dirty: new Set(),
};

const jobGrid = document.getElementById("jobGrid");
const emptyState = document.getElementById("emptyState");
const statusBadge = document.getElementById("statusBadge");
const refreshBtn = document.getElementById("refreshBtn");
const saveBtn = document.getElementById("saveBtn");
const template = document.getElementById("jobCardTemplate");
const fajrRunStatus = document.getElementById("fajrRunStatus");
const haSettingsBadge = document.getElementById("haSettingsBadge");
const haUrlInput = document.getElementById("haUrlInput");
const haEntityInput = document.getElementById("haEntityInput");
const publicBaseUrlInput = document.getElementById("publicBaseUrlInput");
const haTokenInput = document.getElementById("haTokenInput");
const saveSettingsBtn = document.getElementById("saveSettingsBtn");

function setStatus(message, type = "ghost") {
  statusBadge.className = `badge badge-${type} px-4 py-3 text-xs sm:text-sm`;
  statusBadge.textContent = message;
}

function prayerMeta(job) {
  return `${job.audio_url} • volume ${job.volume}`;
}

function setSettingsStatus(message, type = "ghost") {
  if (!haSettingsBadge) return;
  haSettingsBadge.className = `badge badge-${type} px-4 py-3 text-xs sm:text-sm`;
  haSettingsBadge.textContent = message;
}

function renderSettings(settings, overrideMessage = null) {
  if (!haUrlInput || !haEntityInput || !haTokenInput) return;
  haUrlInput.value = settings.ha_url || "";
  haEntityInput.value = settings.ha_entity_id || "";
  if (publicBaseUrlInput) publicBaseUrlInput.value = settings.public_base_url || "";
  haTokenInput.value = "";
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
    const meta = node.querySelector(".job-meta");
    const timeInput = node.querySelector('input[type="time"]');
    const toggle = node.querySelector('.toggle-success');
    const overrideToggle = node.querySelector('.override-toggle');
    const overrideBadge = node.querySelector('.override-status-badge');
    const stateBadge = node.querySelector(".job-state");

    title.textContent = job.label;
    meta.textContent = prayerMeta(job);
    timeInput.value = job.time;
    toggle.checked = job.enabled;
    overrideToggle.checked = job.manual_override || false;
    syncStateBadge(stateBadge, toggle.checked);
    syncOverrideBadge(overrideBadge, overrideToggle.checked);

    timeInput.addEventListener("input", () => {
      job.time = timeInput.value;
      state.dirty.add(job.id);
      setStatus(`${state.dirty.size} change(s) pending`, "warning");
    });

    toggle.addEventListener("change", () => {
      job.enabled = toggle.checked;
      state.dirty.add(job.id);
      syncStateBadge(stateBadge, toggle.checked);
      setStatus(`${state.dirty.size} change(s) pending`, "warning");
    });

    overrideToggle.addEventListener("change", () => {
      job.manual_override = overrideToggle.checked;
      state.dirty.add(job.id);
      syncOverrideBadge(overrideBadge, overrideToggle.checked);
      setStatus(`${state.dirty.size} change(s) pending`, "warning");
    });

    if (state.dirty.has(job.id)) {
      card.classList.add("ring", "ring-warning", "ring-offset-2");
    }

    jobGrid.appendChild(node);
  });
}

function syncStateBadge(node, enabled) {
  node.textContent = enabled ? "Enabled" : "Disabled";
  node.className = `badge badge-outline badge-lg ${enabled ? "badge-success" : "badge-error"} job-state`;
}

function syncOverrideBadge(node, manual) {
  node.textContent = manual ? "Manual" : "Auto";
  node.className = `badge badge-sm ${manual ? "badge-warning" : "badge-ghost opacity-50"} override-status-badge`;
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
    setStatus(`Loaded ${payload.jobs.length} job(s)`, "success");
  } catch (error) {
    setStatus(error.message, "error");
  }
}

async function saveJobs() {
  const dirtyJobs = state.jobs
    .filter((job) => state.dirty.has(job.id))
    .map(({ id, enabled, time, manual_override }) => ({ id, enabled, time, manual_override }));

  if (dirtyJobs.length === 0) {
    setStatus("No changes to apply", "info");
    return;
  }

  setStatus("Applying changes...", "ghost");
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
    setStatus("Crontab updated", "success");
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

async function saveSettings() {
  if (!haUrlInput || !haEntityInput || !haTokenInput || !saveSettingsBtn) return;

  const payload = {
    ha_url: haUrlInput.value,
    ha_entity_id: haEntityInput.value,
    public_base_url: publicBaseUrlInput ? publicBaseUrlInput.value : "",
  };
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
    renderSettings(data, "Settings saved");
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
      fajrRunStatus.textContent = "Fajr auto-update has not run yet (no status file found).";
      return;
    }

    const lastRunDate = new Date(data.lastRun);
    const localTime = lastRunDate.toLocaleString();
    const ageMs = Date.now() - lastRunDate.getTime();
    const stale = Number.isFinite(ageMs) && ageMs > 24 * 60 * 60 * 1000;

    const icon = !data.ok ? "🚨" : stale ? "⚠️" : "✅";
    const stateText = !data.ok ? "Failed" : stale ? "Success (stale)" : "Success";
    const summary = data.summary ? ` — ${data.summary.split("\n")[0]}` : "";

    const styleClass = !data.ok
      ? "bg-error/15 border-error/30 text-slate-800"
      : stale
      ? "bg-warning/15 border-warning/30 text-slate-800"
      : "bg-success/15 border-success/30 text-slate-800";

    fajrRunStatus.className = `mt-3 rounded-xl px-4 py-3 text-sm border ${styleClass}`;
    fajrRunStatus.textContent = `${icon} Last Fajr auto-update: ${stateText} at ${localTime}${summary}`;
  } catch (error) {
    fajrRunStatus.className = "mt-3 rounded-xl bg-error/15 border border-error/30 px-4 py-3 text-sm text-slate-800";
    fajrRunStatus.textContent = `Unable to load Fajr run status: ${error.message}`;
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
  loadPrayerTimes();
  loadFajrStatus();
});
saveBtn.addEventListener("click", saveJobs);
if (saveSettingsBtn) saveSettingsBtn.addEventListener("click", saveSettings);

loadJobs();
loadSettings();
loadPrayerTimes();
loadFajrStatus();
