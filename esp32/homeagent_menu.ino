#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* apiUrl   = "http://172.19.206.92:8000/api/status?api_key=YOUR_API_KEY";

void mapTouch(uint16_t rawX, uint16_t rawY, uint16_t &screenX, uint16_t &screenY) {
  screenX = map(rawY, 301, 1, 0, 240);
  screenY = map(rawX, 234, 3, 0, 320);
  screenX = constrain(screenX, 0, 240);
  screenY = constrain(screenY, 0, 320);
}

void drawBar(int x, int y, int w, int h, float percent, uint16_t color) {
  tft.drawRect(x, y, w, h, TFT_DARKGREY);
  tft.fillRect(x+1, y+1, (w-2) * percent / 100, h-2, color);
}

void showDashboard() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(apiUrl);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    deserializeJson(doc, payload);

    float cpu  = doc["cpu_percent"];
    float ram  = doc["ram_percent"];
    float disk = doc["disk_percent"];
    float temp = doc["cpu_temp"];

    tft.fillScreen(TFT_BLACK);

    // Başlık
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Dashboard", 55, 8);

    // CPU
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("CPU", 10, 50);
    tft.setTextSize(2);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.drawString(String(cpu, 1) + "%", 10, 65);
    drawBar(10, 90, 220, 12, cpu, TFT_BLUE);

    // RAM
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("RAM", 10, 115);
    tft.setTextSize(2);
    tft.setTextColor(TFT_PURPLE, TFT_BLACK);
    tft.drawString(String(ram, 1) + "%", 10, 130);
    drawBar(10, 155, 220, 12, ram, TFT_PURPLE);

    // Disk
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("Disk", 10, 180);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(String(disk, 1) + "%", 10, 195);
    drawBar(10, 220, 220, 12, disk, TFT_GREEN);

    // Sıcaklık
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("Temp", 10, 245);
    tft.setTextSize(2);
    uint16_t tempColor = temp > 70 ? TFT_RED : temp > 55 ? TFT_ORANGE : TFT_GREEN;
    tft.setTextColor(tempColor, TFT_BLACK);
    tft.drawString(String(temp, 1) + "C", 10, 260);

    // Geri butonu
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Tap to go back", 75, 305);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Connecting...", 40, 140);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  tft.fillScreen(TFT_BLACK);
  tft.drawString("Connected!", 60, 140);
  delay(800);

  showDashboard();
}

void loop() {
  uint16_t rawX, rawY, x, y;
  bool pressed = tft.getTouch(&rawX, &rawY);

  if (pressed) {
    mapTouch(rawX, rawY, x, y);
    while (tft.getTouch(&rawX, &rawY)) delay(10);
    // 3 saniyede bir yenile
  }
  
  delay(3000);
  showDashboard();
}