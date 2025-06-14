# 🔌 Smart Fish Tank - Otomatisasi Jadwal Pengurasan Air dengan ESP32 & Firebase

Sistem ini dirancang untuk **mengontrol proses pengurasan air** pada akuarium secara otomatis menggunakan **ESP32** dan terintegrasi dengan **Firebase** (Realtime Database & Firestore). Proses kontrol dilakukan berdasarkan **jadwal yang ditentukan melalui Firestore**, dan semua aktivitas dicatat ke dalam **histori**.

---

## ✨ Fitur Utama

- 🔁 **Otomatisasi Relay**: Mengontrol pompa melalui jadwal yang tersimpan di Firestore.
- 🔄 **Sinkronisasi Realtime**: Status relay otomatis mengikuti data dari Firebase Realtime Database (RTDB).
- 🕒 **Penjadwalan Berbasis Waktu NTP**: Menggunakan waktu dari server NTP dan konversi ke WIB (UTC+7).
- 🗃️ **Manajemen Jadwal**: Setelah jadwal dijalankan, data akan dipindahkan ke `drainageHistory` dan dihapus dari `drainageSchedules`.

---

## 🔧 Perangkat Keras yang Dibutuhkan

- [ESP32 Development Board](https://www.espressif.com/en/products/socs/esp32)
- Relay 5V (untuk mengontrol pompa)
- Pompa DC atau solenoid valve (opsional)
- Koneksi WiFi stabil

---

## 📦 Library yang Digunakan

| Library | Fungsi |
|--------|--------|
| [`WiFi.h`](https://www.arduino.cc/en/Reference/WiFi) | Menghubungkan ESP32 ke jaringan WiFi |
| [`Firebase ESP Client`](https://github.com/mobizt/Firebase-ESP-Client) | Mengakses Firebase Realtime Database & Firestore |
| [`ArduinoJson`](https://arduinojson.org/) | Parsing dan manipulasi JSON dari Firebase |
| [`TokenHelper.h`](https://github.com/mobizt/Firebase-ESP-Client/blob/main/src/addons/TokenHelper.h) | Otentikasi dan refresh token Firebase secara otomatis |

---

## 📁 Struktur Firebase

### 🔥 Firestore
- **`drainageSchedules`** (Collection) → berisi dokumen jadwal dengan:
  - `timestamp` (Firestore Timestamp)
  - `duration` (Integer, durasi dalam menit)

- **`drainageHistory`** (Collection) → hasil pemindahan dari `drainageSchedules` setelah jadwal selesai dieksekusi.

### 🌐 Realtime Database
- `/sensors/relay` → 0 atau 1 untuk mengontrol `RELAY_PIN`
- `/sensors/relay2` → 0 atau 1 untuk mengontrol `RELAY2_PIN`

---

## 🧠 Cara Kerja

1. **ESP32 terhubung ke WiFi & Firebase** menggunakan email/password.
2. **Waktu disinkronkan via NTP** dan dikonversi ke zona waktu WIB.
3. Setiap 60 detik, ESP32 mengecek `drainageSchedules` di Firestore:
   - Jika ada jadwal yang cocok dengan waktu saat ini → nyalakan relay.
   - Setelah waktu habis → matikan relay.
   - Pindahkan jadwal ke histori dan hapus dari schedules.
4. Setiap 1 detik, ESP32 membaca status relay dari RTDB dan memperbarui output relay.

---

## ✅ Output di Serial Monitor

- `🎯 Jadwal cocok! Menyalakan relay…`
- `🛑 Relay OFF`
- `✅ Schedule moved to history.`
- `✅ Schedule deleted.`
- Dan status WiFi/Firebase/Relay lainnya.

---

## 🚀 Deploy & Upload

1. **Install semua library di Arduino IDE** (gunakan Library Manager atau install manual).
2. **Ganti kredensial WiFi dan Firebase** di bagian konfigurasi.
3. Upload ke board ESP32.

