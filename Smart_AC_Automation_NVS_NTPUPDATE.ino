/************************************************************
 * SMART CLASSROOM AC AUTOMATION SYSTEM
 * ESP32 + Firebase + NVS + Offline Backup + Speed Monitor
 *
 * FEATURES:
 * ----------------------------------------------------------
 * 1. Fetches schedules from Firebase
 * 2. Stores schedules in ESP32 NVS memory
 * 3. Works offline using saved schedules
 * 4. Controls relay automatically
 * 5. Supports MANUAL / AUTO modes
 * 6. Prints WiFi signal and Firebase fetch speed
 * 7. Prints NVS storage status
 ************************************************************/

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

/* =========================================================
   WIFI CONFIG
   ========================================================= */

#define WIFI_SSID       "KeshavMOTO"
#define WIFI_PASSWORD   "KeshavG123"

/* =========================================================
   FIREBASE CONFIG
   ========================================================= */

#define API_KEY       "AIzaSyB1-t0e-ZKkMEPCdMb5fPmvDmKSwMqIMLQ"
#define DATABASE_URL  "https://smart-ac-automation-default-rtdb.asia-southeast1.firebasedatabase.app"

/* =========================================================
   ROOM CONFIG
   ========================================================= */

#define ROOM_ID "Room_D117"

/* =========================================================
   RELAY CONFIG
   =========================================================
   ACTIVE LOW RELAY:
   ON  = LOW
   OFF = HIGH
   ========================================================= */

#define RELAY_PIN         5
#define RELAY_ON_STATE    LOW
#define RELAY_OFF_STATE   HIGH

/* =========================================================
   TIMING
   ========================================================= */

#define CONTROL_INTERVAL_MS      10000UL
// #define CLOUD_SYNC_INTERVAL_MS   3600000UL  //  for 1 hr
#define CLOUD_SYNC_INTERVAL_MS   5000UL       // for 5 sec
/* =========================================================
   FIREBASE OBJECTS
   ========================================================= */

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

/* =========================================================
   NTP
   ========================================================= */

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);

/* =========================================================
   NVS
   ========================================================= */

Preferences prefs;

/* =========================================================
   VARIABLES
   ========================================================= */

unsigned long lastControlCheck = 0;
unsigned long lastCloudSync = 0;
bool ntpSynced = false;

/* =========================================================
   ROOMS & DAYS
   ========================================================= */

const char* ROOMS[2] = {
  "Room_D047",
  "Room_D117"
};

const char* DAYS[7] = {
  "Mon","Tue","Wed","Thu","Fri","Sat","Sun"
};

/* =========================================================
   FUNCTION DECLARATIONS
   ========================================================= */

String getDayOfWeek();
String getCurrentTimeHHMM();


int hhmmToMinutes(const String &t);

void turnACOn();
void turnACOff();

bool isCloudAvailable();

bool fetchRoomStateFromCloud(
  const String &roomId,
  String &overall,
  String &control,
  String &manual
);

bool fetchDayScheduleFromCloud(
  const String &roomId,
  const String &day,
  String &jsonData
);

void saveRoomStateToNVS(
  const String &overall,
  const String &control,
  const String &manual
);

bool loadRoomStateFromNVS(
  String &overall,
  String &control,
  String &manual
);

void saveDayScheduleToNVS(
  const String &roomId,
  const String &day,
  const String &jsonData
);

bool loadDayScheduleFromNVS(
  const String &roomId,
  const String &day,
  String &jsonData
);

/* =========================================================
   SAVE ROOM STATE TO NVS
   ========================================================= */

void saveRoomStateToNVS(
  const String &overall,
  const String &control,
  const String &manual
) {
  prefs.begin("ROOMSTATE", false);

  prefs.putString("overall", overall);
  prefs.putString("control", control);
  prefs.putString("manual", manual);

  prefs.end();

  Serial.println("💾 Room State Saved To NVS");
}

/* =========================================================
   LOAD ROOM STATE FROM NVS
   ========================================================= */

