#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();

struct Btn {
  const char* label;
  uint16_t color;
};

// WiFi & API

// WiFi & API
const char* ssid       = "YOUR_WIFI_SSID";
const char* password   = "YOUR_WIFI_PASSWORD";
const char* apiBase    = "http://172.19.206.92:8000";
const char* apiKey     = "YOUR_API_KEY";

// Ekran boyutları
#define SCREEN_W 240
#define SCREEN_H 320
#define BAR_H    40
#define SCROLL_W 12
#define CONTENT_W (SCREEN_W - SCROLL_W)
#define CONTENT_H (SCREEN_H - BAR_H)

// Renkler
#define COL_BG      TFT_BLACK
#define COL_TOPBAR  0x1A3A
#define COL_BTNBAR  0x1A3A
#define COL_ACCENT  TFT_CYAN
#define COL_BTN     0x2A5A
#define COL_BTN_HL  0x4A9A
#define COL_SCROLL  0x2A3A
#define COL_THUMB   0x4A6A

// Sayfa sabitleri
#define PAGE_MENU       0
#define PAGE_DASHBOARD  1
#define PAGE_FILES      2
#define PAGE_SETTINGS   3

// Dosya işlem menüsü
#define FMENU_COPY    0
#define FMENU_CUT     1
#define FMENU_PASTE   2
#define FMENU_DELETE  3
#define FMENU_RENAME  4
#define FMENU_MKDIR   5
#define FMENU_TRASH   6
#define FMENU_COUNT   7

const char* fmenuLabels[] = {
  "Copy", "Cut", "Paste", "Delete", "Rename", "New Folder", "Trash"
};

// Global state
int currentPage = PAGE_MENU;
int scrollOffset = 0;
int totalItems = 0;

// Files state
struct FileItem {
  char name[64];
  bool isDir;
  long size;
};

FileItem fileItems[64];
int fileCount = 0;
char currentMount[32] = "";
char currentPath[256] = "";
int selectedFile = -1;
bool showFileMenu = false;

// Clipboard
char clipMount[32] = "";
char clipPath[256] = "";
bool clipCut = false;
bool hasClip = false;

// Touch kalibrasyon
void mapTouch(uint16_t rawX, uint16_t rawY, uint16_t &sx, uint16_t &sy) {
  sx = map(rawY, 301, 1, 0, SCREEN_W);
  sy = map(rawX, 234, 3, 0, SCREEN_H);
  sx = constrain(sx, 0, SCREEN_W);
  sy = constrain(sy, 0, SCREEN_H);
}

// HTTP GET
String httpGet(String url) {
  if (WiFi.status() != WL_CONNECTED) return "";
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  String result = "";
  if (code == 200) result = http.getString();
  http.end();
  return result;
}

// HTTP POST JSON
String httpPost(String url, String body) {
  if (WiFi.status() != WL_CONNECTED) return "";
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  String result = "";
  if (code == 200 || code == 201) result = http.getString();
  http.end();
  return result;
}

// Bar grafiği
void drawBar(int x, int y, int w, int h, float pct, uint16_t color) {
  tft.drawRect(x, y, w, h, TFT_DARKGREY);
  int filled = (w - 2) * constrain(pct, 0, 100) / 100;
  tft.fillRect(x+1, y+1, filled, h-2, color);
}

// Kaydırma çubuğu
void drawScrollBar(int offset, int total, int visible) {
  tft.fillRect(CONTENT_W, 0, SCROLL_W, CONTENT_H, COL_SCROLL);
  if (total <= visible) return;
  int thumbH = max(20, CONTENT_H * visible / total);
  int thumbY = (CONTENT_H - thumbH) * offset / (total - visible);
  tft.fillRoundRect(CONTENT_W + 2, thumbY + 2, SCROLL_W - 4, thumbH - 4, 3, COL_THUMB);
}

// Buton barı

void drawBtnBar(Btn* btns, int count) {
  tft.fillRect(0, CONTENT_H, SCREEN_W, BAR_H, COL_BTNBAR);
  int bw = SCREEN_W / count;
  for (int i = 0; i < count; i++) {
    int x = i * bw;
    tft.drawRect(x, CONTENT_H, bw, BAR_H, 0x3A5A);
    tft.setTextColor(btns[i].color, COL_BTNBAR);
    tft.setTextSize(1);
    int tw = strlen(btns[i].label) * 6;
    tft.drawString(btns[i].label, x + (bw - tw) / 2, CONTENT_H + 14);
  }
}

