#include <SoftwareSerial.h>

const int GSR = A0;
const int serial_calibration = 525; // nilai kalibrasi (baseline OPEN + offset)

int sensorValue = 0;
int gsr_average = 0;
float human_resistance = 0.0;
float gsr_uS = 0.0;

// Kirim ke ESP32 via SoftwareSerial (RX, TX)
// TX = D3 -> level shifter HV1
SoftwareSerial toESP32(2, 3); // RX=D2 (opsional), TX=D3

// Baca VCC (mV) pakai internal bandgap 1.1V (ATmega328P / Arduino Nano)
long readVcc_mV() {
  // set reference ke AVcc, dan pilih channel internal 1.1V (bandgap)
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // tunggu referensi stabil

  ADCSRA |= _BV(ADSC);                 // start conversion
  while (bit_is_set(ADCSRA, ADSC));    // tunggu selesai

  uint16_t result = ADC;               // hasil ADC bandgap
  // 1125300 = 1.1V * 1023 * 1000 (konstanta umum)
  long vcc = 1125300L / result;
  return vcc; // dalam mV
}

void setup() {
  Serial.begin(9600);
  toESP32.begin(9600);

  Serial.println("=== GSR Sensor Monitor (Auto Vref) ===");
  Serial.println();
}

void loop() {
  long sum = 0;

  // ambil 10 sampel, rata-rata
  for (int i = 0; i < 10; i++) {
    sensorValue = analogRead(GSR);
    sum += sensorValue;
    delay(5);
  }
  gsr_average = sum / 10;

  // VCC otomatis (Volt)
  float VCC = readVcc_mV() / 1000.0;

  // Tegangan output A0 (pakai VCC aktual)
  float voltage = (gsr_average / 1023.0) * VCC;

  // Rumus Seeed (pakai serial_calibration)
  float numerator   = (1024.0 + 2.0 * gsr_average) * 10000.0;
  float denominator = (float)serial_calibration - (float)gsr_average;

  // Clamp biar gak negatif / error kalau RAW >= serial_calibration
  if (denominator > 0.0) {
    human_resistance = numerator / denominator;
  } else {
    human_resistance = 0.0;
  }

  // Konduktansi µS
  if (human_resistance > 0.0) {
    gsr_uS = (1.0 / human_resistance) * 1000000.0;
  } else {
    gsr_uS = 0.0;
  }

  // ===== OUTPUT STRING (RAW FIX: jangan pake ", 1" karena RAW itu int) =====
  String line =
    String("RAW: ") + String(gsr_average) +
    " | VCC: " + String(VCC, 3) +
    " V | Vout: " + String(voltage, 3) +
    " V | R_est: " + String(human_resistance, 2) +
    " Ohm | G: " + String(gsr_uS, 3) + " uS";

  // Print ke Serial Nano + kirim ke ESP32
  Serial.println(line);
  toESP32.println(line);

  delay(200);
}
