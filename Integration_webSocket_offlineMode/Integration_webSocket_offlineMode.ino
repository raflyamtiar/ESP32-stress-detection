#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>

#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <LittleFS.h>
#include <ArduinoJson.h>

#include "MAX30105.h"
#include "heartRate.h"

// ======================================================
// WIFI
// ======================================================
const char* WIFI_SSID = "POCO X3 Pro";
const char* WIFI_PASS = "12321213";

const unsigned long WIFI_CONNECT_TIMEOUT = 3000;
const unsigned long WIFI_RETRY_INTERVAL = 10000;
unsigned long lastWiFiRetry = 0;
bool lastWiFiConnected = false;

// ======================================================
// BACKEND / DEV TUNNEL
// ======================================================
const char* ws_host = "6j62wg36-5000.asse.devtunnels.ms";
const int ws_port = 443;
const char* ws_path = "/socket.io/?EIO=4&transport=websocket&type=esp32";

const char* PREDICT_URL = "https://6j62wg36-5000.asse.devtunnels.ms/api/predict-stress";
const char* OFFLINE_SYNC_URL = "https://6j62wg36-5000.asse.devtunnels.ms/api/offline-sync";

WebSocketsClient webSocket;
bool webSocketStarted = false;
bool socketIoConnected = false;

unsigned long lastWsConnectedTime = 0;
unsigned long lastWsOffCheckTime = 0;
unsigned long lastWsStartAttempt = 0;

const unsigned long WS_START_COOLDOWN = 5000;
const unsigned long WS_OFF_FORCE_WIFI_RETRY = 20000; // 10 detik

unsigned long lastTimeWS = 0;
const unsigned long INTERVAL_WS = 100;

// ======================================================
// OLED 128x32
// ======================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

// ======================================================
// BUTTON
// ======================================================
#define BUTTON_PIN 25

unsigned long buttonPressStart = 0;
bool buttonWasPressed = false;
bool longPressHandled = false;

const unsigned long LONG_PRESS_DURATION = 2000;

// ======================================================
// STATE ALAT
// ======================================================
enum DeviceState {
  READY,
  MEASURING,
  RESULT,
  CANCELED
};

DeviceState deviceState = READY;

// ======================================================
// PENGUKURAN 60 DETIK, RATA-RATA 10 DETIK TERAKHIR
// ======================================================
unsigned long measurementStartTime = 0;

const unsigned long MEASUREMENT_DURATION = 60000;
const unsigned long LAST_WINDOW_DURATION = 10000;
const unsigned long SAMPLE_INTERVAL = 1000;

unsigned long lastAverageSampleTime = 0;

float sumHR = 0;
float sumTemp = 0;
float sumEDA = 0;
int avgCount = 0;

const int MAX_LAST_SAMPLES = 10;

float lastSampleHR[MAX_LAST_SAMPLES];
float lastSampleTemp[MAX_LAST_SAMPLES];
float lastSampleEDA[MAX_LAST_SAMPLES];

int lastSampleCount = 0;

float finalHR = 0;
float finalTemp = 0;
float finalEDA = 0;

String finalStressLabel = "";
String finalMode = "";

// ======================================================
// DS18B20
// ======================================================
#define ONE_WIRE_BUS 4

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

const float KALIBRASI_OFFSET = 0.50;

unsigned long lastTempRequest = 0;
const long tempInterval = 1000;

float tempC_final = 0.0;

// ======================================================
// MAX30102
// ======================================================
MAX30105 particleSensor;

const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;

long lastBeat = 0;

float beatsPerMinute = 0;
int beatAvg = 0;
byte beatCount = 0;

// ======================================================
// GSR DARI ARDUINO NANO VIA SERIAL2
// ======================================================
HardwareSerial NanoSerial(2);

const int NANO_RX_PIN = 16;
const int NANO_TX_PIN = 17;

float shared_GSR_uS = 0.0f;
bool gsrDataValid = false;

String lastGsrLine;

float gsr_raw = 0.0f;
float gsr_vcc = 0.0f;
float gsr_vout = 0.0f;
float gsr_rest = 0.0f;

// ======================================================
// DEBUG
// ======================================================
unsigned long lastPrintTime = 0;
const long printInterval = 1000;

// ======================================================
// OFFLINE FILE
// ======================================================
const char* OFFLINE_FILE = "/offline_queue.jsonl";

// ======================================================
// OLED HELPER
// ======================================================
void oledClear() {
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
}

void oledShow() {
  if (!oledReady) return;
  display.display();
}

int countOfflineRecords() {
  if (!LittleFS.exists(OFFLINE_FILE)) return 0;

  File file = LittleFS.open(OFFLINE_FILE, "r");
  if (!file) return 0;

  int count = 0;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.length() > 0) {
      count++;
    }
  }

  file.close();
  return count;
}

