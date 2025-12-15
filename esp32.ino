#include <WiFi.h>

// --- Ganti dengan detail Wi-Fi Anda ---
const char* ssid = "POCO X3 Pro";       // Masukkan nama Wi-Fi (SSID) Anda di sini
const char* password = "12321213";  // Masukkan password Wi-Fi Anda di sini
// ----------------------------------------

void setup() {
  // Mulai komunikasi Serial untuk menampilkan output
  Serial.begin(115200);
  delay(10); // Beri jeda singkat

  Serial.println();
  Serial.print("Menghubungkan ke jaringan: ");
  Serial.println(ssid);

  // Mulai koneksi Wi-Fi
  WiFi.begin(ssid, password);

  // Loop tunggu (blocking loop) sampai ESP32 terhubung
  // Tanda titik akan dicetak setiap 500ms
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Jika loop di atas selesai, berarti sudah terhubung
  Serial.println("");
  Serial.println("=================================");
  Serial.println("  Wi-Fi BERHASIL TERHUBUNG!  ");
  Serial.println("=================================");
  Serial.print("Alamat IP ESP32: ");
  Serial.println(WiFi.localIP()); // Cetak alamat IP lokal
}

void loop() {
  // Biarkan kosong untuk tes koneksi sederhana ini
  // Atau Anda bisa tambahkan kode untuk cek koneksi berkala
  
  // delay(10000); // Jeda 10 detik
  // if(WiFi.status() != WL_CONNECTED){
  //   Serial.println("Koneksi terputus, mencoba menyambung ulang...");
  //   WiFi.reconnect();
  // }
}
