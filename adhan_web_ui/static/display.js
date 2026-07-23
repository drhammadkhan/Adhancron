const shell = document.getElementById("clock");
const timeNode = document.getElementById("current-time");
const dateNode = document.getElementById("current-date");
const listNode = document.getElementById("prayer-list");
const setupNode = document.getElementById("setup-state");
let serverOffset = 0;
let nextPrayer = null;

function dashboardAddress() {
  return `${window.location.hostname}${window.location.port ? `:${window.location.port}` : ""}`;
}

function updateClock() {
  const now = new Date(Date.now() + serverOffset);
  const hours = String(now.getHours()).padStart(2, "0");
  const minutes = String(now.getMinutes()).padStart(2, "0");
  const seconds = String(now.getSeconds()).padStart(2, "0");
  timeNode.innerHTML = `${hours}:${minutes}:<span>${seconds}</span>`;
  dateNode.textContent = new Intl.DateTimeFormat("en-GB", {
    weekday: "long", day: "numeric", month: "long", year: "numeric",
  }).format(now);

  if (nextPrayer?.when) {
    const remaining = Math.max(0, new Date(nextPrayer.when).getTime() - now.getTime());
    const totalMinutes = Math.floor(remaining / 60000);
    const countdown = totalMinutes >= 60
      ? `${Math.floor(totalMinutes / 60)} hr ${totalMinutes % 60} min`
      : `${totalMinutes} min`;
    document.getElementById("countdown").textContent = `In ${countdown}`;
  }
}

function renderPrayers(prayers, next) {
  listNode.replaceChildren();
  prayers.forEach((prayer) => {
    const row = document.createElement("div");
    row.className = `prayer-row${prayer.name === next?.label ? " next" : ""}${prayer.enabled ? "" : " off"}`;
    const name = document.createElement("span");
    const time = document.createElement("strong");
    name.textContent = prayer.name;
    time.textContent = prayer.time;
    row.append(name, time);
    listNode.appendChild(row);
  });
}

function renderStatus(status) {
  serverOffset = new Date(status.now).getTime() - Date.now();
  nextPrayer = status.next_adhan;
  shell.dataset.face = status.display_style === "focus" ? "focus" : "overview";
  document.getElementById("location").textContent = status.location || "Location not configured";
  const address = status.dashboard_address || dashboardAddress();
  document.getElementById("dashboard-address").textContent = `Settings: ${address}`;
  document.getElementById("setup-address").textContent = address;
  document.getElementById("version").textContent = `Adhancron ${status.version || ""}`;

  if (status.eid?.active) {
    document.getElementById("next-eyebrow").textContent = "Eid Mubarak";
  } else {
    document.getElementById("next-eyebrow").textContent = "Next prayer";
  }
  document.getElementById("next-name").textContent = nextPrayer?.label || "Schedule ready";
  document.getElementById("next-time").textContent = nextPrayer?.time || "--:--";
  renderPrayers(status.prayers || [], nextPrayer);
  setupNode.classList.toggle("hidden", Boolean(status.setup_ready));
  document.getElementById("network-label").textContent = "Connected";
  document.querySelector(".status-dot").style.background = "#47c597";
  updateClock();
}

async function refreshStatus() {
  try {
    const response = await fetch("/api/display-status", { cache: "no-store" });
    const status = await response.json();
    if (!response.ok) throw new Error(status.detail || "Clock status unavailable");
    renderStatus(status);
  } catch (error) {
    document.getElementById("network-label").textContent = "Reconnecting";
    document.querySelector(".status-dot").style.background = "#e6b967";
  }
}

updateClock();
refreshStatus();
setInterval(updateClock, 1000);
setInterval(refreshStatus, 30000);
