#include <Wire.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;

// === SETTING WAJIB ===
// Isi sesuai hasil ukur tegangan 3V3 ESP32 pakai multimeter (misal 3.28 atau 3.30)
const float VCC = 3.30;

// sampling
const int N = 30;
const int dt_ms = 5;

// kalibrasi (Serial_calibration dari Seeed)
int cal10 = -1;   // nilai 0..1023
bool calibrated = false;

// helper: clamp
int clampInt(int x, int lo, int hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (!ads.begin(0x48)) {
    Serial.println("ADS1115 ga ketemu di 0x48. Cek SDA/SCL/ADDR.");
    while (1) delay(1000);
  }

  // Range input aman buat 0..3.3V
  ads.setGain(GAIN_TWO); // ±2.048V (resolusi lebih halus buat sinyal <2V)
  // Kalau output SIG kamu bisa >2.048V, ganti ke GAIN_ONE.

  Serial.println("\n=== Grove GSR via ADS1115 A0 ===");
  Serial.println("Command: ketik 'B' lalu Enter untuk kalibrasi (NO CONTACT).");
  Serial.println("Kalibrasi: lepas elektroda dari jari dulu, tunggu stabil, lalu tekan B.");
}

void loop() {
  // baca command kalibrasi
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'B' || c == 'b') {
      // ambil rata-rata tegangan saat NO CONTACT
      long sum = 0;
      for (int i = 0; i < N; i++) {
        sum += ads.readADC_SingleEnded(0);
        delay(dt_ms);
      }
      int16_t meanCounts = (int16_t)(sum / N);
      float v = ads.computeVolts(meanCounts);

      // convert tegangan -> "ADC 10-bit virtual" 0..1023
      int adc10 = (int)lround((v / VCC) * 1023.0);
      adc10 = clampInt(adc10, 0, 1023);

      cal10 = adc10;
      calibrated = true;

      Serial.print("[CAL] V_noTouch=");
      Serial.print(v, 4);
      Serial.print(" V | cal10=");
      Serial.println(cal10);
    }
  }

  // ambil sampel
  long sum = 0;
  long sumsq = 0;
  int16_t cmin = 32767, cmax = -32768;

  for (int i = 0; i < N; i++) {
    int16_t c = ads.readADC_SingleEnded(0);
    sum += c;
    sumsq += (long)c * (long)c;
    if (c < cmin) cmin = c;
    if (c > cmax) cmax = c;
    delay(dt_ms);
  }

  float mean = (float)sum / N;
  float var = ((float)sumsq / N) - (mean * mean);
  if (var < 0) var = 0;
  float stdCounts = sqrt(var);

  int16_t meanCounts = (int16_t)lround(mean);
  float v = ads.computeVolts(meanCounts);

  // convert tegangan -> 10-bit virtual (0..1023) biar kompatibel rumus Seeed
  int adc10 = (int)lround((v / VCC) * 1023.0);
  adc10 = clampInt(adc10, 0, 1023);

  // hitung R & G pakai rumus Seeed (kalau sudah kalibrasi)
  bool canCalc = calibrated && (cal10 - adc10) > 1;  // harus positif biar ga negatif/inf
  float R = NAN;
  float GuS = NAN;

  if (canCalc) {
    // Human Resistance = ((1024 + 2*Reading)*10000)/(cal - Reading)
    R = ((1024.0 + 2.0 * adc10) * 10000.0) / (float)(cal10 - adc10);
    if (R > 0) GuS = 1000000.0 / R;
  }

  Serial.print("V_SIG=");
  Serial.print(v, 4);
  Serial.print(" V | adc10=");
  Serial.print(adc10);

  if (!calibrated) {
    Serial.print(" | cal10=NA (tekan B)");
  } else {
    Serial.print(" | cal10=");
    Serial.print(cal10);
  }

  Serial.print(" | stdCounts=");
  Serial.print(stdCounts, 2);

  if (canCalc) {
    Serial.print(" | R=");
    Serial.print(R, 0);
    Serial.print(" ohm | G=");
    Serial.print(GuS, 3);
    Serial.print(" uS");
  } else {
    Serial.print(" | R=N/A | G=N/A");
    if (calibrated && (cal10 - adc10) <= 1) {
      Serial.print(" (cal10 harus > adc10; putar pot biar NO CONTACT lebih tinggi dari CONTACT)");
    }
  }

  Serial.println();
  delay(250);
}
