/*
Project: Weather Station using ESP32
Author: Siddhi Sondkar
Description: Monitors wind speed, rainfall, direction and sends Telegram alerts
Note: Replace WiFi and Telegram credentials before running
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>

/********** LCD **********/
LiquidCrystal_I2C lcd(0x27, 16, 2);

/********** SD **********/
#define SD_CS 10
bool sdOK = false;

/********** WiFi **********/
const char* ssid = "YOUR_WIFI_NAME";        // 🔴 Replace
const char* password = "YOUR_WIFI_PASSWORD"; // 🔴 Replace

/********** Telegram **********/
String botToken = "YOUR_BOT_TOKEN";   // 🔴 Replace
String chatID   = "YOUR_CHAT_ID";     // 🔴 Replace

/********** GPS **********/
const char* gpsAPI = "http://ip-api.com/json";
String gpsLocation = "unknown";

/********** Pins **********/
#define WIND_PIN 7
#define RAIN_PIN 8
#define WIND_DIR_PIN 4

/********** Timing **********/
#define SENSOR_INTERVAL     5000UL       
#define TELEGRAM_INTERVAL   300000UL    
#define SD_INTERVAL         1800000UL   

/********** Thresholds **********/
#define WIND_ALERT_LIMIT 12.0   
#define RAIN_ALERT_LIMIT 8.0    

volatile int windCount = 0;
volatile int rainCount = 0;

float windFactor = 2.4;
float rainBucket = 0.2794;
float totalRainfall = 0;

unsigned long lastSensorRead = 0;
unsigned long lastTelegram = 0;
unsigned long lastSDWrite = 0;

bool windAlertSent = false;
bool rainAlertSent = false;

/********** Interrupts **********/
void IRAM_ATTR windISR() { windCount++; }

void IRAM_ATTR rainISR() {
  static unsigned long lastPulse = 0;
  if (millis() - lastPulse > 250) {
    rainCount++;
    lastPulse = millis();
  }
}

/********** Direction **********/
String directionName(float angle) {
  if (angle >= 337.5 || angle < 22.5) return "N";
  if (angle < 67.5) return "NE";
  if (angle < 112.5) return "E";
  if (angle < 157.5) return "SE";
  if (angle < 202.5) return "S";
  if (angle < 247.5) return "SW";
  if (angle < 292.5) return "W";
  return "NW";
}

/********** Telegram **********/
void sendTelegram(String msg) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "https://api.telegram.org/bot" + botToken + "/sendMessage";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String body =
    "{\"chat_id\":\"" + chatID +
    "\",\"text\":\"" + msg + "\"}";

  http.POST(body);
  http.end();
}

/********** GPS **********/
void fetchGPS() {
  HTTPClient http;
  http.begin(gpsAPI);
  if (http.GET() == 200) {
    String p = http.getString();
    int a = p.indexOf("\"lat\":");
    int b = p.indexOf("\"lon\":");
    if (a > 0 && b > 0) {
      String lat = p.substring(a + 6, p.indexOf(",", a));
      String lon = p.substring(b + 6, p.indexOf(",", b));
      gpsLocation = lat + "," + lon;
    }
  }
  http.end();
}

/********** SD Log **********/
void logToSD(float ws, float rain, String dir) {
  if (!sdOK) return;

  File f = SD.open("/weather.csv", FILE_APPEND);
  if (!f) return;

  f.print(millis());
  f.print(",");
  f.print(ws);
  f.print(",");
  f.print(rain);
  f.print(",");
  f.println(dir);
  f.close();
}

/********** Weather Update **********/
void sendWeatherUpdate(float ws, float rain, String dir) {
  String msg =
    "🌦 WEATHER UPDATE\n"
    "Wind: " + String(ws,1) + " m/s\n"
    "Rain: " + String(rain,2) + " mm\n"
    "Direction: " + dir + "\n"
    "Location:\nhttps://www.google.com/maps/search/?api=1&query=" + gpsLocation;

  sendTelegram(msg);
}

/********** Alerts **********/
void checkAbnormality(float ws, float rainNow) {

  if (ws > WIND_ALERT_LIMIT && !windAlertSent) {
    sendTelegram("🚨 HIGH WIND ALERT\nSpeed: " + String(ws,1) + " m/s");
    windAlertSent = true;
  }

  if (rainNow > RAIN_ALERT_LIMIT && !rainAlertSent) {
    sendTelegram("🚨 HEAVY RAIN ALERT\nRain: " + String(rainNow,2) + " mm");
    rainAlertSent = true;
  }
}

/********** Setup **********/
void setup() {
  Serial.begin(115200);
  Serial.println("System Started");

  Wire.begin(15, 16);
  lcd.init();
  lcd.backlight();

  pinMode(WIND_PIN, INPUT_PULLUP);
  pinMode(RAIN_PIN, INPUT_PULLUP);
  pinMode(WIND_DIR_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(WIND_PIN), windISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainISR, FALLING);

  if (SD.begin(SD_CS)) {
    sdOK = true;
    File f = SD.open("/weather.csv", FILE_WRITE);
    if (f) {
      f.println("Time,WindSpeed,Rainfall,Direction");
      f.close();
    }
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  fetchGPS();
  sendTelegram("✅ Weather station online");

  lcd.print("Weather Station");
  lcd.setCursor(0,1);
  lcd.print("Online");

  lastTelegram = millis();
  lastSDWrite = millis();
}

/********** Loop **********/
void loop() {

  if (millis() - lastSensorRead < SENSOR_INTERVAL) return;
  lastSensorRead = millis();

  int w = windCount; windCount = 0;
  int r = rainCount; rainCount = 0;

  float windSpeed = w * windFactor;
  float rainNow = r * rainBucket;
  totalRainfall += rainNow;

  float angle = map(analogRead(WIND_DIR_PIN), 0, 4095, 0, 360);
  String dir = directionName(angle);

  Serial.print("Wind: "); Serial.print(windSpeed);
  Serial.print(" | Rain: "); Serial.print(totalRainfall);
  Serial.print(" | Dir: "); Serial.println(dir);

  lcd.setCursor(0,0);
  lcd.print("WD:" + dir + " WS:" + String(windSpeed,1));
  lcd.setCursor(0,1);
  lcd.print("Rain:" + String(totalRainfall,2));

  checkAbnormality(windSpeed, rainNow);

  if (millis() - lastTelegram > TELEGRAM_INTERVAL) {
    sendWeatherUpdate(windSpeed, totalRainfall, dir);
    lastTelegram = millis();

    windAlertSent = false;
    rainAlertSent = false;
  }

  if (millis() - lastSDWrite > SD_INTERVAL) {
    logToSD(windSpeed, totalRainfall, dir);
    lastSDWrite = millis();
  }
}
