#include <WiFi.h>
#include <WebSocketsClient.h> 
#include <Wire.h>
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
const char* ws_host  = "premedical-caryl-gawkishly.ngrok-free.dev"; 
const int   ws_port  = 443; // Port SSL
const char* ws_path  = "/socket.io/?EIO=4&transport=websocket&type=esp32";

WebSocketsClient webSocket;

// ============================================================
// 2. SETTINGAN OPTIMAL GSR (PIN 34)
// ============================================================
#define GSR_PIN 34  // <-- Pin 34 (Input Only)

// LOGIKA BATAS:
const float ADC10_MIN = 35.0f;   
const float ADC10_MAX = 580.0f;  

// RUMUS POLINOMIAL ORDE 4 (Internal hitung kOhm dulu biar presisi)
const float K4 =  8.32659e-09f;
const float K3 = -6.45535e-06f;
const float K2 =  0.00237126f;
const float K1 =  0.086247663f;
const float K0 =  48.19170232f;

// Variabel Data & Filter
float shared_GSR_uS = 0.0; 
float shared_GSR_Ohm = 0.0; 
float filtered_ADC = 0.0; // Untuk EMA Filter
bool firstRunGSR = true;

// ============================================================
// 3. SETTINGAN OPTIMAL MAX30102
// ============================================================
MAX30105 particleSensor;
const byte POWER_LED = 0x1F; // Medium Power

const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
unsigned long lastBeat = 0; 
float beatsPerMinute = 0.0f;
int beatAvg = 0;
float shared_BPM = 0.0;
const long IR_FINGER_THRESHOLD = 18000;   // Turun threshold supaya tidak perlu sentuh GSR
const unsigned long FINGER_LOSS_GRACE_MS = 600;
const unsigned long BPM_NO_SIGNAL_TIMEOUT_MS = 4000;
bool fingerDetected = false;
unsigned long lastStrongIR = 0;
unsigned long lastValidBeatMillis = 0;

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

const int INTERVAL_TEMP = 1000;   
const int INTERVAL_WS = 100;      
const int INTERVAL_PRINT = 200;   
const int INTERVAL_FINGER = 50;   