bool isOnlineMode() {
  return WiFi.status() == WL_CONNECTED && socketIoConnected;
}

void showReadyScreen() {
  oledClear();

  display.println("STRESS DETECTOR");

  display.print("Mode: ");
  display.println(isOnlineMode() ? "ONLINE" : "OFFLINE");

  display.print("Saved: ");
  display.println(countOfflineRecords());

  if (isOnlineMode()) {
    display.println("Button: LOCKED");
  } else {
    display.println("Press START");
  }

  oledShow();
}

void showWiFiConnectingScreen() {
  oledClear();
  display.println("Connecting WiFi");
  display.print("Max ");
  display.print(WIFI_CONNECT_TIMEOUT / 1000);
  display.println(" seconds");
  display.println("Please wait...");
  oledShow();
}

void showSearchingWiFiScreen() {
  oledClear();
  display.println("MODE: OFFLINE");
  display.println("Searching WiFi...");
  display.print("Saved: ");
  display.println(countOfflineRecords());
  display.println("Please wait");
  oledShow();
}

void showOfflineScreen() {
  oledClear();
  display.println("WiFi not found");
  display.println("OFFLINE MODE");
  display.println("Press START");
  oledShow();
}

void showMeasuringScreen(unsigned long elapsedMs) {
  int seconds = elapsedMs / 1000;
  if (seconds > 60) seconds = 60;

  oledClear();
  display.println("MEASURING...");
  display.print("Time: ");
  display.print(seconds);
  display.println("/60s");

  display.print("HR:");
  display.print(beatAvg);
  display.print(" T:");
  display.println(tempC_final, 1);

  display.print("EDA:");
  display.print(shared_GSR_uS, 1);

  oledShow();
}

void showResultScreen() {
  oledClear();

  display.print("HASIL: ");
  display.println(finalStressLabel);

  display.print("HR:");
  display.print(finalHR, 1);
  display.print(" T:");
  display.println(finalTemp, 1);

  display.print("EDA:");
  display.print(finalEDA, 2);
  display.print(" ");
  display.println(finalMode);

  display.print("Saved:");
  display.print(countOfflineRecords());
  display.println(" Reset");

  oledShow();
}

void showCanceledScreen() {
  oledClear();
  display.println("MEASUREMENT");
  display.println("CANCELED");
  display.println("Data not saved");
  display.println("Press RESET");
  oledShow();
}

void showSavedOfflineScreen() {
  showResultScreen();
}

void showSyncScreen(int total) {
  oledClear();
  display.println("ONLINE MODE");
  display.println("Syncing data...");
  display.print("Queue: ");
  display.println(total);
  oledShow();
}

void showSyncSuccessScreen() {
  oledClear();
  display.println("SYNC SUCCESS");
  display.println("Offline data");
  display.println("sent to backend");
  oledShow();

  delay(1200);
  showReadyScreen();
}

void showSyncFailedScreen() {
  oledClear();
  display.println("SYNC FAILED");
  display.println("Will retry later");
  oledShow();

  delay(1200);
  showReadyScreen();
}

bool isButtonPressedNow() {
  return digitalRead(BUTTON_PIN) == LOW;
}
// ======================================================
// WIFI HELPER
// ======================================================
bool connectWiFiQuick(bool showProgress = true) {
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);

  if (showProgress) {
    showWiFiConnectingScreen();
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < WIFI_CONNECT_TIMEOUT) {

    // Kalau user pencet button saat cari WiFi,
    // proses WiFi dibatalkan supaya mode offline tetap bisa dipakai
    if (isButtonPressedNow() && !isOnlineMode()) {
      Serial.println("[WiFi] Button pressed. Cancel WiFi search.");

      WiFi.disconnect(true, true);
      socketIoConnected = false;
      webSocketStarted = false;
      lastWiFiConnected = false;

      if (deviceState == READY) {
        showReadyScreen();
      }

      return false;
    }

    delay(100);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());

    lastWiFiConnected = true;
    return true;
  }

  Serial.println("[WiFi] Failed. Continue OFFLINE.");

  WiFi.disconnect(true, true);
  socketIoConnected = false;
  webSocketStarted = false;
  lastWiFiConnected = false;

  return false;
}

// ======================================================
// WEBSOCKET
// ======================================================
void stopWebSocketForHttp() {
  if (webSocketStarted) {
    Serial.println("[WS] Stop temporarily for HTTPS request");
    webSocket.disconnect();
  }

  socketIoConnected = false;
  webSocketStarted = false;
  delay(700);
}

