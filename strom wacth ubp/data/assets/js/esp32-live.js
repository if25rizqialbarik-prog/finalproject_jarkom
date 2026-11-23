// =====================================================
//  UBP Storm Watch - ambil data ESP32 dan isi dashboard
// =====================================================

// Jika dashboard dibuka DARI ESP32 (http://IP-ESP32), biarkan kosong "".
// Jika dibuka lewat Live Server, isi dengan IP ESP32, contoh: "http://192.168.1.50"
const ESP32_URL = "";

const POLL_MS = 3000;          // ambil data tiap 3 detik
const OFFLINE_AFTER_MS = 10000; // dianggap offline jika gagal > 10 detik
const ACTIVE_STORM_SEC = 600;  // status "AKTIF" jika petir < 10 menit lalu
const MAX_RECORDS = 20;

let lastOkTime = 0;
let prevTotalPetir = null;
const lightningRecords = [];
const weatherRecords = [];

// ---------- helper ----------
function setCard(sectionId, label, value, note) {
    const section = document.getElementById(sectionId);
    if (!section) return;

    section.querySelectorAll(".stat-card").forEach((card) => {
        const title = card.querySelector(".stat-top span");
        if (!title || title.textContent.trim() !== label) return;

        const h2 = card.querySelector("h2");
        const small = h2.querySelector("small");

        if (small) {
            h2.firstChild.textContent = value + " ";
        } else {
            h2.textContent = value;
        }

        if (note !== undefined) {
            const noteEl = card.querySelector(":scope > small");
            if (noteEl) noteEl.textContent = note;
        }
    });
}

function setTableBody(sectionId, tbodySelector, rowsHtml, emptyHtml) {
    const section = document.getElementById(sectionId);
    if (!section) return;
    const tbody = section.querySelector(tbodySelector);
    if (!tbody) return;
    tbody.innerHTML = rowsHtml || emptyHtml;
}

function clock(d) {
    return d.toLocaleTimeString("id-ID", { hour12: false });
}

function ago(sec) {
    if (sec < 60) return sec + " detik lalu";
    if (sec < 3600) return Math.floor(sec / 60) + " menit lalu";
    return Math.floor(sec / 3600) + " jam lalu";
}

// ---------- tampilan online / offline ----------
function setConnection(online) {
    const live = document.querySelector(".live-indicator");
    if (live) {
        live.lastChild.textContent = online ? " LIVE" : " DEVICE OFFLINE";
    }

    const sysSmall = document.querySelector(".system-info small");
    if (sysSmall) {
        sysSmall.textContent = online ? "ESP32 terhubung" : "ESP32 tidak terhubung";
    }

    setCard("overview", "Device Status", online ? "ONLINE" : "OFFLINE",
        online ? "Data diterima" : "Tidak ada respon");

    const on = document.getElementById("onlineDevices");
    const off = document.getElementById("offlineDevices");
    const total = document.getElementById("totalDevices");
    if (total) total.textContent = "1";
    if (on) on.textContent = online ? "1" : "0";
    if (off) off.textContent = online ? "0" : "1";
}