int getTappedBtn(uint16_t y, int count) {
  if (y < CONTENT_H || y > SCREEN_H) return -1;
  int bw = SCREEN_W / count;
  return -1; // x ile hesaplanacak
}

// ─── MENU ───────────────────────────────────────────────
void drawMenu() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(2);
  tft.drawString("HomeAgent", 55, 10);

  const char* items[] = { "Dashboard", "Files", "Settings" };
  const char* subs[]  = { "CPU/RAM/Disk/Temp", "File Explorer", "System Info" };
  int count = 3;

  for (int i = 0; i < count; i++) {
    int y = 55 + i * 70;
    tft.fillRoundRect(8, y, SCREEN_W - 16, 58, 8, COL_BTN);
    tft.drawRoundRect(8, y, SCREEN_W - 16, 58, 8, COL_ACCENT);
    tft.setTextColor(TFT_WHITE, COL_BTN);
    tft.setTextSize(2);
    tft.drawString(items[i], 20, y + 10);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, COL_BTN);
    tft.drawString(subs[i], 20, y + 36);
  }
}

void handleMenuTouch(uint16_t x, uint16_t y) {
  int items[] = { 55, 125, 195 };
  for (int i = 0; i < 3; i++) {
    if (y >= items[i] && y <= items[i] + 58) {
      switch (i) {
        case 0: currentPage = PAGE_DASHBOARD; showDashboard(); break;
        case 1: currentPage = PAGE_FILES; loadFileDevices(); break;
        case 2: currentPage = PAGE_SETTINGS; showSettings(); break;
      }
      return;
    }
  }
}

// ─── DASHBOARD ──────────────────────────────────────────
void showDashboard() {
  String url = String(apiBase) + "/api/status?api_key=" + apiKey;
  String resp = httpGet(url);
  if (resp == "") {
    tft.fillScreen(COL_BG);
    tft.setTextColor(TFT_RED, COL_BG);
    tft.drawString("Connection error", 20, 140);
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, resp);
  float cpu  = doc["cpu_percent"];
  float ram  = doc["ram_percent"];
  float disk = doc["disk_percent"];
  float temp = doc["cpu_temp"];

  tft.fillRect(0, 0, SCREEN_W, CONTENT_H, COL_BG);

  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(2);
  tft.drawString("Dashboard", 55, 8);

  // CPU
  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY, COL_BG);
  tft.drawString("CPU Usage", 10, 42);
  tft.setTextSize(2); tft.setTextColor(TFT_BLUE, COL_BG);
  tft.drawString(String(cpu, 1) + "%", 10, 56);
  drawBar(10, 78, CONTENT_W - 20, 10, cpu, TFT_BLUE);

  // RAM
  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY, COL_BG);
  tft.drawString("RAM Usage", 10, 102);
  tft.setTextSize(2); tft.setTextColor(TFT_PURPLE, COL_BG);
  tft.drawString(String(ram, 1) + "%", 10, 116);
  drawBar(10, 138, CONTENT_W - 20, 10, ram, TFT_PURPLE);

  // Disk
  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY, COL_BG);
  tft.drawString("Disk Usage", 10, 162);
  tft.setTextSize(2); tft.setTextColor(TFT_GREEN, COL_BG);
  tft.drawString(String(disk, 1) + "%", 10, 176);
  drawBar(10, 198, CONTENT_W - 20, 10, disk, TFT_GREEN);

  // Temp
  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY, COL_BG);
  tft.drawString("Temperature", 10, 222);
  tft.setTextSize(2);
  uint16_t tc = temp > 70 ? TFT_RED : temp > 55 ? TFT_ORANGE : TFT_GREEN;
  tft.setTextColor(tc, COL_BG);
  tft.drawString(String(temp, 1) + " C", 10, 236);

  // Buton barı
  Btn btns[] = { {"< Back", TFT_LIGHTGREY}, {"Home", COL_ACCENT} };
  drawBtnBar(btns, 2);
}

