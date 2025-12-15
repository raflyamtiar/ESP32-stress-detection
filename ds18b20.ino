#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Offset kalibrasi
const float KALIBRASI_OFFSET = 0.70; 

// --- Variabel Millis ---
unsigned long previousMillis = 0;  // Menyimpan waktu terakhir update
const long interval = 1000;        // Interval pembacaan (1000ms = 1 detik)

void setup(void)
{
  Serial.begin(115200); 
  Serial.println("Mulai pembacaan sensor DS18B20 (Non-blocking)...");
  
  sensors.begin();
  sensors.setResolution(11); 
}

void loop(void)
{
  // Ambil waktu sekarang
  unsigned long currentMillis = millis();

  // Cek apakah sudah 1 detik sejak update terakhir
  if (currentMillis - previousMillis >= interval) {
    // Simpan waktu update sekarang untuk referensi berikutnya
    previousMillis = currentMillis;

    // --- Mulai proses pembacaan suhu ---
    sensors.requestTemperatures();  
    float tempC_raw = sensors.getTempCByIndex(0);

    if(tempC_raw == DEVICE_DISCONNECTED_C) 
    {
      Serial.println("Error: Tidak dapat membaca suhu. Cek koneksi sensor!");
    }  
    else  
    {
      float tempC_terkalibrasi = tempC_raw + KALIBRASI_OFFSET;

      Serial.print("Suhu Tubuh: ");
      Serial.print(tempC_terkalibrasi, 2); 
      Serial.println(" °C");
    }
  }


}