// ================= CALLBACK WEBSOCKET =================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected!");
      break;
    case WStype_CONNECTED:
      Serial.printf("[WS] Connected to: %s\n", payload);
      webSocket.sendTXT("40");
      break;
    case WStype_TEXT:
      if (length > 0 && payload[0] == '2') webSocket.sendTXT("3");
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
    Serial.println("❌ MAX30102 ERROR - Cek Kabel!"); while (1);
  }
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(POWER_LED); 
  particleSensor.setPulseAmplitudeGreen(0);

  // 4. GSR (Pin 34)
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
  webSocket.loop(); 
  unsigned long currentMillis = millis();

  // ==========================================
  // A. MAX30102 (PRIORITAS TINGGI - Realtime)
  // ==========================================
  long irValue = particleSensor.getIR();
  if (irValue > IR_FINGER_THRESHOLD) {
    fingerDetected = true;
    lastStrongIR = currentMillis;
  } else if (fingerDetected && (currentMillis - lastStrongIR > FINGER_LOSS_GRACE_MS)) {
    fingerDetected = false;
  }

  if (checkForBeat(irValue) == true) {
    unsigned long delta = currentMillis - lastBeat;
    lastBeat = currentMillis;
    if (delta == 0) {
      delta = 1; // hindari pembagian nol saat loop sangat cepat
    }
    beatsPerMinute = 60000.0 / delta;
    if (beatsPerMinute < 255 && beatsPerMinute > 40) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++) beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
      shared_BPM = (float)beatAvg;
      lastValidBeatMillis = currentMillis;
      fingerDetected = true;
      lastStrongIR = currentMillis;
    }
  }

  // Reset BPM jika jari dilepas
  if (currentMillis - lastTimeFingerCheck >= INTERVAL_FINGER) {
    lastTimeFingerCheck = currentMillis;
    bool bpmTimedOut = (lastValidBeatMillis > 0) && (currentMillis - lastValidBeatMillis > BPM_NO_SIGNAL_TIMEOUT_MS);
    if (!fingerDetected || bpmTimedOut) {
      shared_BPM = 0; 
      beatAvg = 0; 
      for(byte x=0; x<RATE_SIZE; x++) rates[x]=0; 
      fingerDetected = false;
      lastStrongIR = 0;
      lastValidBeatMillis = 0;
    }
  }

  // ==========================================
  // B. GSR (NON-BLOCKING - 50Hz Sampling)
  // ==========================================
  if (currentMillis - lastTimeGSR >= 20) { // Update setiap 20ms
    lastTimeGSR = currentMillis;
    
    float rawADC = analogRead(GSR_PIN); // Baca Pin 34
    
    // Inisialisasi filter pertama kali
    if (firstRunGSR) { filtered_ADC = rawADC; firstRunGSR = false; }

    // Filter EMA (Halus tanpa delay): 90% lama, 10% baru
    filtered_ADC = (0.90f * filtered_ADC) + (0.10f * rawADC);

    // Konversi ke ADC 10-bit
    float adc10 = filtered_ADC * (1023.0f / 4095.0f);
    
    // Logika Threshold (60 - 600)
    if (adc10 >= ADC10_MIN && adc10 <= ADC10_MAX) {
       float x = adc10;
       
       // Hitung Polinomial (Hasil internal kOhm)
       float R_kOhm = (((K4 * x + K3) * x + K2) * x + K1) * x + K0;
       
       // Konversi ke Ohm (PENTING)
       float R_Ohm = R_kOhm * 1000.0f; 

       if (R_Ohm > 0.0f) {
          shared_GSR_Ohm = R_Ohm;
          shared_GSR_uS = (1.0f / R_Ohm) * 1e6; 
       } else {
          shared_GSR_Ohm = 0.0f; shared_GSR_uS = 0.0f;
       }
    } else {
       // Di luar range (Udara / Lepas)
       shared_GSR_Ohm = 0.0f; 
       shared_GSR_uS = 0.0f; 
    }
  }

  // ==========================================
  // C. SUHU (1 Hz)
  // ==========================================
  if (currentMillis - lastTimeTemp >= INTERVAL_TEMP) {
    lastTimeTemp = currentMillis;
    float tempC = sensors.getTempCByIndex(0);
    if (tempC > -100) shared_Temp = tempC + KALIBRASI_SUHU;
    sensors.requestTemperatures();
  }

  // ==========================================
  // D. KIRIM WEBSOCKET (10 Hz)
  // ==========================================
  if (currentMillis - lastTimeWS >= INTERVAL_WS) {
    lastTimeWS = currentMillis;
    
    // Format JSON Ringkas
    String json = "42[\"esp32_live_data\",{";
    json += "\"hr\":" + String(shared_BPM) + ","; 
    json += "\"temp\":" + String(shared_Temp, 2) + ",";
    json += "\"eda\":" + String(shared_GSR_uS, 2) + ",";
    json += "\"device_id\":\"ESP32_001\"";
    json += "}]";

    webSocket.sendTXT(json);
  }

  // ==========================================
  // E. SERIAL MONITOR (OUTPUT OHM)
  // ==========================================
  if (currentMillis - lastTimePrint >= INTERVAL_PRINT) {
    lastTimePrint = currentMillis;
    Serial.print("BPM: "); Serial.print(shared_BPM, 0);
    Serial.print(" | Temp: "); Serial.print(shared_Temp, 2);
    Serial.print(" | uS: "); Serial.print(shared_GSR_uS, 3);
    Serial.print(" | R: "); 
    
    // TAMPILKAN LANGSUNG OHM (TANPA kOhm)
    if(shared_GSR_Ohm > 0) {
      Serial.print(shared_GSR_Ohm, 2); // Print Ohm
    } else {
      Serial.print("LEPAS");
    }
    Serial.println(" Ohm"); // Label Ohm
  }
}
