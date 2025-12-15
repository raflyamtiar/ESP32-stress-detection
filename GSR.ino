/*
 * Grove GSR v1.2 + ESP32 (3.3V)
 * Kalibrasi (Des 2025): 55k, 68k, 100k, 150k, 208k, 280k, 340k, 391k, 551k
 * Mapping: ADC12 (0–4095) -> ADC10 (0–1023) -> R_est (Ohm)
 * Model: R_est = P(ADC10), polinomial orde 4 (Units in kOhm -> converted to Ohm)
 */

const int   GSR_PIN  = 34;    // pakai ADC1 (32/33/34/35 aman)
const float VREF     = 3.3;   // ESP32 3.3V

// RANGE KALIBRASI ADC10 (Updated based on new data: 42 - 570)
const float ADC10_MIN = 35.0f;   // Batas bawah aman
const float ADC10_MAX = 580.0f;  // Batas atas aman (data max 570)

// Koefisien polinomial (Hasil Regresi Orde 4 - Output dalam kOhm)
// y = 8.33E-09x^4 - 6.46E-06x^3 + 0.00237x^2 + 0.0862x + 48.19
const float K4 =  8.32659e-09f;   // x^4
const float K3 = -6.45535e-06f;   // x^3
const float K2 =  0.00237126f;    // x^2
const float K1 =  0.086247663f;   // x
const float K0 =  48.19170232f;   // konstanta

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);       // 0–4095
  analogSetAttenuation(ADC_11db);
  pinMode(GSR_PIN, INPUT);

  Serial.println("=== Grove GSR v1.2 + ESP32 ===");
  Serial.println("Kalibrasi Baru: Polinomial Orde 4");
  Serial.println("Range Valid: ~55k - 551k Ohm");
  Serial.println();
}

void loop() {
  // --- 1. Oversampling biar halus ---
  const int N_SAMPLES = 20;
  long sum = 0;
  for (int i = 0; i < N_SAMPLES; i++) {
    sum += analogRead(GSR_PIN);
    delay(2);
  }

  // rata-rata ADC 12-bit
  float adc12   = sum / (float)N_SAMPLES;           // 0–4095
  float voltage = adc12 * (VREF / 4095.0f);         // Volt

  // konversi ke "ADC 10-bit" (skala 0–1023) sebagai variabel fit
  float adc10 = adc12 * (1023.0f / 4095.0f);

  // --- 2. Flag valid / nggak berdasarkan range kalibrasi ---
  bool in_range = (adc10 >= ADC10_MIN && adc10 <= ADC10_MAX);

  float R_est   = -1.0f;   // Ohm
  float gsr_uS  = 0.0f;    // microSiemens
  const char* status;

  if (in_range) {
    float x = adc10;
    
    // Hitung R dalam kOhm dulu (sesuai unit regresi kita)
    // Horner's Method: Efisien untuk mikrokontroler
    float R_kOhm = (((K4 * x + K3) * x + K2) * x + K1) * x + K0;

    // Konversi ke Ohm (karena sisa kode mengharapkan Ohm)
    R_est = R_kOhm * 1000.0f; 

    if (R_est > 0.0f) {
      gsr_uS = (1.0f / R_est) * 1e6;    // microSiemens (1/Ohm * 1juta)
      status = "OK (DALAM RANGE)";
    } else {
      R_est  = -1.0f;
      gsr_uS = 0.0f;
      status = "ERROR R_NEG";
    }
  } else {
    // DI LUAR RANGE KALIBRASI
    R_est  = -1.0f;
    gsr_uS = 0.0f;

    if (adc10 > ADC10_MAX) {
      status = "LEPAS / UDARA"; // Nilai ADC tinggi biasanya resistansi tak hingga
    } else {
      status = "OUT OF RANGE (RENDAH)"; // Resistansi terlalu kecil (short?)
    }
  }

  // --- 3. Print hasil ---
  Serial.print("ADC12: ");
  Serial.print(adc12, 0); // int is enough
  Serial.print(" | ADC10: ");
  Serial.print(adc10, 1);
  
  Serial.print(" | R_est: ");
  if (R_est > 0.0f) {
    Serial.print(R_est / 1000.0f, 2);   // Tampilkan kOhm
    Serial.print(" kOhm");
  } else {
    Serial.print("-");
  }

  Serial.print(" | GSR: ");
  Serial.print(gsr_uS, 3);
  Serial.print(" uS | ");
  Serial.println(status);

  delay(500);
}
