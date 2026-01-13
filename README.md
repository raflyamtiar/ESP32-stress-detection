# ESP32 Stress Detection

Firmware ESP32 untuk eksperimen deteksi stres berbasis tiga sinyal fisiologis. Sketch utama
menggabungkan pembacaan **MAX30102** (detak jantung), **DS18B20** (suhu kulit), dan **Grove
GSR** (konduktansi kulit) lalu mengirimkan data realtime ke Socket.IO melalui terowongan
Ngrok. Modul GSR dikondisikan oleh **Arduino Nano** terpisah yang terus menerus mengirim
string hasil pengukuran ke ESP32 via `Serial2`.

Fitur kunci:

- Koneksi Wi-Fi otomatis: coba kredensial hardcode terlebih dahulu, jika gagal masuk mode
  captive portal WiFiManager.
- WebSocket SSL + Socket.IO handshake manual (`40`/`42`) agar kompatibel dengan endpoint
  Ngrok.
- Streaming data 10 Hz (`INTERVAL_WS = 100 ms`) lengkap dengan heartbeat Socket.IO.
- Serial monitor menampilkan status WS/Wi-Fi bersama metrik GSR (RAW, VCC, Vout, R, uS)
  untuk memudahkan debugging lapangan.

## Dependensi Perangkat & Library

### Perangkat keras

- ESP32 DevKit (board lain yang kompatibel I/O juga bisa)
- Sensor MAX30102 + kabel I2C
- Sensor suhu DS18B20 (mode OneWire)
- Grove GSR v1.2 (atau sensor EDA sejenis) yang dibaca oleh Arduino Nano
- Arduino Nano (mengukur GSR, mengirim data teks ke ESP32 via UART)
- Level shifter / konverter tegangan jika diperlukan antara Nano ↔ ESP32

### Library Arduino

- `WiFi.h` (sudah bawaan core ESP32)
- `WiFiManager` (library oleh tzapu)
- `WebSocketsClient` (dari library "arduinoWebSockets")
- `MAX30105` dan `heartRate` (SparkFun MAX3010x sensor library)
- `OneWire` + `DallasTemperature`
- `SoftwareSerial` (untuk sketch `nano.ino`)

Pastikan board package **ESP32** terbaru sudah terpasang melalui Boards Manager.

## Struktur Sketch

| File                                              | Fungsi Singkat                                                                                                                                                                                                         |
| ------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `integration_websocket/integration_websocket.ino` | Firmware utama. Menggabungkan pembacaan MAX30102 & DS18B20, menerima data GSR dari Nano lewat `Serial2`, lalu mengirim JSON `esp32_live_data` ke Socket.IO (Ngrok) serta menampilkan status lengkap di Serial Monitor. |
| `integration_sensor.ino`                          | Integrasi sensor lokal tanpa koneksi websocket. Cocok untuk men-tuning filter BPM/suhu sebelum jaringan dinyalakan.                                                                                                    |
| `nano.ino`                                        | Firmware Arduino Nano untuk sensor Grove GSR. Menghitung resistansi, VCC aktual, tegangan output, dan konduktansi mikroSiemens, lalu menyiarkan string hasil ke ESP32.                                                 |
| `GSR.ino`                                         | Sketch kalibrasi GSR versi ESP32 (langsung membaca ADC34). Menyediakan polinomial orde 4 hasil regresi Desember 2025.                                                                                                  |
| `max30102.ino`                                    | Uji mandiri sensor MAX30102. Fokus pada deteksi jari, perhitungan BPM rata-rata (4 sampel), dan pencetakan hanya jika nilai berubah.                                                                                   |
| `ds18b20.ino`                                     | Pembacaan suhu non-blocking (berbasis `millis`) dengan offset kalibrasi. Berguna mengecek penempatan sensor sebelum digabungkan.                                                                                       |
| `esp32.ino`                                       | Tes koneksi Wi-Fi paling sederhana untuk memastikan kredensial benar dan ESP32 mendapat alamat IP.                                                                                                                     |
| `ads1115/`                                        | Eksperimen tambahan untuk membaca sensor via ADC eksternal ADS1115.                                                                                                                                                    |

## Cara Pakai (Sketch Utama)

1. **Instal library** yang disebutkan pada bagian Dependensi melalui Library Manager (untuk ESP32 dan Arduino Nano).
2. Flash `nano.ino` ke Arduino Nano, lalu hubungkan TX Nano → RX2 ESP32 (GPIO16) dan GND bersama.
3. Buka `integration_websocket/integration_websocket.ino` di Arduino IDE / PlatformIO.
4. Sesuaikan parameter:
   - `hardcode_ssid` / `hardcode_pass` (opsional, WiFiManager akan muncul jika gagal).
   - `ws_host`, `ws_port`, `ws_path` untuk alamat Ngrok / Socket.IO server.
   - `INTERVAL_WS`, `KALIBRASI_OFFSET`, atau filter lain jika diperlukan.
5. Pilih board **ESP32 Dev Module** dan port yang benar, lalu upload sketch.
6. Buka Serial Monitor (115200 baud). Kamu akan melihat baris debug seperti:

   ```
   WS:ON | WiFi:OK || BPM:73 | Temp:32.15 | RAW:410.0 | VCC:4.978 | Vout:2.315 | R:123456.00 | uS:8.1234
   ```

   Nilai `WS` menunjukkan status Socket.IO, sedangkan kolom GSR membantu mengecek apakah elektroda terpasang dengan baik.

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

- Jalankan sketch pecahan (`GSR.ino`, `max30102.ino`, dll.) jika salah satu sensor bermasalah
  agar debugging lebih terfokus.
- `integration_sensor.ino` bisa dipakai ketika jaringan belum dibutuhkan; lebih ringan dan
  cocok untuk tuning awal.
- Jika `ws_host` berubah (Ngrok membuat URL baru), update nilai tersebut sebelum upload.
- `WiFiManager` akan membuat AP bernama `ESP32_raply` ketika tidak bisa tersambung Wi-Fi;
  buka portal 192.168.4.1 untuk memasukkan SSID/Password baru.
- Untuk Grove GSR, pastikan kulit sedikit lembap; resistansi udara (RAW tinggi) akan membuat
  `shared_GSR_uS` turun ke 0.

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