void startWebSocketIfOnline() {
  if (WiFi.status() != WL_CONNECTED) {
    socketIoConnected = false;
    webSocketStarted = false;
    return;
  }

  if (webSocketStarted) {
    return;
  }

  if (millis() - lastWsStartAttempt < WS_START_COOLDOWN) {
    return;
  }

  lastWsStartAttempt = millis();

  Serial.print("[WS] Connecting to Dev Tunnel: ");
  Serial.println(ws_host);

  webSocket.disconnect();
  delay(150);

  webSocket.beginSSL(ws_host, ws_port, ws_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);

  webSocketStarted = true;
  socketIoConnected = false;

  Serial.println("[WS] beginSSL called");
}

void handleWiFiAutoRetry() {
  static int failedWifiRetryCount = 0;

  bool wifiNow = (WiFi.status() == WL_CONNECTED);

  if (wifiNow) {
    lastWiFiConnected = true;
    failedWifiRetryCount = 0;

    if (!webSocketStarted) {
      startWebSocketIfOnline();
    }

    return;
  }

  socketIoConnected = false;
  webSocketStarted = false;
  lastWiFiConnected = false;

  // Saat sedang mengukur, jangan cari WiFi
  if (deviceState == MEASURING) {
    return;
  }

  // Kalau button sedang dipencet, jangan cari WiFi
  if (isButtonPressedNow()) {
    return;
  }

  if (millis() - lastWiFiRetry >= WIFI_RETRY_INTERVAL) {
    lastWiFiRetry = millis();

    Serial.println("[WiFi] OFFLINE mode. Background WiFi retry...");

    // Kalau sedang READY, boleh tampil connecting sebentar.
    // Kalau RESULT/CANCELED, jangan ganggu tampilan hasil.
    bool showProgress = (deviceState == READY);

    bool wifiOK = connectWiFiQuick(showProgress);

    if (wifiOK) {
      Serial.println("[WiFi] Auto reconnect success");

      failedWifiRetryCount = 0;
      startWebSocketIfOnline();

      if (deviceState == READY) {
        showReadyScreen();
      } else if (deviceState == RESULT) {
        showResultScreen();
      } else if (deviceState == CANCELED) {
        showCanceledScreen();
      }

      return;
    }

    failedWifiRetryCount++;

    Serial.print("[WiFi] Auto reconnect failed. Count: ");
    Serial.println(failedWifiRetryCount);

    if (deviceState == READY) {
      showReadyScreen();
    } else if (deviceState == RESULT) {
      showResultScreen();
    } else if (deviceState == CANCELED) {
      showCanceledScreen();
    }
  }
}

void handleWsOffButWifiOk() {
  static int failedWsRetryCount = 0;

  if (deviceState == MEASURING) {
    return;
  }

  if (isButtonPressedNow()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    failedWsRetryCount = 0;
    return;
  }

  if (socketIoConnected) {
    failedWsRetryCount = 0;
    return;
  }

  if (millis() - lastWsOffCheckTime < WS_OFF_FORCE_WIFI_RETRY) {
    return;
  }

  lastWsOffCheckTime = millis();
  failedWsRetryCount++;

  Serial.println("[WATCHDOG] WiFi OK but WS OFF.");
  Serial.print("[WATCHDOG] Background WS retry count: ");
  Serial.println(failedWsRetryCount);

  // Coba restart WebSocket dulu
  if (failedWsRetryCount < 3) {
    Serial.println("[WATCHDOG] Restart WebSocket only.");

    webSocket.disconnect();
    delay(200);

    webSocketStarted = false;
    socketIoConnected = false;

    startWebSocketIfOnline();
    return;
  }

  // Kalau WebSocket tetap gagal, baru reconnect WiFi
  Serial.println("[WATCHDOG] WS failed several times. Force WiFi reconnect.");

  failedWsRetryCount = 0;

  webSocket.disconnect();
  socketIoConnected = false;
  webSocketStarted = false;

  WiFi.disconnect(true, true);
  delay(500);

  // Jangan ganggu OLED saat RESULT/CANCELED
  bool showProgress = (deviceState == READY);

  bool wifiOK = connectWiFiQuick(showProgress);

  if (wifiOK) {
    Serial.println("[WATCHDOG] WiFi reconnect success");
    startWebSocketIfOnline();
  } else {
    Serial.println("[WATCHDOG] WiFi reconnect failed");
  }

  if (deviceState == READY) {
    showReadyScreen();
  } else if (deviceState == RESULT) {
    showResultScreen();
  } else if (deviceState == CANCELED) {
    showCanceledScreen();
  }
}