bool loadRoomStateFromNVS(
  String &overall,
  String &control,
  String &manual
) {
  prefs.begin("ROOMSTATE", true);

  overall = prefs.getString("overall", "");
  control = prefs.getString("control", "");
  manual  = prefs.getString("manual", "");

  prefs.end();

  return overall.length() > 0;
}

void syncAllSchedulesToNVS();

bool applySchedule(
  const String &jsonData,
  const String &today,
  const String &currentTime,
  const String &source
);

void printWiFiStatus();
void printNVSStoredData();

/* =========================================================
   SETUP
   ========================================================= */

void setup() {

  Serial.begin(115200);
  delay(1000);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF_STATE);

  Serial.println("\n======================================");
  Serial.println("SMART AC AUTOMATION SYSTEM STARTING");
  Serial.println("======================================");

  /* ---------- WIFI ---------- */

  Serial.println("\nConnecting to WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int wifiRetry = 0;

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");

    wifiRetry++;

    if (wifiRetry > 40) {
      Serial.println("\n⚠️ WiFi Connection Timeout");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("\n✅ WiFi Connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    printWiFiStatus();
  }

  /* ---------- TIME ---------- */

  timeClient.begin();

  Serial.print("Syncing NTP Time");

  int ntpRetry = 0;

  while (!timeClient.update() && ntpRetry < 20) {

    timeClient.forceUpdate();

    Serial.print(".");

    delay(500);

    ntpRetry++;
  }

  if (ntpRetry >= 20) {

    Serial.println("\n⚠️ NTP Sync Failed");
    Serial.println("⚠️ Continuing with available time");

  } else {

    Serial.println("\n✅ NTP Time Synced");
    ntpSynced = true;
  }

  /* ---------- FIREBASE ---------- */

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  config.timeout.networkReconnect = 10000;
  config.timeout.socketConnection = 15000;
  config.timeout.serverResponse = 15000;

  if (Firebase.signUp(&config, &auth, "", "")) {

    Serial.println("✅ Firebase Login Success");

  } else {

    Serial.print("❌ Firebase Login Failed: ");
    Serial.println(config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("✅ Firebase Ready");

  /* ---------- INITIAL NVS SYNC ---------- */

  syncAllSchedulesToNVS();

  /* ---------- PRINT SAVED DATA ---------- */

  printNVSStoredData();

  lastCloudSync = millis();
}

/* =========================================================
   LOOP
   ========================================================= */

void loop() {

  if (millis() - lastControlCheck < CONTROL_INTERVAL_MS) {
    return;
  }

  lastControlCheck = millis();
  /* ---------- AUTO WIFI RECONNECT ---------- */

  if (WiFi.status() != WL_CONNECTED) {

  Serial.println("⚠️ WiFi Lost -> Reconnecting");

  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  delay(3000);

  } else {

    if (!ntpSynced) {

      Serial.println("🔄 Re-syncing NTP");

      if (timeClient.forceUpdate()) {

        ntpSynced = true;

        Serial.println("✅ NTP Re-synced");
      }
    }
  }

  static unsigned long lastNtpUpdate = 0;

  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastNtpUpdate > 3600000UL) {

    Serial.println("🔄 Refreshing NTP Time");

    timeClient.forceUpdate();

    lastNtpUpdate = millis();
  }

  String today = getDayOfWeek();
  String currentTime = getCurrentTimeHHMM();
  String overallPath =
  "rooms/" + String(ROOM_ID) + "/overall";

  String controlPath =
    "control/" + String(ROOM_ID);

  String manualPath =
    "manual/" + String(ROOM_ID);

  String todayPath =
    "rooms/" + String(ROOM_ID) + "/days/" + today;
  /* ---------- DEBUG PRINT ---------- */

  Serial.println("\n=================================================");
  Serial.println("📡 FIREBASE DEBUG INFORMATION");
  Serial.println("=================================================");

  Serial.println("🏫 ROOM ID         : " + String(ROOM_ID));

  Serial.println("📅 CURRENT DAY     : " + today);

  Serial.println("🕒 CURRENT TIME    : " + currentTime);

  Serial.println("📡 OVERALL PATH    : " + overallPath);

  Serial.println("📡 CONTROL PATH    : " + controlPath);

  Serial.println("📡 MANUAL PATH     : " + manualPath);

  Serial.println("📡 TODAY PATH      : " + todayPath);

  Serial.println("📶 WIFI RSSI       : " +
                  String(WiFi.RSSI()) + " dBm");

  Serial.println("🌐 WIFI STATUS     : " +
                  String(WiFi.status() == WL_CONNECTED ?
                  "CONNECTED" : "DISCONNECTED"));


  printWiFiStatus();

  /* ---------- PERIODIC CLOUD SYNC ---------- */

  if (millis() - lastCloudSync > CLOUD_SYNC_INTERVAL_MS) {

    if (isCloudAvailable()) {

      syncAllSchedulesToNVS();
      lastCloudSync = millis();
    }
  }

  /* ---------- FETCH ROOM STATE ---------- */

  String overall;
  String control;
  String manual;

  bool online = false;

  if (isCloudAvailable()) {

    unsigned long fetchStart = millis();

    if (fetchRoomStateFromCloud(
          ROOM_ID,
          overall,
          control,
          manual
        )) {
            saveRoomStateToNVS(
            overall,
            control,
            manual
          );
            Serial.println("\n========== CLOUD FETCHED DATA ==========");

            Serial.println("✅ ROOM OVERALL    : " + overall);

            Serial.println("✅ CONTROL MODE    : " + control);

            Serial.println("✅ MANUAL STATUS   : " + manual);

            Serial.println("========================================");


          online = true;

      unsigned long fetchEnd = millis();

      Serial.print("⚡ Firebase Fetch Time: ");
      Serial.print(fetchEnd - fetchStart);
      Serial.println(" ms");
    }
  }

  if (!online) {

    Serial.println("⚠️ Using OFFLINE MODE");

  if (loadRoomStateFromNVS(
        overall,
        control,
        manual
      )) {

      Serial.println("📦 STATE LOADED FROM NVS");

      Serial.println("Overall : " + overall);
      Serial.println("Control : " + control);
      Serial.println("Manual  : " + manual);

  } else {

      Serial.println("❌ NO ROOM STATE FOUND IN NVS");

      turnACOff();
      return;
  }
  }

  Serial.println("Overall : " + overall);
  Serial.println("Control : " + control);

  /* ---------- ROOM OFF ---------- */

  if (overall != "ON") {

    Serial.println("🛑 Room OFF -> Relay OFF");

    turnACOff();
    return;
  }

  /* ---------- MANUAL MODE ---------- */

  if (control == "MANUAL") {

    Serial.println("🎛 MANUAL MODE");

    if (manual == "ON") {
      turnACOn();
    } else {
      turnACOff();
    }

    return;
  }

  /* ---------- AUTO MODE ---------- */

  String scheduleJson;

  bool gotSchedule = false;

  if (online) {

    gotSchedule = fetchDayScheduleFromCloud(
                    ROOM_ID,
                    today,
                    scheduleJson
                  );

    if (gotSchedule) {
      
        Serial.println("\n📥 TODAY SCHEDULE JSON:");

        Serial.println(scheduleJson);

      saveDayScheduleToNVS(
        ROOM_ID,
        today,
        scheduleJson
      );
    }
  }

  if (!gotSchedule) {

    gotSchedule = loadDayScheduleFromNVS(
                    ROOM_ID,
                    today,
                    scheduleJson
                  );

    if (gotSchedule) {
      Serial.println("📦 Loaded Schedule From NVS");
    }
  }

  if (!gotSchedule) {

    Serial.println("❌ No Schedule Found");
    turnACOff();
    return;
  }

  applySchedule(
    scheduleJson,
    today,
    currentTime,
    online ? "CLOUD" : "NVS"
  );
}

/* =========================================================
   CLOUD STATUS
   ========================================================= */

bool isCloudAvailable() {

  return WiFi.status() == WL_CONNECTED &&
         Firebase.ready();
}

/* =========================================================
   FETCH ROOM STATE
   ========================================================= */

bool fetchRoomStateFromCloud(
  const String &roomId,
  String &overall,
  String &control,
  String &manual
) {

  if (!Firebase.RTDB.getString(
        &fbdo,
        ("rooms/" + roomId + "/overall").c_str()
      )) return false;

  overall = fbdo.stringData();

  if (!Firebase.RTDB.getString(
        &fbdo,
        ("control/" + roomId).c_str()
      )) return false;

  control = fbdo.stringData();

  if (!Firebase.RTDB.getString(
        &fbdo,
        ("manual/" + roomId).c_str()
      )) return false;

  manual = fbdo.stringData();

  return true;
}

/* =========================================================
   FETCH DAY SCHEDULE
   ========================================================= */

bool fetchDayScheduleFromCloud(
  const String &roomId,
  const String &day,
  String &jsonData
) {

  String path =
    "rooms/" + roomId + "/days/" + day;

  if (!Firebase.RTDB.getJSON(
        &fbdo,
        path.c_str()
      )) {

    return false;
  }

  jsonData = fbdo.payload();

  return true;
}

/* =========================================================
   SAVE TO NVS
   ========================================================= */

void saveDayScheduleToNVS(
  const String &roomId,
  const String &day,
  const String &jsonData
) {

  String ns =
    roomId == "Room_D047" ? "SCH047" : "SCH117";

  prefs.begin(ns.c_str(), false);

  prefs.putString(day.c_str(), jsonData);

  prefs.end();

  Serial.println("💾 Saved " + roomId + " " + day + " to NVS");
}

/* =========================================================
   LOAD FROM NVS
   ========================================================= */

bool loadDayScheduleFromNVS(
  const String &roomId,
  const String &day,
  String &jsonData
) {

  String ns =
    roomId == "Room_D047" ? "SCH047" : "SCH117";

  prefs.begin(ns.c_str(), true);

  jsonData = prefs.getString(day.c_str(), "");

  prefs.end();

  return jsonData.length() > 0;
}

/* =========================================================
   SYNC ALL SCHEDULES
   ========================================================= */

void syncAllSchedulesToNVS() {

  Serial.println("\n🔄 Syncing All Schedules To NVS");

  for (int r = 0; r < 2; r++) {

    String room = ROOMS[r];

    for (int d = 0; d < 7; d++) {

      String day = DAYS[d];
      String jsonData;

      unsigned long startTime = millis();

      bool ok = fetchDayScheduleFromCloud(
                  room,
                  day,
                  jsonData
                );

      unsigned long endTime = millis();

      Serial.print("📡 Fetch Speed ");
      Serial.print(room);
      Serial.print(" ");
      Serial.print(day);
      Serial.print(" : ");
      Serial.print(endTime - startTime);
      Serial.println(" ms");

      if (ok) {

        saveDayScheduleToNVS(
          room,
          day,
          jsonData
        );

      } else {

        Serial.println("❌ Fetch Failed");
      }
    }
  }

  Serial.println("✅ All Schedules Synced");
}

 
/* =========================================================
   APPLY SCHEDULE
   ========================================================= */

bool applySchedule(
  const String &jsonData,
  const String &today,
  const String &currentTime,
  const String &source
) {

  DynamicJsonDocument doc(4096);

  DeserializationError err =
    deserializeJson(doc, jsonData);

  if (err) {

    Serial.println("❌ JSON Parse Error");
    return false;
  }

  int currentMinutes =
    hhmmToMinutes(currentTime);

  bool found = false;

  /* =====================================================
     HANDLE JSON ARRAY FORMAT
     ===================================================== */

  JsonArray arr = doc.as<JsonArray>();

  int index = 0;

  for (JsonObject slot : arr) {

    String start  = slot["start"] | "";
    String end    = slot["end"] | "";
    String status = slot["status"] | "OFF";

    int startMin = hhmmToMinutes(start);
    int endMin   = hhmmToMinutes(end);

    Serial.println("\n--------------------------------");

    Serial.print("📚 SLOT INDEX : ");
    Serial.println(index);

    Serial.print("🕒 START TIME : ");
    Serial.println(start);

    Serial.print("🕒 END TIME   : ");
    Serial.println(end);

    Serial.print("📌 STATUS     : ");
    Serial.println(status);

    Serial.print("⏱ CURRENT MIN : ");
    Serial.println(currentMinutes);

    Serial.print("⏱ START MIN   : ");
    Serial.println(startMin);

    Serial.print("⏱ END MIN     : ");
    Serial.println(endMin);

    Serial.println("--------------------------------");

    if (currentMinutes >= startMin &&
        currentMinutes <= endMin) {

      found = true;

      Serial.println("✅ ACTIVE SLOT MATCHED");

      if (status == "ON") {

        Serial.println("💡 RELAY STATUS : ON");
        Serial.println("⚡ AC POWER     : ENABLED");

        turnACOn();

      } else {

        Serial.println("💤 RELAY STATUS : OFF");
        Serial.println("❄️ AC POWER     : DISABLED");

        turnACOff();
      }

      break;
    }

    index++;
  }

  if (!found) {

    Serial.println("⏰ NO ACTIVE SLOT FOUND");
    turnACOff();
  }

  return found;
}

/* =========================================================
   WIFI STATUS
   ========================================================= */

void printWiFiStatus() {

  Serial.print("📶 RSSI Signal : ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  if (WiFi.RSSI() > -60) {
    Serial.println("🚀 Internet Speed: GOOD");
  }
  else if (WiFi.RSSI() > -75) {
    Serial.println("⚠️ Internet Speed: MEDIUM");
  }
  else {
    Serial.println("🐢 Internet Speed: WEAK");
  }
}

/* =========================================================
   PRINT NVS DATA
   ========================================================= */

void printNVSStoredData() {

  Serial.println("\n📦 CHECKING NVS STORAGE");

  for (int r = 0; r < 2; r++) {

    String room = ROOMS[r];

    String ns =
      room == "Room_D047" ? "SCH047" : "SCH117";

    prefs.begin(ns.c_str(), true);

    for (int d = 0; d < 7; d++) {

      String day = DAYS[d];

      String data =
        prefs.getString(day.c_str(), "");

      if (data.length() > 0) {

        Serial.println("✅ STORED -> " + room + " " + day);

      } else {

        Serial.println("❌ EMPTY -> " + room + " " + day);
      }
    }

    prefs.end();
  }
}

/* =========================================================
   UTILITIES
   ========================================================= */

String getDayOfWeek() {

  int dayIndex =
    (timeClient.getDay() + 6) % 7;

  String days[7] = {
    "Mon","Tue","Wed","Thu","Fri","Sat","Sun"
  };

  return days[dayIndex];
}

String getCurrentTimeHHMM() {

  int h = timeClient.getHours();
  int m = timeClient.getMinutes();

  char buf[6];

  sprintf(buf, "%02d:%02d", h, m);

  return String(buf);
}

int hhmmToMinutes(const String &t) {

  int h = t.substring(0,2).toInt();
  int m = t.substring(3,5).toInt();

  return h * 60 + m;
}


/* =========================================================
   ADD INSIDE turnACOn()
   ========================================================= */

void turnACOn() {

  digitalWrite(RELAY_PIN, RELAY_ON_STATE);

  Serial.println("================================");
  Serial.println("💡 RELAY TURNED ON");
  Serial.println("⚡ AC POWER ENABLED");
  Serial.println("================================");
}

/* =========================================================
   ADD INSIDE turnACOff()
   ========================================================= */

void turnACOff() {

  digitalWrite(RELAY_PIN, RELAY_OFF_STATE);

  Serial.println("================================");
  Serial.println("💤 RELAY TURNED OFF");
  Serial.println("❄️ AC POWER DISABLED");
  Serial.println("================================");
}