#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoJson.h>
#include <addons/TokenHelper.h>  // Untuk auto refresh token

// =======================
// Konfigurasi WiFi & Firebase
// =======================
#define WIFI_SSID "HAYOLO"
#define WIFI_PASSWORD "12345678"

#define API_KEY "AIzaSyCmmHx7BdW0dSfvxsoL4WIvVIWOj7F-jIU"
#define DATABASE_URL "https://smart-fish-tank-10016-default-rtdb.asia-southeast1.firebasedatabase.app"
#define PROJECT_ID "smart-fish-tank-10016"

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Firestore collections
const String SCHEDULE_COLLECTION = "drainageSchedules";
const String HISTORY_COLLECTION  = "drainageHistory";

// Relay control
const int RELAY_PIN = 23;
const int RELAY2_PIN = 22;
const long gmtOffset_sec   = 7 * 3600;    // WIB
const int daylightOffset_sec = 0;

// =======================
// Setup
// =======================
void setup() {

  Serial.begin(230400);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY2_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);
  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi Connected");

  // Firebase config & auth
  config.api_key       = API_KEY;
  config.database_url  = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;
  config.timeout.serverResponse = 60 * 1000; // 60 detik

  auth.user.email    = "firebaseesppompa@gmail.com";
  auth.user.password = "12345678";

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // NTP time
  configTime(gmtOffset_sec, daylightOffset_sec, "ntp.bmkg.go.id");
}

// =======================
// Loop
// =======================
unsigned long lastRelayCheck    = 0;
unsigned long lastScheduleCheck = 0;

void loop() {
  if (WiFi.status() != WL_CONNECTED || !Firebase.ready()) {
    Serial.println("Waiting for WiFi/Firebase...");
    delay(5000);
    return;
  }

  time_t now = time(nullptr);
  if (now < 100000) {
    Serial.println("Waiting for NTP...");
    delay(1000);
    return;
  }

  // Update relay state tiap 1 detik
  if (millis() - lastRelayCheck >= 1000) {
    lastRelayCheck = millis();
    updateRelayState();
    updateRelay2State(); 
  }

  // Cek jadwal tiap 1 menit
  if (millis() - lastScheduleCheck >= 60000) {
    lastScheduleCheck = millis();

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char currentTime[30];
    strftime(currentTime, sizeof(currentTime), "%Y-%m-%dT%H:%M:00", &timeinfo);
    Serial.printf("Now: %s\n", currentTime);

    checkAndRunSchedule(currentTime);
  }
}