// ======================================================
// HTTP POST HELPER
// ======================================================
int postJson(const char* url, const String& payload, String& response) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] WiFi not connected");
    return -100;
  }

  Serial.println();
  Serial.println("[HTTP] ===== START HTTPS POST =====");
  Serial.print("[HTTP] URL: ");
  Serial.println(url);
  Serial.print("[HTTP] Payload length: ");
  Serial.println(payload.length());
  Serial.print("[HTTP] Free heap before: ");
  Serial.println(ESP.getFreeHeap());

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);

  HTTPClient http;
  http.setTimeout(30000);
  http.setReuse(false);

  bool beginOk = http.begin(client, url);

  if (!beginOk) {
    Serial.println("[HTTP] begin failed");
    http.end();
    client.stop();
    return -101;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("Connection", "close");
  http.addHeader("User-Agent", "ESP32-Stress-Detector");

  int httpCode = http.POST(payload);

  Serial.print("[HTTP] Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    response = http.getString();
    Serial.print("[HTTP] Response: ");
    Serial.println(response);
  } else {
    response = "";
    Serial.print("[HTTP] Error text: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
  client.stop();

  Serial.print("[HTTP] Free heap after: ");
  Serial.println(ESP.getFreeHeap());
  Serial.println("[HTTP] ===== END HTTPS POST =====");
  Serial.println();

  return httpCode;
}

// ======================================================
// GSR PARSER
// ======================================================
bool extractFloatAfterKey(const String& source, const char* key, float& out) {
  int idx = source.indexOf(key);

  if (idx < 0) {
    return false;
  }

  idx += strlen(key);

  while (idx < (int)source.length() && source[idx] == ' ') {
    idx++;
  }

  int end = idx;

  while (end < (int)source.length()) {
    char c = source[end];

    if ((c >= '0' && c <= '9') || c == '.' || c == '-') {
      end++;
    } else {
      break;
    }
  }

  if (end <= idx) {
    return false;
  }

  out = source.substring(idx, end).toFloat();
  return true;
}

void readGsrFromNano() {
  while (NanoSerial.available()) {
    String line = NanoSerial.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) {
      continue;
    }

    lastGsrLine = line;

    float rawTemp, vccTemp, voutTemp, restTemp, usTemp;

    bool okRaw = extractFloatAfterKey(line, "RAW:", rawTemp);
    bool okVcc = extractFloatAfterKey(line, "VCC:", vccTemp);
    bool okVout = extractFloatAfterKey(line, "Vout:", voutTemp);
    bool okRest = extractFloatAfterKey(line, "R_est:", restTemp);
    bool okUs = extractFloatAfterKey(line, "G:", usTemp);

    if (okRaw) gsr_raw = rawTemp;
    if (okVcc) gsr_vcc = vccTemp;
    if (okVout) gsr_vout = voutTemp;
    if (okRest) gsr_rest = restTemp;

    if (okUs) {
      gsrDataValid = true;
      shared_GSR_uS = usTemp;
    }
  }
}

// ======================================================
// WEBSOCKET EVENT
// ======================================================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      socketIoConnected = false;

      Serial.println("[WS] Disconnected");

      if (deviceState == READY) {
        showReadyScreen();
      }

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

      for (size_t i = 0; i < length; i++) {
        msg += static_cast<char>(payload[i]);
      }

      if (!msg.startsWith("42[\"live_data_received\"")) {
        Serial.print("[WS] TEXT: ");
        Serial.println(msg);
      }

      if (msg.length() > 0 && msg[0] == '0') {
        webSocket.sendTXT("40");
        Serial.println("[WS] Re-handshake sent: 40");

      } else if (msg.startsWith("40")) {
        socketIoConnected = true;
        lastWsConnectedTime = millis();

        Serial.println("[WS] Socket.IO ready");

        if (deviceState == READY) {
          showReadyScreen();
        }

      } else if (msg.startsWith("42[\"connection_status\"")) {
        socketIoConnected = true;
        lastWsConnectedTime = millis();

        Serial.println("[WS] Connection status received. Socket.IO ready");

        if (deviceState == READY) {
          showReadyScreen();
        }
      }

      break;
    }

    case WStype_ERROR:
      socketIoConnected = false;

      Serial.println("[WS] Error");

      if (deviceState == READY) {
        showReadyScreen();
      }

      break;

    default:
      break;
  }
}

void sendWsLiveData() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!socketIoConnected) return;

  String json = "42[\"esp32_live_data\",{";
  json += "\"hr\":" + String(beatAvg) + ",";
  json += "\"temp\":" + String(tempC_final, 2) + ",";
  json += "\"eda\":" + String(gsrDataValid ? shared_GSR_uS : 0.0f, 2) + ",";
  json += "\"device_id\":\"ESP32_001\"";
  json += "}]";

  webSocket.sendTXT(json);
}

