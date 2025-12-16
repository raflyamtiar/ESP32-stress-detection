#include <WiFi.h>
#include <WebSocketsClient.h> // Library: "WebSockets" by Markus Sattler
#include <Wire.h>
#include <math.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================
// 1. KONFIGURASI WIFI & NGROK
// ============================================================
const char* ssid     = "POCO X3 Pro";
const char* password = "12321213";

// HOST NGROK (Tanpa https:// dan tanpa /)
// CEK LAGI! Pastikan ini alamat Ngrok terbaru kamu
const char* ws_host  = "premedical-caryl-gawkishly.ngrok-free.dev"; 

const int   ws_port  = 443; // Port SSL
const char* ws_path  = "/socket.io/?EIO=4&transport=websocket&type=esp32";

WebSocketsClient webSocket;

// ============================================================
// 2. SETTINGAN OPTIMAL GSR
// ============================================================
#define GSR_PIN 34
const float ADC10_MIN = 35.0f;
const float ADC10_MAX = 800.0f;   // Range lebar (supaya nilai 630 masuk)
const float GSR_EMA_ALPHA = 0.2f; // 0..1, semakin kecil semakin halus
const float BATAS_UDARA = 600.0f; // Di atas angka ini diasumsikan lepas/udara

// Rumus Polinomial
const float K4 =  8.32659e-09f;
const float K3 = -6.45535e-06f;
const float K2 =  0.00237126f;
const float K1 =  0.086247663f;
const float K0 =  48.19170232f;

// Variabel Data
float shared_GSR_uS = 0.0; 
float shared_GSR_Ohm = 0.0; // Ditampilkan di Serial, TIDAK dikirim ke Web
float gsrFilteredAdc10 = 0.0f;
bool gsrFilterPrimed = false;

// ============================================================
// 3. SETTINGAN OPTIMAL MAX30102
// ============================================================
MAX30105 particleSensor;
const byte POWER_LED = 0x1F; // Medium Power

const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
float beatsPerMinute;
int beatAvg = 0;
float shared_BPM = 0.0;

// ============================================================
// 4. SETTINGAN SUHU & TIMING
// ============================================================
#define DS18B20_PIN 4
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
float shared_Temp = 0.0;
const float KALIBRASI_SUHU = 0.70; 

unsigned long lastTimeGSR = 0;
unsigned long lastTimeTemp = 0;
unsigned long lastTimeWS = 0;
unsigned long lastTimeFingerCheck = 0;
unsigned long lastTimePrint = 0;

const int INTERVAL_GSR = 100;    
const int INTERVAL_TEMP = 1000;   
const int INTERVAL_WS = 100;      // Kirim ke Web 10Hz
const int INTERVAL_PRINT = 200;   // Tampil di Serial 5Hz
const int INTERVAL_FINGER = 50;   

// ================= CALLBACK WEBSOCKET =================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected!");
      break;

    case WStype_CONNECTED:
      Serial.printf("[WS] Connected to: %s\n", payload);
      // HANDSHAKE (Wajib buat Socket.IO)
      webSocket.sendTXT("40");
      Serial.println("[WS] Handshake sent: 40");
      break;

    case WStype_TEXT:
      // HEARTBEAT (Balas Ping "2" dengan Pong "3")
      if (length > 0 && payload[0] == '2') {
         webSocket.sendTXT("3");
      }
      break;
      
    case WStype_ERROR:
      Serial.println("[WS] Error!");
      break;
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- SYSTEM STARTING ---");

  // 1. WIFI
  Serial.print("Menghubungkan WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA); 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n✅ WIFI TERHUBUNG!");

  // 2. WEBSOCKET SSL
  Serial.print("Menghubungkan Ngrok: ");
  Serial.println(ws_host);
  webSocket.beginSSL(ws_host, ws_port, ws_path);
  webSocket.onEvent(webSocketEvent); 
  webSocket.setReconnectInterval(3000); 

  // 3. MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("❌ MAX30102 ERROR"); while (1);
  }
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(POWER_LED); 
  particleSensor.setPulseAmplitudeGreen(0);

  // 4. GSR
  analogReadResolution(12);       
  analogSetAttenuation(ADC_11db); 
  pinMode(GSR_PIN, INPUT);

  // 5. SUHU
  sensors.begin();
  sensors.setResolution(11);
  sensors.setWaitForConversion(false); 
  sensors.requestTemperatures();       
}