// ===============================
// Cek Jadwal dan Kontrol Relay
// ===============================
void checkAndRunSchedule(const char* currentTime) {
  if (!Firebase.Firestore.listDocuments(
        &fbdo,
        PROJECT_ID,
        "(default)",
        SCHEDULE_COLLECTION.c_str(),
        5,
        "",
        "",
        "",
        false
      )) {
    Serial.print("Error get documents: ");
    Serial.println(fbdo.errorReason());
    return;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, fbdo.payload());
  if (err) {
    Serial.println("JSON Parse error!");
    return;
  }

  if (!doc.containsKey("documents")) {
    Serial.println("No schedules found.");
    return;
  }

  JsonArray documents = doc["documents"].as<JsonArray>();
  for (JsonObject document : documents) {
    String name = document["name"].as<String>();
    String id   = name.substring(name.lastIndexOf("/") + 1);
    JsonObject fields = document["fields"].as<JsonObject>();

    // Ambil waktu UTC dari Firestore
    String timestampValue = fields["timestamp"]["timestampValue"].as<String>();
    Serial.printf("Checking (raw UTC): %s\n", timestampValue.c_str());

    // Parse UTC
    struct tm scheduleTimeinfo;
    memset(&scheduleTimeinfo, 0, sizeof(scheduleTimeinfo));
    if (strptime(timestampValue.c_str(), "%Y-%m-%dT%H:%M:%SZ", &scheduleTimeinfo) == nullptr) {
      Serial.println("Failed to parse timestamp!");
      continue;
    }

    time_t scheduleTime = mktime(&scheduleTimeinfo) + (7 * 3600); // ke WIB
    localtime_r(&scheduleTime, &scheduleTimeinfo);

    char scheduleLocalTime[30];
    strftime(scheduleLocalTime, sizeof(scheduleLocalTime), "%Y-%m-%dT%H:%M:00", &scheduleTimeinfo);
    Serial.printf("Checking (converted WIB): %s\n", scheduleLocalTime);

    if (String(scheduleLocalTime) == String(currentTime)) {
      Serial.println("🎯 Jadwal cocok! Menyalakan relay…");

      // Baca durasi dari Firestore (menit)
      int durationMinutes = 5;
      if (fields.containsKey("duration") &&
          fields["duration"]["integerValue"].as<const char*>() != nullptr) {
        durationMinutes = atoi(fields["duration"]["integerValue"].as<const char*>());
      }
      Serial.printf("Duration: %d menit\n", durationMinutes);

      // ON
      Firebase.RTDB.setInt(&fbdo, "/sensors/relay", 1);
      digitalWrite(RELAY_PIN, HIGH);

      unsigned long startMillis = millis();
      unsigned long waitMs = (unsigned long)durationMinutes * 60UL * 1000UL;
      while (millis() - startMillis < waitMs) {
        delay(500);
      }

      // OFF
      digitalWrite(RELAY_PIN, LOW);
      Firebase.RTDB.setInt(&fbdo, "/sensors/relay", 0);
      Serial.println("🛑 Relay OFF");

      // Pindah & Hapus jadwal
      moveScheduleToHistory(id, fields);
      deleteSchedule(id);
      break;
    }
  }
}

// ===============================
// Pindahkan ke drainageHistory
// ===============================
void moveScheduleToHistory(const String& id, JsonObject fields) {
  DynamicJsonDocument payload(4096);
  payload["fields"] = fields;

  String jsonStr;
  serializeJson(payload, jsonStr);

  if (Firebase.Firestore.createDocument(
        &fbdo,
        PROJECT_ID,
        "(default)",
        HISTORY_COLLECTION.c_str(),
        id.c_str(),
        jsonStr.c_str(),
        ""
      )) {
    Serial.println("✅ Schedule moved to history.");
  } else {
    Serial.print("❌ Failed to move to history: ");
    Serial.println(fbdo.errorReason());
  }
}

// ===============================
// Hapus Jadwal
// ===============================
void deleteSchedule(const String& id) {
  String docPath = SCHEDULE_COLLECTION + "/" + id;
  if (Firebase.Firestore.deleteDocument(
        &fbdo,
        PROJECT_ID,
        "(default)",
        docPath.c_str()
      )) {
    Serial.println("✅ Schedule deleted.");
  } else {
    Serial.print("❌ Failed to delete schedule: ");
    Serial.println(fbdo.errorReason());
  }
}

// ===============================
// Sync Relay State dari RTDB
// ===============================
void updateRelayState() {
  if (Firebase.RTDB.getInt(&fbdo, "/sensors/relay")) {
    if (fbdo.dataType() == "int") {
      int relayStatus = fbdo.intData();
      digitalWrite(RELAY_PIN, relayStatus ? HIGH : LOW);
      Serial.printf("Relay %s (from Firebase)\n", relayStatus ? "ON" : "OFF");
    }
  } else {
    Serial.print("Failed to get relay status: ");
    Serial.println(fbdo.errorReason());
  }
}

void updateRelay2State() {
  if (Firebase.RTDB.getInt(&fbdo, "/sensors/relay2")) {
    if (fbdo.dataType() == "int") {
      int relay2Status = fbdo.intData();
      if (relay2Status == 1) {
        digitalWrite(RELAY2_PIN, HIGH);
        Serial.println("Relay2 ON (from Firebase)");
      } else {
        digitalWrite(RELAY2_PIN, LOW);
        Serial.println("Relay2 OFF (from Firebase)");
      }
    }
  } else {
    Serial.print("Failed to get relay2 status: ");
    Serial.println(fbdo.errorReason());
  }
}