// ======================================================
// LABEL NORMALIZER
// ======================================================
String normalizeStressLabel(String label) {
  label.trim();
  label.toLowerCase();

  if (label == "normal") {
    return "Normal";
  }

  if (label == "medium" || label == "medium stress" || label == "medium stres") {
    return "Medium Stress";
  }

  if (label == "high" || label == "high stress") {
    return "High Stress";
  }

  return "Normal";
}

// ======================================================
// PREDIKSI LOKAL OFFLINE
// HASIL HANYA: Normal, Medium Stress, High Stress
// ======================================================
String predictStressLocal(float hr, float temp, float eda) {
  int normalScore = 0;
  int mediumScore = 0;
  int highScore = 0;

  // Heart Rate
  if (hr >= 60 && hr <= 90) {
    normalScore++;
  } else if (hr > 90 && hr <= 100) {
    mediumScore++;
  } else if (hr > 100) {
    highScore++;
  } else {
    normalScore++;
  }

  // Skin Conductance / EDA
  if (eda < 5) {
    normalScore++;
  } else if (eda >= 5 && eda <= 10) {
    mediumScore++;
  } else if (eda > 10) {
    highScore++;
  } else {
    normalScore++;
  }

  // Skin Temperature
  if (temp >= 35 && temp <= 37) {
    normalScore++;
  } else if (temp >= 33 && temp < 35) {
    mediumScore++;
  } else if (temp < 33) {
    highScore++;
  } else {
    normalScore++;
  }

  if (highScore >= 2) {
    return "High Stress";
  }

  if (mediumScore >= 2) {
    return "Medium Stress";
  }

  if (normalScore >= 2) {
    return "Normal";
  }

  // Jika 1-1-1, ambil risiko tertinggi
  if (highScore == 1) {
    return "High Stress";
  }

  if (mediumScore == 1) {
    return "Medium Stress";
  }

  return "Normal";
}

// ======================================================
// ONLINE PREDICTION KE BACKEND
// ======================================================
String predictByBackend(float hr, float temp, float eda, bool& success) {
  success = false;

  StaticJsonDocument<256> doc;
  doc["hr"] = hr;
  doc["temp"] = temp;
  doc["eda"] = eda;

  String payload;
  serializeJson(doc, payload);

  Serial.println("[PREDICT] Sending to backend:");
  Serial.println(payload);

  stopWebSocketForHttp();

  String response;
  int httpCode = postJson(PREDICT_URL, payload, response);

  delay(700);
  startWebSocketIfOnline();

  Serial.print("[PREDICT] HTTP Code: ");
  Serial.println(httpCode);

  Serial.print("[PREDICT] Response: ");
  Serial.println(response);

  if (httpCode < 200 || httpCode >= 300) {
    success = false;
    return predictStressLocal(hr, temp, eda);
  }

  StaticJsonDocument<1024> res;
  DeserializationError err = deserializeJson(res, response);

  if (err) {
    success = false;
    return predictStressLocal(hr, temp, eda);
  }

  String label = "";

  if (res["prediction"].is<const char*>()) {
    label = String((const char*)res["prediction"]);
  } else if (res["stress_level"].is<const char*>()) {
    label = String((const char*)res["stress_level"]);
  } else if (res["stress_label"].is<const char*>()) {
    label = String((const char*)res["stress_label"]);
  } else if (res["label"].is<const char*>()) {
    label = String((const char*)res["label"]);
  } else if (res["result"].is<const char*>()) {
    label = String((const char*)res["result"]);
  } else if (res["data"]["label"].is<const char*>()) {
    label = String((const char*)res["data"]["label"]);
  } else if (res["data"]["prediction"].is<const char*>()) {
    label = String((const char*)res["data"]["prediction"]);
  }

  if (label.length() == 0) {
    success = false;
    return predictStressLocal(hr, temp, eda);
  }

  success = true;
  return normalizeStressLabel(label);
}

// ======================================================
// SIMPAN DATA OFFLINE
// ======================================================
bool saveOfflineRecord(float hr, float temp, float eda, String label) {
  DynamicJsonDocument doc(2048);

  doc["device_id"] = "ESP32_001";
  doc["hr"] = hr;
  doc["temp"] = temp;
  doc["eda"] = eda;
  doc["label"] = normalizeStressLabel(label);
  doc["prediction_source"] = "esp32_offline";
  doc["duration"] = 60;
  doc["average_window"] = 10;
  doc["local_millis"] = millis();

  JsonArray readings = doc.createNestedArray("readings");

  for (int i = 0; i < lastSampleCount; i++) {
    JsonObject reading = readings.createNestedObject();
    reading["hr"] = lastSampleHR[i];
    reading["temp"] = lastSampleTemp[i];
    reading["eda"] = lastSampleEDA[i];
    reading["second_index"] = i + 1;
  }

  String line;
  serializeJson(doc, line);

  File file = LittleFS.open(OFFLINE_FILE, "a");

  if (!file) {
    Serial.println("[FS] Failed to open offline file");
    return false;
  }

  file.println(line);
  file.close();

  Serial.println("[FS] Offline data saved:");
  Serial.println(line);

  return true;
}

