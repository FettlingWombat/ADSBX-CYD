#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

// Wi-Fi Credentials
const char* ssid = "YOURSSID";
const char* password = "YOURPASSWORD";

// MQTT Broker
const char* mqtt_server = "YOUR MQTT SERVER IP";
WiFiClient espClient;
PubSubClient mqttClient(espClient);
String mqttPayload =""; //"Waiting for MQTT data...";

// ADS-B Stats Endpoint
const char* statsUrl = "http://adsbexchange.local/tar1090/data/stats.json";

// Display
TFT_eSPI tft = TFT_eSPI();
bool displayMode = false;         // false = ADS-B, true = MQTT
bool screenDrawn = false;
unsigned long lastSwap = 0;
unsigned long swapInterval = 15000;

// Cached stats
unsigned long lastHttpFetch = 0;
unsigned long httpInterval = 60000;
StaticJsonDocument<2048> cachedStats;
bool statsOK = false;

// Aircraft history
const int historySize = 256;
int aircraftHistory[historySize];
int historyIndex = 0;

// MQTT Handling
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  mqttPayload = "";  // Clear any previous data

  if (length == 0) {
    Serial.println(mqttPayload);
    Serial.println("MQTT message received, but payload is empty.");
    return;
  }

  // Safely copy payload bytes to mqttPayload string
  for (unsigned int i = 0; i < length; i++) {
    mqttPayload += (char)payload[i];
  }

  screenDrawn = false;  // Trigger screen refresh
}

void setupMQTT() {
  Serial.println("setupMQTT");
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);
}

void reconnectMQTT() {
  Serial.println("reconnectMQTT");
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect("PlaneFenceClient")) {
      mqttClient.subscribe("planefence/planefence");
      Serial.println("Subscribed successfully!");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      delay(2000);
    }
  }
}


// UI Draw Helpers
void drawBanner() {
  tft.fillRect(0, 0, 320, 25, TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("ADS-B Dashboard");
}

void drawMetricLine(int y, const char* label, const String& value, uint16_t bgColor) {
  tft.fillRect(10, y, 300, 25, bgColor);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(15, y + 5);
  tft.print(label);
  tft.setCursor(200, y + 5);
  tft.print(value);
}

void drawWithPosAndHigh(int y, int withPos, int highMark) {
  tft.fillRect(10, y, 300, 25, TFT_CYAN);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(15, y + 5);
  tft.printf("With Positions: %d", withPos);

  tft.fillRect(10, y + 30, 300, 25, TFT_SKYBLUE);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(15, y + 35);
  tft.printf("Max Aircraft: %d", highMark);
}

void drawADSBScreen() {
  tft.fillScreen(TFT_BLACK);
  drawBanner();

  if (statsOK) {
    int gainDB     = cachedStats["gain_db"];
    int withPos    = cachedStats["aircraft_with_pos"];
    int withoutPos = cachedStats["aircraft_without_pos"];
    int adsb       = cachedStats["total"]["position_count_by_type"]["adsb_icao"];
    int mlat       = cachedStats["total"]["position_count_by_type"]["mlat"];

    aircraftHistory[historyIndex] = withPos;
    historyIndex = (historyIndex + 1) % historySize;
    int highMark = 0;
    for (int i = 0; i < historySize; i++) {
      if (aircraftHistory[i] > highMark) highMark = aircraftHistory[i];
    }

    time_t nowTime;
    time(&nowTime);
    char nowStr[32];
    strftime(nowStr, sizeof(nowStr), "%H:%M:%S", localtime(&nowTime));

    drawMetricLine(35, "Gain (dB)", String(gainDB), TFT_GREEN);
    drawWithPosAndHigh(65, withPos, highMark);
    drawMetricLine(125, "Without Pos", String(withoutPos), TFT_CYAN);
    drawMetricLine(155, "ADSB Pos", String(adsb), TFT_MAGENTA);
    drawMetricLine(185, "MLAT Pos", String(mlat), TFT_MAGENTA);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 220);
    tft.print("Sample Taken: ");
    tft.print(nowStr);
  } else {
    tft.setCursor(10, 40);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.println("No ADS-B data yet.");
  }
}

