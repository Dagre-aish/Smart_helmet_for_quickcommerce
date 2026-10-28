/*
  Smart Helmet + Firebase Realtime Database
  Board: ESP32 DevKit (Arduino IDE)
  Sensors/pins preserved from your original sketch:
    IR_PIN      19  (LOW means helmet worn)
    BUZZER_PIN  25
    ALCOHOL_PIN 34  (ADC input)
  Rider ID: RIDER_001

  Libraries to install in Arduino IDE:
    - ArduinoJson by Benoit Blanchon (v6 or v7)

  Firebase setup:
    - Enable Authentication > Sign-in method > Anonymous
    - Create Realtime Database
    - Put your Web API key and database URL below
    - Use authenticated-only rules during prototype testing (see README)

  NOTE: setInsecure() is used for a quick prototype. It skips HTTPS certificate
  verification and is NOT suitable for production. Use certificate validation
  before real-world deployment.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---------- EDIT THESE VALUES ----------
const char* WIFI_SSID = "YOUR_PHONE_HOTSPOT_NAME";
const char* WIFI_PASSWORD = "YOUR_PHONE_HOTSPOT_PASSWORD";

const char* FIREBASE_API_KEY = "AIzaSyCP5DJHNgHqYlhZ6WG2nqEjkSZR8i5Vd2c";
const char* FIREBASE_DATABASE_URL = "https://helmet-project-5b2b6-default-rtdb.firebaseio.com";
// If your Firebase console shows a different database URL, use it exactly.
// Do not add a trailing slash.
// ---------------------------------------

#define IR_PIN       19
#define BUZZER_PIN   25
#define ALCOHOL_PIN  34

#define ALCOHOL_THRESHOLD 1000

const unsigned long HELMET_WARNING_DELAY = 20UL * 1000UL; // 20 seconds
const unsigned long BAN_TIME = 2UL * 60UL * 1000UL;       // 2 minutes after warning
const unsigned long UPLOAD_INTERVAL = 1000UL;
const unsigned long TOKEN_REFRESH_MARGIN = 5UL * 60UL * 1000UL;

const char* RIDER_ID = "RIDER_001";

bool helmetWorn = false;
bool alcoholDetected = false;
bool buzzerState = false;
bool banned = false;
int alcoholValue = 0;

unsigned long helmetRemovedAt = 0;
unsigned long banStartedAt = 0;
unsigned long lastUploadAt = 0;
unsigned long tokenObtainedAt = 0;
unsigned long tokenExpiresInMs = 0;

String idToken;
String refreshToken;
bool firebaseReady = false;

String databaseBaseUrl() {
  String url = String(FIREBASE_DATABASE_URL);
  while (url.endsWith("/")) url.remove(url.length() - 1);
  return url;
}

bool parseAuthResponse(const String& response, bool isRefresh) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.printf("Auth JSON parse error: %s\n", err.c_str());
    return false;
  }

  if (doc["error"]["message"].is<const char*>()) {
    Serial.print("Firebase auth error: ");
    Serial.println((const char*)doc["error"]["message"]);
    return false;
  }

  const char* newIdToken = doc["idToken"] | "";
  if (strlen(newIdToken) == 0) {
    Serial.println("Firebase auth response did not contain idToken.");
    return false;
  }
  idToken = String(newIdToken);

  const char* newRefreshToken = doc["refreshToken"] | "";
  if (strlen(newRefreshToken) > 0) refreshToken = String(newRefreshToken);

  unsigned long expiresSeconds = 3600;
  if (doc["expiresIn"].is<const char*>()) {
    expiresSeconds = String((const char*)doc["expiresIn"]).toInt();
  } else if (doc["expiresIn"].is<unsigned long>()) {
    expiresSeconds = doc["expiresIn"].as<unsigned long>();
  }

  tokenObtainedAt = millis();
  tokenExpiresInMs = expiresSeconds * 1000UL;
  Serial.println(isRefresh ? "Firebase token refreshed." : "Firebase anonymous sign-in successful.");
  return true;
}

bool firebaseAnonymousSignIn() {
  if (String(FIREBASE_API_KEY).startsWith("YOUR_") ||
      String(FIREBASE_DATABASE_URL).indexOf("YOUR_PROJECT_ID") >= 0) {
    Serial.println("ERROR: Add Firebase API key and database URL in the sketch first.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Prototype only; see security note at top.
  HTTPClient https;
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=" +
               String(FIREBASE_API_KEY);
  if (!https.begin(client, url)) {
    Serial.println("Could not start Firebase sign-in request.");
    return false;
  }
  https.addHeader("Content-Type", "application/json");
  int code = https.POST("{\"returnSecureToken\":true}");
  String response = https.getString();
  https.end();

  if (code < 200 || code >= 300) {
    Serial.printf("Firebase sign-in HTTP error: %d\n", code);
    Serial.println(response);
    return false;
  }
  return parseAuthResponse(response, false);
}

bool firebaseRefreshToken() {
  if (refreshToken.length() == 0) return firebaseAnonymousSignIn();

  WiFiClientSecure client;
  client.setInsecure(); // Prototype only; see security note at top.
  HTTPClient https;
  String url = "https://securetoken.googleapis.com/v1/token?key=" + String(FIREBASE_API_KEY);
  if (!https.begin(client, url)) return false;

  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "grant_type=refresh_token&refresh_token=" + refreshToken;
  int code = https.POST(body);
  String response = https.getString();
  https.end();

  if (code < 200 || code >= 300) {
    Serial.printf("Firebase token refresh HTTP error: %d\n", code);
    Serial.println(response);
    return false;
  }
  return parseAuthResponse(response, true);
}

bool ensureFirebaseToken() {
  if (idToken.length() == 0) return firebaseAnonymousSignIn();

  unsigned long elapsed = millis() - tokenObtainedAt;
  if (tokenExpiresInMs > TOKEN_REFRESH_MARGIN &&
      elapsed < tokenExpiresInMs - TOKEN_REFRESH_MARGIN) {
    return true;
  }
  return firebaseRefreshToken();
}

bool uploadRiderData(unsigned long helmetMissingSeconds, unsigned long banRemainingSeconds) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!ensureFirebaseToken()) return false;

  DynamicJsonDocument doc(1024);
  doc["riderId"] = RIDER_ID;
  doc["helmet"] = helmetWorn;
  doc["alcohol"] = alcoholDetected;
  doc["alcoholValue"] = alcoholValue;
  doc["buzzer"] = buzzerState;
  doc["banned"] = banned;
  doc["helmetMissingSeconds"] = helmetMissingSeconds;
  doc["banRemainingSeconds"] = banRemainingSeconds;
  doc["updatedAt"][".sv"] = "timestamp";

  String payload;
  serializeJson(doc, payload);

  WiFiClientSecure client;
  client.setInsecure(); // Prototype only; see security note at top.
  HTTPClient https;

  String url = databaseBaseUrl() + "/riders/" + String(RIDER_ID) +
               ".json?auth=" + idToken;
  if (!https.begin(client, url)) {
    Serial.println("Could not start Firebase database request.");
    return false;
  }
  https.addHeader("Content-Type", "application/json");
  int code = https.PUT(payload);
  String response = https.getString();
  https.end();

  if (code < 200 || code >= 300) {
    Serial.printf("Firebase upload HTTP error: %d\n", code);
    Serial.println(response);
    return false;
  }
  Serial.println("Firebase upload OK");
  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to phone hotspot");
  unsigned long started = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000UL) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi connection failed. Will retry in loop.");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(IR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  analogReadResolution(12);
  delay(500);

  Serial.println("{\"type\":\"starting\"}");
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    firebaseReady = firebaseAnonymousSignIn();
  }
  Serial.println("{\"type\":\"ready\"}");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWiFiRetry = 0;
    if (millis() - lastWiFiRetry >= 5000UL) {
      lastWiFiRetry = millis();
      Serial.println("Wi-Fi disconnected; reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  // READ SENSORS (same active-low IR behavior as original sketch)
  int irState = digitalRead(IR_PIN);
  helmetWorn = (irState == LOW);
  alcoholValue = analogRead(ALCOHOL_PIN);
  alcoholDetected = (alcoholValue >= ALCOHOL_THRESHOLD);

  // HELMET WORN: preserve original behavior; wearing it again clears the ban.
  if (helmetWorn) {
    helmetRemovedAt = 0;
    banStartedAt = 0;
    banned = false;
    buzzerState = alcoholDetected;
  } else {
    if (helmetRemovedAt == 0) helmetRemovedAt = millis();

    unsigned long helmetMissingTime = millis() - helmetRemovedAt;
    if (helmetMissingTime >= HELMET_WARNING_DELAY) {
      buzzerState = true;
      if (banStartedAt == 0) banStartedAt = millis();

      unsigned long banElapsed = millis() - banStartedAt;
      if (banElapsed >= BAN_TIME) {
        banned = true;
        buzzerState = true;
      }
    } else {
      buzzerState = false;
    }
  }

  // Alcohol alert overrides the helmet timer, as in the original.
  if (alcoholDetected) buzzerState = true;
  digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);

  unsigned long helmetMissingSeconds = 0;
  if (helmetRemovedAt > 0) helmetMissingSeconds = (millis() - helmetRemovedAt) / 1000UL;

  unsigned long banRemainingSeconds = 0;
  if (banStartedAt > 0 && !banned) {
    unsigned long elapsed = millis() - banStartedAt;
    if (elapsed < BAN_TIME) banRemainingSeconds = (BAN_TIME - elapsed) / 1000UL;
  }

  // Keep serial JSON output for debugging, and upload live values about once per second.
  DynamicJsonDocument serialDoc(1024);
  serialDoc["riderId"] = RIDER_ID;
  serialDoc["helmet"] = helmetWorn;
  serialDoc["alcohol"] = alcoholDetected;
  serialDoc["alcoholValue"] = alcoholValue;
  serialDoc["buzzer"] = buzzerState;
  serialDoc["banned"] = banned;
  serialDoc["helmetMissingSeconds"] = helmetMissingSeconds;
  serialDoc["banRemainingSeconds"] = banRemainingSeconds;
  serializeJson(serialDoc, Serial);
  Serial.println();

  if (millis() - lastUploadAt >= UPLOAD_INTERVAL) {
    lastUploadAt = millis();
    if (WiFi.status() == WL_CONNECTED) {
      if (idToken.length() == 0) firebaseReady = firebaseAnonymousSignIn();
      if (firebaseReady || idToken.length() > 0) {
        uploadRiderData(helmetMissingSeconds, banRemainingSeconds);
      }
    }
  }

  delay(200);
}
