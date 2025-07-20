#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

// Wi-Fi credentials
const char* ssid = "YourWirelessSSID";
const char* password = "YourPassword";

// ADS-B JSON endpoint change as appropriate
const char* statsUrl = "http://adsbexchange.local/tar1090/data/stats.json";

// Display instance
TFT_eSPI tft = TFT_eSPI();

// High water tracking
const int historySize = 256;
int aircraftHistory[historySize];
int historyIndex = 0;

void setup() {
  Serial.begin(115200);

  // Backlight pin (adjust if needed)
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
configTime(0, 0, "pool.ntp.org");       // Sync with NTP
setenv("TZ", "EST5EDT", 1);             // Set timezone to US Eastern (with DST)
tzset();                                // Apply timezone settings
// Wait for time to sync
Serial.print("Waiting for NTP time sync");
while (time(nullptr) < 1640995200) {  // Wait until year is at least 2022
  delay(100);
  Serial.print(".");
}
Serial.println("Time synced");



  tft.setCursor(10, 30);
  tft.println("WiFi Connected!");
  delay(1000);
}

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

void loop() {
  HTTPClient http;
  http.begin(statsUrl);
  int httpCode = http.GET();

  tft.fillScreen(TFT_BLACK);
  drawBanner();

  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // Extract values
      int gainDB     = doc["gain_db"];
      int withPos    = doc["aircraft_with_pos"];
      int withoutPos = doc["aircraft_without_pos"];
      int adsb       = doc["total"]["position_count_by_type"]["adsb_icao"];
      int mlat       = doc["total"]["position_count_by_type"]["mlat"];

      // Update high water mark
      aircraftHistory[historyIndex] = withPos;
      historyIndex = (historyIndex + 1) % historySize;
      int highMark = 0;
      for (int i = 0; i < historySize; i++) {
        if (aircraftHistory[i] > highMark) highMark = aircraftHistory[i];
      }

      // Timestamp
      time_t now;
      time(&now);
      char nowStr[32];
      strftime(nowStr, sizeof(nowStr), "%H:%M:%S", localtime(&now));

      // Draw metrics
      drawMetricLine(35, "Gain (dB)", String(gainDB), TFT_GREEN);
      drawWithPosAndHigh(65, withPos, highMark);
      drawMetricLine(125, "Without Pos", String(withoutPos), TFT_CYAN);
      drawMetricLine(155, "ADSB Pos", String(adsb), TFT_MAGENTA);
      drawMetricLine(185, "MLAT Pos", String(mlat), TFT_MAGENTA);

      // Footer timestamp
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(1);
      tft.setCursor(10, 220);
      tft.print("Sample Taken: ");
      tft.print(nowStr);
    } else {
      tft.setCursor(10, 40);
      tft.setTextColor(TFT_RED);
      tft.setTextSize(2);
      tft.println("JSON Parse Error");
    }
  } else {
    tft.setCursor(10, 40);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.println("HTTP Fail");
  }

  http.end();
  delay(60000);  // Refresh every minute
}