// ─── FILES ──────────────────────────────────────────────
void loadFileDevices() {
  String url = String(apiBase) + "/api/files/devices";
  String resp = httpGet(url);
  if (resp == "") return;

  JsonDocument doc;
  deserializeJson(doc, resp);
  JsonArray arr = doc.as<JsonArray>();

  tft.fillRect(0, 0, SCREEN_W, CONTENT_H, COL_BG);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(2);
  tft.drawString("Devices", 75, 8);

  fileCount = 0;
  scrollOffset = 0;
  int y = 40;

  for (JsonObject d : arr) {
    const char* mount = d["mount"];
    const char* used  = d["used"];
    const char* size  = d["size"];
    const char* pct   = d["percent"];

    if (strcmp(mount, "tmpfs") == 0) continue;

    tft.fillRoundRect(8, y, CONTENT_W - 16, 48, 6, COL_BTN);
    tft.setTextColor(TFT_WHITE, COL_BTN);
    tft.setTextSize(1);
    tft.drawString(mount, 16, y + 8);
    tft.setTextColor(TFT_DARKGREY, COL_BTN);
    tft.drawString(String(used) + " / " + String(size) + " (" + String(pct) + ")", 16, y + 24);

    strlcpy(fileItems[fileCount].name, mount, 64);
    fileItems[fileCount].isDir = true;
    fileItems[fileCount].size = 0;
    fileCount++;

    y += 56;
    if (fileCount >= 64) break;
  }

  Btn btns[] = { {"< Back", TFT_LIGHTGREY}, {"Home", COL_ACCENT}, {"Action", TFT_YELLOW}, {"Trash", TFT_RED} };
  drawBtnBar(btns, 4);

  currentPage = PAGE_FILES;
  strcpy(currentMount, "");
  strcpy(currentPath, "");
}

void loadFileDir() {
  String url = String(apiBase) + "/api/files/list?mount=" + 
               urlEncode(currentMount) + "&path=" + urlEncode(currentPath);
  String resp = httpGet(url);
  if (resp == "") return;

  JsonDocument doc;
  deserializeJson(doc, resp);
  JsonArray arr = doc["items"].as<JsonArray>();

  fileCount = 0;
  int idx = 0;
  for (JsonObject item : arr) {
    if (idx >= 64) break;
    strlcpy(fileItems[idx].name, item["name"] | "", 64);
    fileItems[idx].isDir = strcmp(item["type"] | "", "dir") == 0;
    fileItems[idx].size  = item["size"] | 0;
    idx++;
    fileCount++;
  }

  scrollOffset = 0;
  selectedFile = -1;
  drawFileList();
}

void drawFileList() {
  tft.fillRect(0, 0, CONTENT_W, CONTENT_H, COL_BG);

  // Başlık
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(1);
  String title = String(currentMount) + "/" + String(currentPath);
  if (title.length() > 26) title = "..." + title.substring(title.length() - 23);
  tft.drawString(title, 4, 4);

  int visibleCount = (CONTENT_H - 20) / 28;
  int y = 18;

  for (int i = scrollOffset; i < fileCount && i < scrollOffset + visibleCount; i++) {
    bool sel = (i == selectedFile);
    uint16_t bg = sel ? COL_BTN_HL : COL_BG;
    tft.fillRect(0, y, CONTENT_W, 26, bg);
    tft.drawLine(0, y + 26, CONTENT_W, y + 26, COL_SCROLL);

    tft.setTextColor(fileItems[i].isDir ? TFT_CYAN : TFT_WHITE, bg);
    tft.setTextSize(1);
    String icon = fileItems[i].isDir ? "[D] " : "[F] ";
    String name = icon + String(fileItems[i].name);
    if (name.length() > 28) name = name.substring(0, 25) + "...";
    tft.drawString(name, 6, y + 8);

    y += 28;
  }

  drawScrollBar(scrollOffset, fileCount, visibleCount);

  Btn btns[] = { {"< Back", TFT_LIGHTGREY}, {"Home", COL_ACCENT}, {"Action", TFT_YELLOW}, {"Trash", TFT_RED} };
  drawBtnBar(btns, 4);
}

void drawFileActionMenu() {
  int menuW = 140;
  int menuH = FMENU_COUNT * 28 + 8;
  int mx = (SCREEN_W - menuW) / 2;
  int my = (CONTENT_H - menuH) / 2;

  tft.fillRoundRect(mx, my, menuW, menuH, 8, COL_BTN);
  tft.drawRoundRect(mx, my, menuW, menuH, 8, COL_ACCENT);

  for (int i = 0; i < FMENU_COUNT; i++) {
    int iy = my + 4 + i * 28;
    uint16_t col = (i == FMENU_DELETE || i == FMENU_TRASH) ? TFT_RED : TFT_WHITE;
    if (i == FMENU_PASTE && !hasClip) col = TFT_DARKGREY;
    tft.setTextColor(col, COL_BTN);
    tft.setTextSize(1);
    tft.drawString(fmenuLabels[i], mx + 10, iy + 8);
  }
}

