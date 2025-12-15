#include <WiFi.h>  
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <OneWire.h>
#include <DallasTemperature.h>

const char* ssid     = "POCO X3 Pro";
const char* password = "12321213";

// ============================================================
// SETTINGAN OPTIMAL GSR (ANALOG)
// ============================================================
#define GSR_PIN 34
const float ADC10_MIN = 35.0f;
const float ADC10_MAX = 800.0f; // Range lebar agar aman
const float BATAS_UDARA = 600.0f; // Threshold: Di atas ini = Udara (0)

// Rumus Polinomial GSR (JANGAN DIUBAH)
const float K4 = 5.58351e-06f;
const float K3 = -3.11704e-03f;
const float K2 = 1.110639f;
const float K1 = 250.926318f;
const float K0 = 44025.78807f;

float shared_GSR_uS = 0.0; 
float shared_GSR_Ohm = 0.0;

// ============================================================
// SETTINGAN OPTIMAL MAX30102 (DIGITAL)
// ============================================================
MAX30105 particleSensor;

// POWER LED JANTUNG (Ubah disini untuk sensitivitas Jantung)
// 0x0A = Low (Hemat), 0x1F = Medium (Stabil), 0x3F = High (Powerfull)
const byte POWER_LED = 0x1F; 

const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
float beatsPerMinute;
int beatAvg = 0;
float shared_BPM = 0.0;

// ============================================================
// SETTINGAN LAIN (Suhu & Timing)
// ============================================================
#define DS18B20_PIN 4
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
float shared_Temp = 0.0;
const float KALIBRASI_SUHU = 0.70; 

unsigned long lastTimeGSR = 0;
unsigned long lastTimeTemp = 0;
unsigned long lastTimePrint = 0;
unsigned long lastTimeFingerCheck = 0;

const int INTERVAL_GSR = 100;    
const int INTERVAL_TEMP = 1000;   
const int INTERVAL_PRINT = 200;   
const int INTERVAL_FINGER = 50;   

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- SYSTEM STARTING ---");

  // WIFI
  WiFi.mode(WIFI_STA); 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n✅ WIFI CONNECTED");

  // 1. SETUP MAX30102 (OPTIMASI DIGITAL)
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("❌ MAX30102 ERROR"); while (1);
  }
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(POWER_LED); // Pakai settingan Medium (0x1F)
  particleSensor.setPulseAmplitudeGreen(0);

  // 2. SETUP GSR (OPTIMASI ANALOG)
  analogReadResolution(12);       
  analogSetAttenuation(ADC_11db); 
  pinMode(GSR_PIN, INPUT);

  // 3. SETUP SUHU
  sensors.begin();
  sensors.setResolution(11);
  sensors.setWaitForConversion(false); 
  sensors.requestTemperatures();       
}

void loop() {
  unsigned long currentMillis = millis();

  // --- A. MAX30102 PROCESS ---
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

  // --- B. GSR PROCESS (OPTIMASI FILTER) ---
  if (currentMillis - lastTimeGSR >= INTERVAL_GSR) {
    lastTimeGSR = currentMillis;
    long sum = 0;
    for (int i = 0; i < 20; i++) sum += analogRead(GSR_PIN);
    float adc12 = sum / 20.0;
    float adc10 = adc12 * (1023.0f / 4095.0f);
    
    // Logika Threshold (Pisahkan Udara vs Sentuh)
    if (adc10 >= ADC10_MIN && adc10 <= ADC10_MAX) {
      if (adc10 > BATAS_UDARA) {
         // Di atas 600 = Udara -> Paksa 0
         shared_GSR_Ohm = 0.0f; shared_GSR_uS = 0.0f;
      } else {
         // Di bawah 600 = Sentuh -> Hitung
         float x = adc10;
         float R_est = (((K4 * x + K3) * x + K2) * x + K1) * x + K0;
         if (R_est > 0.0f) {
            shared_GSR_Ohm = R_est;
            shared_GSR_uS = (1.0f / R_est) * 1e6; 
         }
      }
    } else {
      shared_GSR_Ohm = 0.0f; shared_GSR_uS = 0.0f; 
    }
  }

  // --- C. SUHU PROCESS ---
  if (currentMillis - lastTimeTemp >= INTERVAL_TEMP) {
    lastTimeTemp = currentMillis;
    float tempC = sensors.getTempCByIndex(0);
    if (tempC > -100) shared_Temp = tempC + KALIBRASI_SUHU;
    sensors.requestTemperatures();
  }

  // --- D. DISPLAY ---
  if (currentMillis - lastTimePrint >= INTERVAL_PRINT) {
    lastTimePrint = currentMillis;
    Serial.print("BPM: "); Serial.print(shared_BPM, 0);
    Serial.print(" | Temp: "); Serial.print(shared_Temp, 2);
    Serial.print(" | uS: "); Serial.print(shared_GSR_uS, 2);
    Serial.print(" | Ohm: "); Serial.println(shared_GSR_Ohm, 0);
  }
}