// ---------- render data ----------
function render(d) {
    const now = new Date();

    // OVERVIEW
    setCard("overview", "Total Lightning", d.totalPetir,
        d.totalPetir > 0 ? "Total deteksi sejak perangkat menyala" : "Belum ada deteksi");
    setCard("overview", "Temperature", d.bmpOk ? d.suhu.toFixed(1) : "—",
        d.bmpOk ? "Sensor BMP280" : "BMP280 tidak terdeteksi");
    setCard("overview", "Humidity", "—", "BMP280 tidak mengukur kelembapan");

    // WEATHER
    setCard("weather", "Temperature", d.bmpOk ? d.suhu.toFixed(1) : "—", "Sensor BMP280");
    setCard("weather", "Air Pressure", d.bmpOk ? d.tekanan.toFixed(1) : "—", "Sensor BMP280");
    setCard("weather", "Humidity", "—", "Tidak tersedia");
    setCard("weather", "Wind Speed", "—", "Tidak tersedia");

    // LIGHTNING
    setCard("lightning", "Total Deteksi Petir", d.totalPetir,
        "Disturber terdeteksi: " + d.totalDisturber);

    if (d.totalPetir > 0) {
        setCard("lightning", "Last Detection", ago(d.petirAgoSec), "Jarak " + d.jarak + " km");
        setCard("lightning", "Lightning Intensity", d.energi, "Energi mentah AS3935");
    }

    const aktif = d.totalPetir > 0 && d.petirAgoSec < ACTIVE_STORM_SEC;
    setCard("lightning", "Detection Status", d.petirOk ? (aktif ? "AKTIF" : "AMAN") : "ERROR",
        d.petirOk ? "Sensor AS3935 siap" : "AS3935 tidak terdeteksi");

    // catat petir baru
    if (prevTotalPetir !== null && d.totalPetir > prevTotalPetir) {
        lightningRecords.unshift({
            time: now,
            energi: d.energi,
            jarak: d.jarak,
        });
        if (lightningRecords.length > MAX_RECORDS) lightningRecords.pop();
    }
    prevTotalPetir = d.totalPetir;

    const lightningRows = lightningRecords
        .map((r) => `<tr>
            <td>${clock(r.time)}</td>
            <td>${r.energi}</td>
            <td>${r.jarak} km</td>
            <td>—</td></tr>`)
        .join("");
    setTableBody("lightning", "tbody", lightningRows,
        `<tr><td colspan="4" class="text-center">Belum ada riwayat deteksi petir</td></tr>`);

    // catat cuaca
    if (d.bmpOk) {
        weatherRecords.unshift({ time: now, suhu: d.suhu, tekanan: d.tekanan });
        if (weatherRecords.length > MAX_RECORDS) weatherRecords.pop();
    }

    const weatherRows = weatherRecords
        .map((r) => `<tr>
            <td>${clock(r.time)}</td>
            <td>${r.suhu.toFixed(1)} °C</td>
            <td>—</td>
            <td>${r.tekanan.toFixed(1)} hPa</td>
            <td>—</td></tr>`)
        .join("");
    setTableBody("weather", "tbody", weatherRows,
        `<tr><td colspan="5" class="text-center">Belum ada riwayat pengukuran cuaca</td></tr>`);

    // DEVICES
    const last = document.getElementById("lastCommunication");
    if (last) last.textContent = clock(now);

    const deviceBody = document.getElementById("deviceTableBody");
    if (deviceBody) {
        deviceBody.innerHTML = `<tr>
            <td>${d.deviceId}</td>
            <td>${d.deviceName}</td>
            <td>WiFi (${d.rssi} dBm)</td>
            <td>${clock(now)}</td>
            <td>BMP280: ${d.bmpOk ? "OK" : "ERROR"} | AS3935: ${d.petirOk ? "OK" : "ERROR"}</td>
        </tr>`;
    }

    // OVERVIEW - tabel device
    setTableBody("overview", ".device-panel tbody",
        `<tr>
            <td>${d.deviceName}</td>
            <td>ESP32</td>
            <td>ONLINE</td>
            <td>${clock(now)}</td></tr>`,
        "");
}

// ---------- polling ----------
async function fetchData() {
    try {
        const res = await fetch(ESP32_URL + "/api/data", { cache: "no-store" });
        if (!res.ok) throw new Error("HTTP " + res.status);

        const data = await res.json();
        lastOkTime = Date.now();

        setConnection(true);
        render(data);
    } catch (err) {
        console.warn("Gagal mengambil data ESP32:", err.message);
        if (Date.now() - lastOkTime > OFFLINE_AFTER_MS) {
            setConnection(false);
        }
    }
}

fetchData();
setInterval(fetchData, POLL_MS);