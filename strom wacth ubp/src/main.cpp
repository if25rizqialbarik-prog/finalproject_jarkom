#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Adafruit_BMP280.h>
#include "DFRobot_AS3935_I2C.h"

// ===================== KONFIGURASI =====================
const char *WIFI_SSID = "Redmi 9C";      // <-- ganti
const char *WIFI_PASS = "26032007";  // <-- ganti

const char *DEVICE_ID = "COM5";
const char *DEVICE_NAME = "UBP Storm Watch Node 1";

// PIN ESP32
#define SDA_PIN 21
#define SCL_PIN 22
#define AS3935_IRQ_PIN 4

// ALAMAT SENSOR
#define BMP280_ADDR 0x76
#define AS3935_ADDR AS3935_ADD3

// ===================== OBJEK =====================
Adafruit_BMP280 bmp;
DFRobot_AS3935_I2C lightning(AS3935_IRQ_PIN, AS3935_ADDR);
WebServer server(80);

// ===================== STATE =====================
volatile bool petirIRQ = false;

bool bmpOk = false;
bool petirOk = false;

float lastSuhu = 0;
float lastTekanan = 0;
float lastKetinggian = 0;

uint32_t totalPetir = 0;
uint32_t totalDisturber = 0;
uint8_t lastJarak = 0;
uint32_t lastEnergi = 0;
unsigned long lastPetirMs = 0;

unsigned long waktuBMP = 0;
const unsigned long intervalBMP = 5000;

// ===================== INTERRUPT =====================
void IRAM_ATTR isrPetir() {
  petirIRQ = true;
}

// ===================== WEB HANDLER =====================
void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) {
    server.send(404, "text/plain",
                "index.html tidak ditemukan. Jalankan Upload Filesystem Image.");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

void handleData() {
  char json[512];

  long agoSec = totalPetir > 0 ? (long)((millis() - lastPetirMs) / 1000) : -1;

  snprintf(json, sizeof(json),
           "{"
           "\"deviceId\":\"%s\","
           "\"deviceName\":\"%s\","
           "\"suhu\":%.2f,"
           "\"tekanan\":%.2f,"
           "\"ketinggian\":%.2f,"
           "\"totalPetir\":%lu,"
           "\"totalDisturber\":%lu,"
           "\"jarak\":%u,"
           "\"energi\":%lu,"
           "\"petirAgoSec\":%ld,"
           "\"bmpOk\":%s,"
           "\"petirOk\":%s,"
           "\"rssi\":%d,"
           "\"uptimeSec\":%lu"
           "}",
           DEVICE_ID, DEVICE_NAME,
           lastSuhu, lastTekanan, lastKetinggian,
           (unsigned long)totalPetir, (unsigned long)totalDisturber,
           lastJarak, (unsigned long)lastEnergi, agoSec,
           bmpOk ? "true" : "false",
           petirOk ? "true" : "false",
           WiFi.RSSI(), (unsigned long)(millis() / 1000));

  // CORS: supaya dashboard dari Live Server bisa membaca data ESP32
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "404 Not Found");
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("================================");
  Serial.println(" UBP STORM WATCH - ESP32");
  Serial.println("================================");

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  // BMP280
  Serial.println("\nMemeriksa BMP280...");
  bmpOk = bmp.begin(BMP280_ADDR);
  Serial.println(bmpOk ? "BMP280 BERHASIL TERHUBUNG!" : "BMP280 TIDAK TERDETEKSI!");

  // AS3935 (coba 5x, tidak mengunci program)
  Serial.println("\nMemeriksa AS3935...");
  for (int i = 0; i < 5; i++) {
    if (lightning.begin() == 0) {
      petirOk = true;
      break;
    }
    Serial.println("AS3935 belum terdeteksi, mencoba lagi...");
    delay(1000);
  }

  if (petirOk) {
    Serial.println("AS3935 BERHASIL TERHUBUNG!");

    lightning.defInit();
    lightning.setOutdoors();
    lightning.disturberEn();
    lightning.setNoiseFloorLvl(2);
    lightning.setWatchdogThreshold(2);
    lightning.setSpikeRejection(2);

    pinMode(AS3935_IRQ_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(AS3935_IRQ_PIN), isrPetir, RISING);
  } else {
    Serial.println("AS3935 TIDAK TERDETEKSI!");
  }

  // LittleFS (tempat index.html dan assets/)
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS gagal dimount!");
  }

  // WiFi
  Serial.printf("\nMenghubungkan WiFi ke %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi terhubung!");
    Serial.print("Buka dashboard di: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi GAGAL terhubung (cek SSID/password).");
  }

  // Web server
  server.on("/", handleRoot);
  server.on("/index.html", handleRoot);
  server.on("/api/data", handleData);
  server.serveStatic("/assets", LittleFS, "/assets");
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("\nWeb server berjalan.");
  Serial.println("================================");
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();

  // SENSOR PETIR
  if (petirIRQ) {
    noInterrupts();
    petirIRQ = false;
    interrupts();

    delay(2);

    uint8_t hasil = lightning.getInterruptSrc();

    if (hasil == 1) {
      lastJarak = lightning.getLightningDistKm();
      lastEnergi = lightning.getStrikeEnergyRaw();
      lastPetirMs = millis();
      totalPetir++;

      Serial.println("\n===== PETIR TERDETEKSI =====");
      Serial.printf("Jarak badai : %u km\n", lastJarak);
      Serial.printf("Energi      : %lu\n", (unsigned long)lastEnergi);
      Serial.println("============================");
    } else if (hasil == 2) {
      totalDisturber++;
      Serial.println("[AS3935] Gangguan disturber terdeteksi");
    } else if (hasil == 3) {
      Serial.println("[AS3935] Noise terlalu tinggi");
    }
  }

  // BMP280 SETIAP 5 DETIK
  if (bmpOk && millis() - waktuBMP >= intervalBMP) {
    waktuBMP = millis();

    lastSuhu = bmp.readTemperature();
    lastTekanan = bmp.readPressure() / 100.0F;
    lastKetinggian = bmp.readAltitude(1013.25);

    Serial.printf("[BMP280] %.2f C | %.2f hPa | %.2f m\n",
                  lastSuhu, lastTekanan, lastKetinggian);
  }
}