// ======================================================
// SYNC DATA OFFLINE KE BACKEND
// ======================================================
bool syncOfflineRecords() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SYNC] WiFi not connected");
    return false;
  }

  if (!LittleFS.exists(OFFLINE_FILE)) {
    Serial.println("[SYNC] No offline file");
    return true;
  }

  File file = LittleFS.open(OFFLINE_FILE, "r");

  if (!file) {
    Serial.println("[SYNC] Cannot open offline file");
    return false;
  }

  // Dibesarkan karena sekarang payload bisa berisi 10 readings per record
  DynamicJsonDocument doc(16384);
  doc["device_id"] = "ESP32_001";

  JsonArray records = doc.createNestedArray("records");

  int total = 0;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) {
      continue;
    }

    // Dibesarkan karena 1 record offline sekarang punya readings array
    DynamicJsonDocument item(2048);
    DeserializationError err = deserializeJson(item, line);

    if (!err) {
      JsonObject obj = records.createNestedObject();

      obj["hr"] = item["hr"];
      obj["temp"] = item["temp"];
      obj["eda"] = item["eda"];

      const char* rawLabel = item["label"] | "Normal";
      obj["label"] = normalizeStressLabel(String(rawLabel));

      obj["prediction_source"] = item["prediction_source"] | "esp32_offline";
      obj["duration"] = item["duration"] | 60;
      obj["average_window"] = item["average_window"] | 10;
      obj["local_millis"] = item["local_millis"];

      // Kirim juga 10 data sensor terakhir kalau ada
      if (item["readings"].is<JsonArray>()) {
        JsonArray sourceReadings = item["readings"].as<JsonArray>();
        JsonArray targetReadings = obj.createNestedArray("readings");

        for (JsonObject sourceReading : sourceReadings) {
          JsonObject targetReading = targetReadings.createNestedObject();

          targetReading["hr"] = sourceReading["hr"];
          targetReading["temp"] = sourceReading["temp"];
          targetReading["eda"] = sourceReading["eda"];
          targetReading["second_index"] = sourceReading["second_index"];
        }
      }

      // JANGAN DIHAPUS
      // Ini menghitung jumlah record offline yang valid
      total++;

    } else {
      Serial.print("[SYNC] Bad JSON line: ");
      Serial.println(line);
      Serial.print("[SYNC] JSON error: ");
      Serial.println(err.c_str());
    }
  }

  file.close();

  if (total == 0) {
    LittleFS.remove(OFFLINE_FILE);
    Serial.println("[SYNC] No valid records. Offline file removed.");
    return true;
  }

  showSyncScreen(total);

  String payload;
  serializeJson(doc, payload);

  Serial.println();
  Serial.println("[SYNC] ===== START SYNC =====");
  Serial.print("[SYNC] Total records: ");
  Serial.println(total);
  Serial.print("[SYNC] Payload:");
  Serial.println(payload);

  stopWebSocketForHttp();

  String response;
  int httpCode = -999;

  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.print("[SYNC] Attempt ");
    Serial.print(attempt);
    Serial.println("/3");

    httpCode = postJson(OFFLINE_SYNC_URL, payload, response);

    Serial.print("[SYNC] HTTP Code: ");
    Serial.println(httpCode);

    Serial.print("[SYNC] Response: ");
    Serial.println(response);

    if (httpCode >= 200 && httpCode < 300) {
      LittleFS.remove(OFFLINE_FILE);
      Serial.println("[SYNC] Success. Offline file removed.");

      delay(700);
      startWebSocketIfOnline();

      return true;
    }

    if (attempt < 3) {
      Serial.println("[SYNC] Retry in 3 seconds...");
      delay(3000);
    }
  }

  Serial.println("[SYNC] Failed. Offline data kept.");

  delay(700);
  startWebSocketIfOnline();

  return false;
}