String urlEncode(const char* str) {
  String encoded = "";
  for (int i = 0; str[i]; i++) {
    char c = str[i];
    if (isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '/') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += String(c, HEX);
    }
  }
  return encoded;
}

String getFullPath(const char* name) {
  String p = String(currentPath);
  if (p.length() > 0 && p[p.length()-1] != '/') p += "/";
  p += name;
  return p;
}

void handleFileAction(int action) {
  showFileMenu = false;
  String fullPath = getFullPath(selectedFile >= 0 ? fileItems[selectedFile].name : "");

  switch (action) {
    case FMENU_COPY:
      if (selectedFile < 0) return;
      strlcpy(clipMount, currentMount, 32);
      strlcpy(clipPath, fullPath.c_str(), 256);
      clipCut = false;
      hasClip = true;
      break;

    case FMENU_CUT:
      if (selectedFile < 0) return;
      strlcpy(clipMount, currentMount, 32);
      strlcpy(clipPath, fullPath.c_str(), 256);
      clipCut = true;
      hasClip = true;
      break;

    case FMENU_PASTE:
      if (!hasClip) return;
      {
        String srcName = String(clipPath);
        int lastSlash = srcName.lastIndexOf('/');
        if (lastSlash >= 0) srcName = srcName.substring(lastSlash + 1);
        String dst = getFullPath(srcName.c_str());
        String endpoint = clipCut ? "/api/files/move" : "/api/files/copy";
        String body = "{\"mount\":\"" + String(currentMount) + "\",\"src\":\"" + 
                      String(clipPath) + "\",\"dst\":\"" + dst + "\"}";
        httpPost(String(apiBase) + endpoint, body);
        if (clipCut) hasClip = false;
      }
      break;

    case FMENU_DELETE:
      if (selectedFile < 0) return;
      {
        String body = "{\"mount\":\"" + String(currentMount) + "\",\"path\":\"" + fullPath + "\"}";
        httpPost(String(apiBase) + "/api/files/delete", body);
      }
      break;

    case FMENU_RENAME:
      // ESP32'de klavye yok, şimdilik atla
      break;

    case FMENU_MKDIR:
      // ESP32'de klavye yok, şimdilik atla
      break;

    case FMENU_TRASH:
      if (selectedFile < 0) return;
      {
        String body = "{\"mount\":\"" + String(currentMount) + "\",\"path\":\"" + fullPath + "\"}";
        httpPost(String(apiBase) + "/api/files/trash", body);
      }
      break;
  }

  loadFileDir();
}

// ─── SETTINGS ───────────────────────────────────────────
void showSettings() {
  String url = String(apiBase) + "/api/info";
  String resp = httpGet(url);

  tft.fillRect(0, 0, SCREEN_W, CONTENT_H, COL_BG);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(2);
  tft.drawString("Settings", 65, 8);

  if (resp != "") {
    JsonDocument doc;
    deserializeJson(doc, resp);

    const char* labels[] = { "IP", "WiFi", "Host", "User", "Version" };
    const char* keys[]   = { "ip", "wifi", "hostname", "username", "version" };

    for (int i = 0; i < 5; i++) {
      int y = 48 + i * 44;
      tft.fillRoundRect(8, y, CONTENT_W - 16, 36, 6, COL_BTN);
      tft.setTextSize(1);
      tft.setTextColor(TFT_DARKGREY, COL_BTN);
      tft.drawString(labels[i], 16, y + 4);
      tft.setTextColor(TFT_WHITE, COL_BTN);
      tft.drawString(doc[keys[i]] | "—", 16, y + 18);
    }
  }

  Btn btns[] = { {"< Back", TFT_LIGHTGREY}, {"Home", COL_ACCENT} };
  drawBtnBar(btns, 2);
}