void drawMQTTScreen() {
  Serial.println("drawMQTTScreen");
  tft.fillScreen(TFT_BLACK);
  drawBanner();
  reconnectMQTT();

  Serial.println("---- MQTT PAYLOAD DEBUG ----");
  Serial.print("Length: ");
  Serial.println(mqttPayload.length());
  Serial.print("Raw Payload: >>>");
  Serial.print(mqttPayload);
  Serial.println("<<<");

  for (size_t i = 0; i < mqttPayload.length(); i++) {
    Serial.print((int)mqttPayload[i]);
    Serial.print(" ");  
  }
  Serial.println();
  Serial.println("---- END DEBUG ----");

  StaticJsonDocument<2048> doc;
  DeserializationError Error = deserializeJson(doc, mqttPayload);

  if (Error) {
    Serial.print("JSON error: ");
    Serial.println(Error.c_str());
    Serial.println(mqttPayload); // for debugging
    return; 
  }

  if (!Error) {
    const char* operatorName = doc["operator"] | "N/A";
    const char* distance     = doc["min_dist"] | "N/A";
    const char* thumbnail    = doc["thumbnail"] | "N/A";
    const char* lastSeen     = doc["last_seen"] | "N/A";
    const char* flight       = doc["flight"] | "N/A";
    const char* timezone     = doc["timezone"] | "N/A";
    const char* planespotters_link          = doc["planespotters_link"] | "N/A";
    const char* altitude     = doc["min_alt"] | "N/A";
    const char* origin       = doc["origin"] | "N/A";
    const char* destination  = doc["destination"] | "N/A";
    const char* icao         = doc["icao"] | "N/A";
    const char* first_seen   = doc["first_seen"] | "N/A";
    const char* link         = doc["link"] | "N/A";

    drawMetricLine(35,  "Flight",     String(flight),       TFT_SKYBLUE);
    drawMetricLine(65,  "Distance",   String(distance),     TFT_GREEN);
    drawMetricLine(95, "Altitude",   String(altitude),     TFT_MAGENTA);
    drawMetricLine(125, "Origin",     String(origin),       TFT_BLUE);
    drawMetricLine(155, "Dest.",     String(destination),   TFT_BLUE);   

    time_t nowTime;
    time(&nowTime);
    char nowStr[32];
    strftime(nowStr, sizeof(nowStr), "%H:%M:%S", localtime(&nowTime));

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 220);
    tft.print("MQTT Timestamp: ");
    tft.print(nowStr);
  } else {
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(10, 40);
    tft.println("MQTT Parse Error");

    tft.setTextSize(1);
    tft.setCursor(10, 80);
    tft.println(mqttPayload);
  }
}

// Setup
void setup() {
  Serial.begin(115200);
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Booting...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", "EST5EDT", 1);
  tzset();
  Serial.print("Waiting for NTP time sync");
  while (time(nullptr) < 1640995200) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("Time synced");
  tft.setCursor(10, 30);
  tft.println("WiFi Connected!");
  delay(1000);

  setupMQTT();
  reconnectMQTT();  // <-- Force MQTT connection on startup
}

// Main Loop
void loop() {
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  unsigned long now = millis();

  if (now - lastHttpFetch > httpInterval) {
    lastHttpFetch = now;
    HTTPClient http;
    http.begin(statsUrl);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      DeserializationError error = deserializeJson(cachedStats, payload);
      statsOK = !error;
    } else {
      statsOK = false;
    }
    http.end();
  }

  if (now - lastSwap > swapInterval) {
    displayMode = !displayMode;
    lastSwap = now;
    screenDrawn = false;
  }

  if (!screenDrawn) {
    if (displayMode) {
      drawMQTTScreen();
    } else {
      drawADSBScreen();
    }
    screenDrawn = true;
  }

  delay(100);
}