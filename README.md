# ESP32-stress-detection

Firmware ESP32 untuk eksperimen deteksi stres berbasis tiga sensor fisiologis:

1. **MAX30102** untuk detak jantung (BPM)
2. **DS18B20** untuk suhu tubuh
3. **Grove GSR** untuk konduktansi kulit (EDA)

Sketch utama (`integration_websocket.ino`) membaca ketiga sensor secara non-blocking,
melakukan filtrasi ringan, lalu mengirimkan data realtime ke server Socket.IO melalui
WebSocket (dalam repo ini diarahkan ke endpoint Ngrok). File `.ino` lain dipertahankan
sebagai _single-sensor playground_ agar mudah melakukan kalibrasi/diagnostik masing-masing
sensor tanpa harus menjalankan keseluruhan sistem.

## Dependensi Perangkat & Library

### Perangkat keras

- ESP32 DevKit (board lain yang kompatibel I/O juga bisa)
- Sensor MAX30102 + kabel I2C
- Sensor suhu DS18B20 (mode OneWire)
- Grove GSR v1.2 (atau sensor EDA sejenis) ke pin ADC34

### Library Arduino

- `WiFi.h` (sudah bawaan core ESP32)
- `WebSocketsClient` (dari library "arduinoWebSockets")
- `MAX30105` dan `heartRate` (SparkFun MAX3010x sensor library)
- `OneWire` + `DallasTemperature`

Pastikan board package **ESP32** terbaru sudah terpasang melalui Boards Manager.

## Struktur Sketch

| File                        | Fungsi Singkat                                                                                                                                                                                                                                           |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `integration_websocket.ino` | Firmware utama. Menggabungkan pembacaan MAX30102, GSR, dan DS18B20 dengan filter dan interval berbeda, lalu mengirim JSON ke Socket.IO melalui WebSocket SSL (bisa diarahkan ke Ngrok/Cloud server). Menampilkan data di Serial Monitor untuk debugging. |
| `integration_sensor.ino`    | Integrasi lokal tanpa WebSocket. Cocok untuk men-tuning filter GSR/HR/suhu sambil melihat output serial sebelum data diteruskan ke jaringan.                                                                                                             |
| `GSR.ino`                   | Sketch khusus kalibrasi Grove GSR. Menerapkan oversampling, konversi ADC12→ADC10, dan polinomial orde-4 berbasis data kalibrasi Desember 2025.                                                                                                           |
| `max30102.ino`              | Uji mandiri sensor MAX30102. Fokus pada deteksi jari, perhitungan BPM rata-rata (4 sampel), dan pencetakan hanya jika nilai berubah.                                                                                                                     |
| `ds18b20.ino`               | Pembacaan suhu non-blocking (berbasis `millis`) dengan offset kalibrasi +0.7 °C. Berguna mengecek penempatan sensor sebelum digabungkan.                                                                                                                 |
| `esp32.ino`                 | Tes koneksi Wi-Fi paling sederhana untuk memastikan kredensial benar dan ESP32 mendapat alamat IP.                                                                                                                                                       |

## Cara Pakai (Sketch Utama)

1. **Instal library** yang disebutkan pada bagian Dependensi melalui Library Manager.
2. Buka `integration_websocket.ino` di Arduino IDE / PlatformIO.
3. Ubah variabel berikut sesuai kebutuhan:
   - `ssid` dan `password` untuk Wi-Fi lokal.
   - `ws_host`, `ws_port`, `ws_path` untuk server Socket.IO (contoh repo memakai URL Ngrok).
   - Jika perlu, sesuaikan `IR_FINGER_THRESHOLD`, konstanta EMA GSR, serta `KALIBRASI_SUHU`.
4. Pilih board **ESP32 Dev Module** (atau varian yang digunakan) dan port yang benar.
5. Upload sketch. Pantau Serial Monitor (115200 baud) untuk melihat BPM, suhu, EDA, dan status WebSocket.

## Payload WebSocket

Sketch utama mengirim event Socket.IO bernama `esp32_live_data` berformat JSON:

```json
{
	"hr": <float>,
	"temp": <float>,
	"eda": <float>,
	"device_id": "ESP32_001"
}
```

Server dapat men-decode event ini untuk menampilkan dashboard, menyimpan ke database,
atau menjalankan analisis stres.

## Tips Kalibrasi & Debugging

- Jalankan sketch pecahan jika salah satu sensor bermasalah agar debugging lebih terfokus.
- Gunakan `integration_sensor.ino` untuk memastikan seluruh pipeline sensor stabil sebelum
  menyalakan koneksi WebSocket.
- Untuk Grove GSR, pastikan tangan dibasahi ringan ketika menguji karena resistansi udara
  akan memaksa pembacaan nol (uS = 0).

## Diagram & Alur Desain

Folder `Design/` berisi kumpulan diagram pendukung agar tim hardware maupun software punya
referensi visual yang konsisten:

| File                             | Deskripsi                                                                                                                                |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `Desain Arsitektur Jaringan.png` | Skema konektivitas antara ESP32, Wi-Fi lokal, Ngrok/Socket.IO server, dan klien pemantau. Cocok dijadikan acuan saat men-deploy backend. |
| `Desain Elektrik.png`            | Wiring diagram sensor MAX30102, DS18B20, dan Grove GSR ke pin ESP32 (termasuk kebutuhan power & level I/O).                              |
| `Desain Mekanik 2.png`           | Ilustrasi penempatan fisik sensor/enclosure sehingga perangkat tetap ergonomis untuk pengguna.                                           |
| `desain software.png`            | Alur perangkat lunak mulai dari pembacaan sensor, filtering, pengemasan JSON, hingga konsumsi data di aplikasi.                          |
| `diagram blok.png`               | Ringkasan blok fungsional sistem (sensor layer → pengolahan → komunikasi → interface pengguna).                                          |

Selamat Bereksperimen!