// ─── TOUCH HANDLER ──────────────────────────────────────
void handleTouch(uint16_t x, uint16_t y) {
  // Dosya action menüsü açıksa
  if (showFileMenu) {
    int menuW = 140;
    int menuH = FMENU_COUNT * 28 + 8;
    int mx = (SCREEN_W - menuW) / 2;
    int my = (CONTENT_H - menuH) / 2;

    if (x >= mx && x <= mx + menuW && y >= my && y <= my + menuH) {
      int action = (y - my - 4) / 28;
      if (action >= 0 && action < FMENU_COUNT) {
        handleFileAction(action);
        return;
      }
    }
    showFileMenu = false;
    drawFileList();
    return;
  }

  // Buton barı
  if (y >= CONTENT_H) {
    int btnCount = 2;
    if (currentPage == PAGE_FILES) btnCount = 4;
    int bw = SCREEN_W / btnCount;
    int btn = x / bw;

    if (currentPage == PAGE_DASHBOARD) {
      if (btn == 0) { currentPage = PAGE_MENU; drawMenu(); }
      else if (btn == 1) { currentPage = PAGE_MENU; drawMenu(); }
    } else if (currentPage == PAGE_FILES) {
      if (btn == 0) {
        // Geri
        if (strlen(currentPath) == 0 && strlen(currentMount) == 0) {
          currentPage = PAGE_MENU; drawMenu();
        } else if (strlen(currentPath) == 0) {
          strcpy(currentMount, "");
          loadFileDevices();
        } else {
          String p = String(currentPath);
          int last = p.lastIndexOf('/');
          if (last >= 0) p = p.substring(0, last);
          else p = "";
          strlcpy(currentPath, p.c_str(), 256);
          loadFileDir();
        }
      } else if (btn == 1) {
        currentPage = PAGE_MENU; drawMenu();
      } else if (btn == 2) {
        showFileMenu = true;
        drawFileList();
        drawFileActionMenu();
      } else if (btn == 3) {
        if (selectedFile >= 0) {
          String fullPath = getFullPath(fileItems[selectedFile].name);
          String body = "{\"mount\":\"" + String(currentMount) + "\",\"path\":\"" + fullPath + "\"}";
          httpPost(String(apiBase) + "/api/files/trash", body);
          loadFileDir();
        }
      }
    } else if (currentPage == PAGE_SETTINGS) {
      currentPage = PAGE_MENU; drawMenu();
    }
    return;
  }

  // İçerik alanı
  if (currentPage == PAGE_MENU) {
    handleMenuTouch(x, y);
  } else if (currentPage == PAGE_FILES) {
    if (strlen(currentMount) == 0) {
      // Device seçimi
      int idx = (y - 40) / 56;
      if (idx >= 0 && idx < fileCount) {
        strlcpy(currentMount, fileItems[idx].name, 32);
        strcpy(currentPath, "");
        loadFileDir();
      }
    } else {
      // Dosya seçimi
      int visibleCount = (CONTENT_H - 20) / 28;
      int idx = (y - 18) / 28 + scrollOffset;
      if (idx >= 0 && idx < fileCount) {
        if (idx == selectedFile && fileItems[idx].isDir) {
          // Klasöre gir
          String newPath = getFullPath(fileItems[idx].name);
          strlcpy(currentPath, newPath.c_str(), 256);
          loadFileDir();
        } else {
          selectedFile = idx;
          drawFileList();
        }
      }
    }
  }
}

// ─── SETUP & LOOP ───────────────────────────────────────
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(COL_BG);
  tft.setTextColor(TFT_WHITE, COL_BG);
  tft.setTextSize(2);
  tft.drawString("Connecting...", 40, 140);

  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    tries++;
  }

  tft.fillScreen(COL_BG);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(TFT_GREEN, COL_BG);
    tft.drawString("Connected!", 60, 140);
  } else {
    tft.setTextColor(TFT_RED, COL_BG);
    tft.drawString("No WiFi!", 70, 140);
  }
  delay(800);

  drawMenu();
}

unsigned long lastRefresh = 0;

void loop() {
  uint16_t rawX, rawY, x, y;
  bool pressed = tft.getTouch(&rawX, &rawY);

  if (pressed) {
    mapTouch(rawX, rawY, x, y);
    delay(50);
    handleTouch(x, y);
    while (tft.getTouch(&rawX, &rawY)) delay(10);
  }

  // Dashboard otomatik yenileme
  if (currentPage == PAGE_DASHBOARD && !showFileMenu) {
    unsigned long now = millis();
    if (now - lastRefresh > 3000) {
      lastRefresh = now;
      showDashboard();
    }
  }
}