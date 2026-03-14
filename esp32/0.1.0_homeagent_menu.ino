#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>

TFT_eSPI tft = TFT_eSPI();

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* menuItems[] = { "Dashboard", "Files", "Docker", "Settings" };
const char* menuIcons[] = { "CPU/RAM/Disk", "File Explorer", "Containers", "Network" };
int menuCount = 4;
int selectedItem = 0;
bool inMenu = true;

void mapTouch(uint16_t rawX, uint16_t rawY, uint16_t &screenX, uint16_t &screenY) {
  screenX = map(rawY, 301, 1, 0, 240);
  screenY = map(rawX, 234, 3, 0, 320);
  screenX = constrain(screenX, 0, 240);
  screenY = constrain(screenY, 0, 320);
}

void drawMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("HomeAgent", 85, 10);

  for (int i = 0; i < menuCount; i++) {
    int y = 60 + i * 55;
    if (i == selectedItem) {
      tft.fillRoundRect(10, y - 8, 220, 46, 8, TFT_NAVY);
      tft.drawRoundRect(10, y - 8, 220, 46, 8, TFT_BLUE);
      tft.setTextColor(TFT_WHITE, TFT_NAVY);
    } else {
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    }
    tft.setTextSize(2);
    tft.drawString(menuItems[i], 30, y);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, i == selectedItem ? TFT_NAVY : TFT_BLACK);
    tft.drawString(menuIcons[i], 30, y + 20);
  }
}

void showPage(const char* title) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString(title, 20, 10);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Coming soon...", 40, 120);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Tap to go back", 80, 220);
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

  drawMenu();
  inMenu = true;
}

void loop() {
  uint16_t rawX, rawY, x, y;
  bool pressed = tft.getTouch(&rawX, &rawY);

  if (pressed) {
    mapTouch(rawX, rawY, x, y);
    delay(50);

    if (inMenu) {
      for (int i = 0; i < menuCount; i++) {
        int itemY = 60 + i * 55;
        if (y >= itemY - 8 && y <= itemY + 38) {
          selectedItem = i;
          drawMenu();
          delay(150);
          switch (i) {
            case 0: showPage("Dashboard"); break;
            case 1: showPage("Files"); break;
            case 2: showPage("Docker"); break;
            case 3: showPage("Settings"); break;
          }
          inMenu = false;
          break;
        }
      }
    } else {
      drawMenu();
      inMenu = true;
    }

    while (tft.getTouch(&rawX, &rawY)) delay(10);
  }
}