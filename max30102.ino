#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h" // Pastikan Anda sudah meng-install library ini

MAX30105 particleSensor;

// --- Konfigurasi Rata-rata BPM ---
const byte RATE_SIZE = 4; // Ambil rata-rata dari 4 detak terakhir
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 

float beatsPerMinute;
int beatAvg; 
int bpmTercetakTerakhir = 0; // Penyimpan nilai BPM terakhir yang dicetak
byte beatCount = 0; 

// --- Konfigurasi Timer & Mode ---
unsigned long lastPrintTime = 0; 
long printInterval = 40; // Sampling rate 25 Hz (40 ms)

void setup()
{
  Serial.begin(115200);
  Serial.println("Inisialisasi Sensor MAX30102...");

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) 
  {
    Serial.println("MAX30102 tidak ditemukan. Periksa kabel/power.");
    while (1); 
  }
  
  Serial.println("Letakkan jari telunjuk Anda di sensor dengan stabil.");
  
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x0A); // Sesuaikan jika perlu
  particleSensor.setPulseAmplitudeGreen(0); 

  Serial.println("Mulai mengukur BPM...");
}

void loop()
{
  long irValue = particleSensor.getIR(); 

  // --- Logika Deteksi Detak Jantung ---
  if (checkForBeat(irValue) == true)
  {
    long delta = millis() - lastBeat; 
    lastBeat = millis();
    
    beatsPerMinute = 60000.0 / delta; // Rumus BPM

    // Filter data BPM yang valid (40-255 bpm)
    if (beatsPerMinute < 255 && beatsPerMinute > 40)
    {
      // Masukkan data baru ke array rata-rata
      rates[rateSpot++] = (byte)beatsPerMinute; 
      rateSpot %= RATE_SIZE; 
      
      // Tambah counter data valid, maks sejumlah RATE_SIZE
      if (beatCount < RATE_SIZE) beatCount++; 

      // Hitung nilai rata-rata baru
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++)
        beatAvg += rates[x];
      
      if (beatCount > 0) beatAvg /= beatCount;
    }
  }

  // --- Logika Pencetakan (Timer) ---
  unsigned long currentTime = millis();
  
  if (currentTime - lastPrintTime >= printInterval)
  {
    lastPrintTime = currentTime; // Reset timer

    // KASUS 1: Jari tidak terdeteksi
    if (irValue < 50000) // Nilai threshold, sesuaikan jika perlu
    {
      Serial.println("Jari tidak terdeteksi. Letakkan jari pada sensor.");
      
      // Reset semua variabel ke kondisi awal
      beatAvg = 0; 
      bpmTercetakTerakhir = 0; // RESET
      beatCount = 0; 
      for (byte x = 0 ; x < RATE_SIZE ; x++) rates[x] = 0; 
    }
    // KASUS 2: Data stabil, siap dicetak
    else
    {
      // --- LOGIKA PINTAR: HANYA CETAK JIKA NILAI BERUBAH ---
      if (beatAvg != bpmTercetakTerakhir) 
      {
        Serial.print("=======================\n");
        Serial.print("Rata-rata BPM: ");
        Serial.print(beatAvg);
        Serial.println(" bpm");
        Serial.println("=======================");
        
        // Simpan nilai yang baru dicetak
        bpmTercetakTerakhir = beatAvg; 
      }
    }
  }  
}