// ================= LOOP =================
void loop() {
  webSocket.loop(); // Wajib jalan terus
  unsigned long currentMillis = millis();

  // --- A. MAX30102 (Realtime) ---
  long irValue = particleSensor.getIR();
  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60000.0 / delta;
    if (beatsPerMinute < 255 && beatsPerMinute > 40) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++) beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
      shared_BPM = (float)beatAvg;
    }
  }
  // Reset BPM Logic
  if (currentMillis - lastTimeFingerCheck >= INTERVAL_FINGER) {
     lastTimeFingerCheck = currentMillis;
     if (irValue < 50000) { shared_BPM = 0; beatAvg = 0; for(byte x=0; x<RATE_SIZE; x++) rates[x]=0; }
  }

  // --- B. GSR (Optimasi Filter Udara) ---
  if (currentMillis - lastTimeGSR >= INTERVAL_GSR) {
    lastTimeGSR = currentMillis;
    long sum = 0;
    for (int i = 0; i < 20; i++) sum += analogRead(GSR_PIN);
    float adc12 = sum / 20.0;
    float adc10 = adc12 * (1023.0f / 4095.0f);

    // Smoothing supaya lonjakan kecil tidak langsung mengubah output
    if (!gsrFilterPrimed) {
      gsrFilteredAdc10 = adc10;
      gsrFilterPrimed = true;
    } else {
      gsrFilteredAdc10 = (GSR_EMA_ALPHA * adc10) + ((1.0f - GSR_EMA_ALPHA) * gsrFilteredAdc10);
    }

    float adc10Smooth = gsrFilteredAdc10;
    
    // Logika Threshold
    if (adc10Smooth >= ADC10_MIN && adc10Smooth <= ADC10_MAX) {
      if (adc10Smooth > BATAS_UDARA) {
        shared_GSR_Ohm = 0.0f;
        shared_GSR_uS = 0.0f;
      } else {
        float x = adc10Smooth;
        float R_kOhm = (((K4 * x + K3) * x + K2) * x + K1) * x + K0;
        float R_Ohm = R_kOhm * 1000.0f; 
        R_Ohm = fmaxf(R_Ohm, 1.0f); // clamp supaya tidak negatif / nol
        shared_GSR_Ohm = R_Ohm;
        shared_GSR_uS = (1.0f / R_Ohm) * 1e6; 
      }
    } else {
      shared_GSR_Ohm = 0.0f; shared_GSR_uS = 0.0f; 
    }
  }

  // --- C. SUHU ---
  if (currentMillis - lastTimeTemp >= INTERVAL_TEMP) {
    lastTimeTemp = currentMillis;
    float tempC = sensors.getTempCByIndex(0);
    if (tempC > -100) shared_Temp = tempC + KALIBRASI_SUHU;
    sensors.requestTemperatures();
  }

  // --- D. KIRIM WEBSOCKET (TANPA OHM) ---
  if (currentMillis - lastTimeWS >= INTERVAL_WS) {
    lastTimeWS = currentMillis;
    
    // Format JSON: Hanya hr, temp, eda (uS)
    String json = "42[\"esp32_live_data\",{";
    json += "\"hr\":" + String(shared_BPM) + ",";
    json += "\"temp\":" + String(shared_Temp, 2) + ",";
    json += "\"eda\":" + String(shared_GSR_uS, 2) + ",";
    json += "\"device_id\":\"ESP32_001\"";
    json += "}]";

    webSocket.sendTXT(json);
  }

  // --- E. TAMPILKAN SERIAL (LENGKAP DENGAN OHM) ---
  if (currentMillis - lastTimePrint >= INTERVAL_PRINT) {
    lastTimePrint = currentMillis;
    
    Serial.print("BPM: "); Serial.print(shared_BPM, 0);
    Serial.print(" | Temp: "); Serial.print(shared_Temp, 2);
    Serial.print(" | uS: "); Serial.print(shared_GSR_uS, 2);
    
    // Ohm ditampilkan disini saja, tidak dikirim ke web
    Serial.print(" | Ohm: "); Serial.println(shared_GSR_Ohm, 0);
  }
}