// ======================================================
// MEASUREMENT CONTROL
// ======================================================
void startMeasurement() {
  deviceState = MEASURING;

  measurementStartTime = millis();
  lastAverageSampleTime = 0;

  sumHR = 0;
  sumTemp = 0;
  sumEDA = 0;
  avgCount = 0;

  lastSampleCount = 0;

  for (int i = 0; i < MAX_LAST_SAMPLES; i++) {
    lastSampleHR[i] = 0;
    lastSampleTemp[i] = 0;
    lastSampleEDA[i] = 0;
  }

  finalHR = 0;
  finalTemp = 0;
  finalEDA = 0;

  finalStressLabel = "";
  finalMode = "";

  Serial.println("[MEASURE] Started");
}

void cancelMeasurement() {
  deviceState = CANCELED;

  sumHR = 0;
  sumTemp = 0;
  sumEDA = 0;
  avgCount = 0;

  Serial.println("[MEASURE] Canceled");
  showCanceledScreen();
}

void finishMeasurement() {
  if (avgCount > 0) {
    finalHR = sumHR / avgCount;
    finalTemp = sumTemp / avgCount;
    finalEDA = sumEDA / avgCount;
  } else {
    finalHR = beatAvg;
    finalTemp = tempC_final;
    finalEDA = shared_GSR_uS;
  }

  bool online = isOnlineMode();

  if (online) {
    bool backendSuccess = false;
    String backendLabel = predictByBackend(finalHR, finalTemp, finalEDA, backendSuccess);

    if (backendSuccess) {
      finalMode = "ONLINE";
      finalStressLabel = normalizeStressLabel(backendLabel);
    } else {
      finalMode = "OFFLINE";
      finalStressLabel = predictStressLocal(finalHR, finalTemp, finalEDA);
      saveOfflineRecord(finalHR, finalTemp, finalEDA, finalStressLabel);
    }
  } else {
    finalMode = "OFFLINE";
    finalStressLabel = predictStressLocal(finalHR, finalTemp, finalEDA);
    saveOfflineRecord(finalHR, finalTemp, finalEDA, finalStressLabel);
  }

  deviceState = RESULT;

  Serial.println("[MEASURE] Finished");
  Serial.print("AVG HR: ");
  Serial.println(finalHR);

  Serial.print("AVG TEMP: ");
  Serial.println(finalTemp);

  Serial.print("AVG EDA: ");
  Serial.println(finalEDA);

  Serial.print("MODE: ");
  Serial.println(finalMode);

  Serial.print("RESULT: ");
  Serial.println(finalStressLabel);

  showResultScreen();
}

void resetMeasurement() {
  deviceState = READY;
  showReadyScreen();
}

// ======================================================
// BUTTON HANDLER
// ======================================================
void handleButton() {
  // Kalau WebSocket ON, alat dianggap ONLINE
  // Button dikunci agar tidak mengganggu mode realtime
  if (isOnlineMode()) {
    buttonWasPressed = false;
    longPressHandled = false;
    return;
  }

  bool pressed = digitalRead(BUTTON_PIN) == LOW;

  if (pressed && !buttonWasPressed) {
    buttonPressStart = millis();
    buttonWasPressed = true;
    longPressHandled = false;
  }

  if (pressed &&
      buttonWasPressed &&
      !longPressHandled &&
      deviceState == MEASURING) {

    unsigned long pressDuration = millis() - buttonPressStart;

    if (pressDuration >= LONG_PRESS_DURATION) {
      longPressHandled = true;
      cancelMeasurement();
    }
  }

  if (!pressed && buttonWasPressed) {
    buttonWasPressed = false;

    if (longPressHandled) {
      return;
    }

    if (deviceState == READY) {
      startMeasurement();
    } else if (deviceState == RESULT || deviceState == CANCELED) {
      resetMeasurement();
    }
  }
} 

// ======================================================
// SENSOR UPDATE
// ======================================================
void updateHeartRate() {
  long irValue = particleSensor.getIR();

  if (irValue > 50000) {
    if (checkForBeat(irValue) == true) {
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60000.0 / delta;

      if (beatsPerMinute < 255 && beatsPerMinute > 40) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;

        if (beatCount < RATE_SIZE) {
          beatCount++;
        }

        beatAvg = 0;

        for (byte x = 0; x < RATE_SIZE; x++) {
          beatAvg += rates[x];
        }

        if (beatCount > 0) {
          beatAvg /= beatCount;
        }
      }
    }
  } else {
    beatAvg = 0;
    beatCount = 0;

    for (byte x = 0; x < RATE_SIZE; x++) {
      rates[x] = 0;
    }
  }
}

void updateTemperature() {
  if (millis() - lastTempRequest >= tempInterval) {
    float tempC_raw = sensors.getTempCByIndex(0);

    sensors.requestTemperatures();
    lastTempRequest = millis();

    if (tempC_raw != DEVICE_DISCONNECTED_C && tempC_raw > -127) {
      tempC_final = tempC_raw + KALIBRASI_OFFSET;
    }
  }
}

