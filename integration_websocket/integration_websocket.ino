#include <WiFi.h>
#include <WiFiManager.h> // Pastikan library "WiFiManager" by tzapu terinstall
#include <WebSocketsClient.h> // Library: "WebSockets" by Markus Sattler
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// ================= KONFIGURASI WIFI =================
const char* hardcode_ssid = "POCO X3 Pro"; 
const char* hardcode_pass = "12321213";
const char* ap_name = "ESP32_raply"; 

// ================= KONFIGURASI NGROK / SOCKET.IO =================
const char* ws_host  = "nichelle-attractive-transperitoneally.ngrok-free.dev";
const int   ws_port  = 443; // Port SSL
const char* ws_path  = "/socket.io/?EIO=4&transport=websocket&type=esp32";

WebSocketsClient webSocket;
unsigned long lastTimeWS = 0;
const unsigned long INTERVAL_WS = 100; // Kirim 10Hz seperti referensi
bool socketIoConnected = false;

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

// ================= KONFIGURASI SERIAL GSR (Arduino Nano) =================
HardwareSerial NanoSerial(2);
const int NANO_RX_PIN = 16; // ESP32 menerima data dari TX Nano
const int NANO_TX_PIN = 17; // tidak wajib dipakai (opsional balasan)

float shared_GSR_uS = 0.0f;
bool gsrDataValid = false;
String lastGsrLine;
float gsr_raw = 0.0f;
float gsr_vcc = 0.0f;
float gsr_vout = 0.0f;
float gsr_rest = 0.0f;

// Timer untuk Serial Print (Biar gak spamming terlalu cepat sampai unreadable)
unsigned long lastPrintTime = 0; 
const long printInterval = 100; // Print data setiap 100ms (biar terlihat mengalir terus)

// ================= CALLBACK WEBSOCKET =================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      socketIoConnected = false;
      Serial.println("[WS] Disconnected!");
      break;

    case WStype_CONNECTED:
      socketIoConnected = false;
      Serial.printf("[WS] Connected to: %s\n", payload);
      webSocket.sendTXT("40");
      Serial.println("[WS] Handshake sent: 40");
      break;

    case WStype_TEXT: {
      if (length > 0 && payload[0] == '2') {
        webSocket.sendTXT("3");
        break;
      }

      String msg;
      msg.reserve(length + 1);
      for (size_t i = 0; i < length; i++) msg += static_cast<char>(payload[i]);

      if (msg.length() > 0 && msg[0] == '0') {
        webSocket.sendTXT("40");
        Serial.println("[WS] Re-handshake sent: 40");
      } else if (msg.startsWith("40")) {
        socketIoConnected = true;
        Serial.println("[WS] Socket.IO ready!");
      }
      break;
    }

    case WStype_ERROR:
      Serial.println("[WS] Error!");
      break;
  }
}

void sendWsData() {
  String json = "42[\"esp32_live_data\",{";
  json += "\"hr\":" + String(beatAvg) + ",";
  json += "\"temp\":" + String(tempC_final, 2) + ",";
  json += "\"eda\":" + String(gsrDataValid ? shared_GSR_uS : 0.0f, 2) + ",";
  json += "\"device_id\":\"ESP32_001\"";
  json += "}]";

  webSocket.sendTXT(json);
}

bool extractFloatAfterKey(const String& source, const char* key, float& out) {
  int idx = source.indexOf(key);
  if (idx < 0) return false;
  idx += strlen(key);

  while (idx < (int)source.length() && source[idx] == ' ') idx++;

  int end = idx;
  while (end < (int)source.length()) {
    char c = source[end];
    if ((c >= '0' && c <= '9') || c == '.' || c == '-') {
      end++;
    } else {
      break;
    }
  }

  if (end <= idx) return false;
  out = source.substring(idx, end).toFloat();
  return true;
}

void readGsrFromNano() {
  while (NanoSerial.available()) {
    String line = NanoSerial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    lastGsrLine = line;
    float rawTemp, vccTemp, voutTemp, restTemp, usTemp;
    bool okRaw  = extractFloatAfterKey(line, "RAW:", rawTemp);
    bool okVcc  = extractFloatAfterKey(line, "VCC:", vccTemp);
    bool okVout = extractFloatAfterKey(line, "Vout:", voutTemp);
    bool okRest = extractFloatAfterKey(line, "R_est:", restTemp);
    bool okUs   = extractFloatAfterKey(line, "G:", usTemp);

    if (okRaw)  gsr_raw  = rawTemp;
    if (okVcc)  gsr_vcc  = vccTemp;
    if (okVout) gsr_vout = voutTemp;
    if (okRest) gsr_rest = restTemp;
    if (okUs) {
      gsrDataValid = true;
      shared_GSR_uS = usTemp;
    }
  }
}

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

  NanoSerial.begin(9600, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
  NanoSerial.setTimeout(5);
  Serial.println("Serial2 siap terima data GSR dari Nano.");

  Serial.print("Menghubungkan Ngrok: ");
  Serial.println(ws_host);
  webSocket.beginSSL(ws_host, ws_port, ws_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
}

// ================= LOOP =================
void loop() {
  webSocket.loop();
  readGsrFromNano();
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
    
    Serial.print("WS:");
    Serial.print(socketIoConnected ? "ON" : "OFF");

    Serial.print(" | WiFi:");
    Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "ERR");

    Serial.print(" || BPM:");
    Serial.print(beatAvg);

    Serial.print(" | Temp:");
    Serial.print(tempC_final, 2);

    Serial.print(" | RAW:");
    Serial.print(gsr_raw, 1);

    Serial.print(" | VCC:");
    Serial.print(gsr_vcc, 3);

    Serial.print(" | Vout:");
    Serial.print(gsr_vout, 3);

    Serial.print(" | R:");
    Serial.print(gsr_rest, 2);

    Serial.print(" | uS:");
    Serial.println(shared_GSR_uS, 4);
    
    // Kalau mau liat raw IR value buat debugging sensitivitas, uncomment ini:
    // Serial.print(" \t| IR: "); Serial.println(irValue);
  }

  unsigned long now = millis();
  if (now - lastTimeWS >= INTERVAL_WS) {
    lastTimeWS = now;
    sendWsData();
  }
}
