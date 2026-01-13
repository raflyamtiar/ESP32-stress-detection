#include <WiFi.h>
#include <WiFiManager.h> // Pastikan library "WiFiManager" by tzapu terinstall
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// ================= KONFIGURASI WIFI =================
const char* hardcode_ssid = "POCO X3 Pro"; 
const char* hardcode_pass = "12321213";
const char* ap_name = "ESP32_raply"; 

// ================= KONFIGURASI DS18B20 (SUHU) =================
#define ONE_WIRE_BUS 4 
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

const float KALIBRASI_OFFSET = 0.50; 
unsigned long lastTempRequest = 0;
const long tempInterval = 1000; // Update suhu tiap 1000ms (1 detik)
float tempC_final = 0.0;

// ================= KONFIGURASI MAX30102 (JANTUNG) =================
MAX30105 particleSensor;

const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
float beatsPerMinute;
int beatAvg = 0; 
byte beatCount = 0; 

// Timer untuk Serial Print (Biar gak spamming terlalu cepat sampai unreadable)
unsigned long lastPrintTime = 0; 
const long printInterval = 100; // Print data setiap 100ms (biar terlihat mengalir terus)

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== SYSTEM START ===");

  // --- 1. SETUP WIFI ---
  WiFi.mode(WIFI_STA);
  Serial.print("Mencoba koneksi ke WiFi Hardcoded: ");
  Serial.println(hardcode_ssid);
  
  WiFi.begin(hardcode_ssid, hardcode_pass);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 10) { // Coba 5 detik
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nTerhubung WiFi Hardcode!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nGagal. Masuk Mode WiFi Manager...");
    WiFiManager wm;
    wm.setConfigPortalTimeout(180); 
    if (!wm.autoConnect(ap_name)) {
      Serial.println("Timeout. Restart...");
      ESP.restart();
    }
    Serial.println("Terhubung via WiFi Manager!");
  }

  // --- 2. SETUP SENSOR SUHU ---
  sensors.begin();
  sensors.setResolution(11);
  sensors.setWaitForConversion(false); // PENTING: Non-blocking
  sensors.requestTemperatures(); 
  lastTempRequest = millis();

  // --- 3. SETUP SENSOR JANTUNG ---
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 ERROR. Cek Kabel!");
  } else {
    particleSensor.setup(); 
    particleSensor.setPulseAmplitudeRed(0x0A); 
    particleSensor.setPulseAmplitudeGreen(0); 
  }
}

// ================= LOOP =================
void loop() {
  // -----------------------------------------------------------
  // BAGIAN 1: HITUNG DETAK JANTUNG (Realtime / Cepat)
  // -----------------------------------------------------------
  long irValue = particleSensor.getIR(); 

  // Jika ada jari terdeteksi (Threshold IR > 50.000)
  if (irValue > 50000) {
    if (checkForBeat(irValue) == true) {
      long delta = millis() - lastBeat; 
      lastBeat = millis();
      
      beatsPerMinute = 60000.0 / delta; 

      if (beatsPerMinute < 255 && beatsPerMinute > 40) {
        rates[rateSpot++] = (byte)beatsPerMinute; 
        rateSpot %= RATE_SIZE; 
        if (beatCount < RATE_SIZE) beatCount++; 
        
        // Hitung Rata-rata
        beatAvg = 0;
        for (byte x = 0 ; x < RATE_SIZE ; x++) beatAvg += rates[x];
        if (beatCount > 0) beatAvg /= beatCount;
      }
    }
  } else {
    // Jika tidak ada jari, Reset BPM jadi 0
    beatAvg = 0;
    beatCount = 0;
    for (byte x = 0 ; x < RATE_SIZE ; x++) rates[x] = 0;
  }

  // -----------------------------------------------------------
  // BAGIAN 2: HITUNG SUHU (Interval 1 Detik / 1 Hz)
  // -----------------------------------------------------------
  if (millis() - lastTempRequest >= tempInterval) {
    // 1. Baca hasil request detik sebelumnya
    float tempC_raw = sensors.getTempCByIndex(0);
    
    // 2. Request baru untuk detik berikutnya
    sensors.requestTemperatures(); 
    lastTempRequest = millis();

    // Validasi nilai
    if(tempC_raw != DEVICE_DISCONNECTED_C && tempC_raw > -127) {
       tempC_final = tempC_raw + KALIBRASI_OFFSET;
    }
    // Jika sensor suhu error/lepas, nilai tempC_final dibiarkan nilai terakhir
  }

  // -----------------------------------------------------------
  // BAGIAN 3: PRINT DATA TERUS MENERUS (Streaming)
  // -----------------------------------------------------------
  // Kita update tampilan setiap 100ms agar serial monitor enak dilihat
  if (millis() - lastPrintTime >= printInterval) {
    lastPrintTime = millis();
    
    // FORMAT: "BPM: [nilai] | Temp: [nilai]"
    Serial.print("BPM: ");
    Serial.print(beatAvg);
    Serial.print(" \t| Temp: "); // \t itu tab biar rapi
    Serial.print(tempC_final);
    Serial.println(" °C");
    
    // Kalau mau liat raw IR value buat debugging sensitivitas, uncomment ini:
    // Serial.print(" \t| IR: "); Serial.println(irValue);
  }
}