// ======================================================
// HANDLE MEASUREMENT 60 DETIK
// ======================================================
void handleMeasurement() {
  if (deviceState != MEASURING) {
    return;
  }

  unsigned long elapsed = millis() - measurementStartTime;

  showMeasuringScreen(elapsed);

  bool inLast10Seconds =
    elapsed >= (MEASUREMENT_DURATION - LAST_WINDOW_DURATION) &&
    elapsed <= MEASUREMENT_DURATION;

  if (inLast10Seconds &&
      millis() - lastAverageSampleTime >= SAMPLE_INTERVAL) {

    lastAverageSampleTime = millis();

    if (beatAvg > 0 && tempC_final > 0 && gsrDataValid) {
      sumHR += beatAvg;
      sumTemp += tempC_final;
      sumEDA += shared_GSR_uS;
      avgCount++;
    
      if (lastSampleCount < MAX_LAST_SAMPLES) {
        lastSampleHR[lastSampleCount] = beatAvg;
        lastSampleTemp[lastSampleCount] = tempC_final;
        lastSampleEDA[lastSampleCount] = shared_GSR_uS;
        lastSampleCount++;
      }
    
      Serial.print("[AVG] Sample added. Count: ");
      Serial.println(avgCount);
    } else {
      Serial.println("[AVG] Sample skipped. Invalid data.");
    }
  }

  if (elapsed >= MEASUREMENT_DURATION) {
    finishMeasurement();
  }
}

// ======================================================
// DEBUG PRINT
// ======================================================
void printDebugData() {
  if (millis() - lastPrintTime < printInterval) {
    return;
  }

  lastPrintTime = millis();

  Serial.print("STATE:");

  if (deviceState == READY) Serial.print("READY");
  else if (deviceState == MEASURING) Serial.print("MEASURING");
  else if (deviceState == RESULT) Serial.print("RESULT");
  else if (deviceState == CANCELED) Serial.print("CANCELED");

  Serial.print(" | WiFi:");
  Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "OFF");

  Serial.print(" | WS:");
  Serial.print(socketIoConnected ? "OK" : "OFF");

  Serial.print(" | BPM:");
  Serial.print(beatAvg);

  Serial.print(" | Temp:");
  Serial.print(tempC_final, 2);

  Serial.print(" | EDA:");
  Serial.print(shared_GSR_uS, 2);

  Serial.print(" | GSR valid:");
  Serial.print(gsrDataValid ? "YES" : "NO");

  Serial.print(" | Queue:");
  Serial.println(countOfflineRecords());
}

// ======================================================
// SETUP
// ======================================================
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== ESP32 STRESS DETECTOR START ===");

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("[OLED] Failed");
    oledReady = false;
  } else {
    Serial.println("[OLED] OK");
    oledReady = true;
    showWiFiConnectingScreen();
  }

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS failed");
  } else {
    Serial.println("[FS] LittleFS OK");
  }
  
  WiFi.setSleep(false);

  bool wifiOK = connectWiFiQuick();

  if (wifiOK) {
    startWebSocketIfOnline();
  } else {
    socketIoConnected = false;
    webSocketStarted = false;
  }

  sensors.begin();
  sensors.setResolution(11);
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  lastTempRequest = millis();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("[MAX30102] ERROR. Check wiring.");
  } else {
    Serial.println("[MAX30102] OK");
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
  }

  NanoSerial.begin(9600, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
  NanoSerial.setTimeout(5);

  Serial.println("[NANO] Serial2 ready for GSR");

  showReadyScreen();
}

// ======================================================
// LOOP
// ======================================================
void loop() {
  if (WiFi.status() == WL_CONNECTED && webSocketStarted) {
    webSocket.loop();
  } else {
    socketIoConnected = false;
  }

  readGsrFromNano();
  updateHeartRate();
  updateTemperature();

  handleButton();
  handleMeasurement();

  unsigned long now = millis();

  if (now - lastTimeWS >= INTERVAL_WS) {
    lastTimeWS = now;
    sendWsLiveData();
  }

  handleWiFiAutoRetry();
  handleWsOffButWifiOk();

  static unsigned long lastSyncAttempt = 0;
  const unsigned long SYNC_INTERVAL = 30000;

  if (deviceState == READY &&
      WiFi.status() == WL_CONNECTED &&
      countOfflineRecords() > 0 &&
      millis() - lastSyncAttempt >= SYNC_INTERVAL) {

    lastSyncAttempt = millis();

    bool ok = syncOfflineRecords();

    if (ok) {
      showSyncSuccessScreen();
    } else {
      showSyncFailedScreen();
    }
  }

  printDebugData();
}
