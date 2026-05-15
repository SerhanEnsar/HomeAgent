#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

TFT_eSPI tft = TFT_eSPI();

struct Btn {
  const char* label;
  uint16_t color;
};

struct FileItem {
  char name[64];
  bool isDir;
  long size;
};

struct TrashItem {
  char name[64];
  bool isDir;
};

const char* ssid     = "YOUR_WIFI_SSID";  // Set your Wi-Fi SSID
const char* password = "YOUR_WIFI_PASSWORD";  // Set your Wi-Fi password
const char* apiBase  = "http://AgentJee.local:8000";
const char* apiKey   = "YOUR_API_KEY";  // Match API_KEY in .env
String resolvedIP    = "";

#define SCREEN_W 240
#define SCREEN_H 320
#define BAR_H    40
#define SCROLL_W 12
#define CONTENT_W (SCREEN_W - SCROLL_W)
#define CONTENT_H (SCREEN_H - BAR_H)

#define COL_BG     TFT_BLACK
#define COL_BTNBAR 0x1A3A
#define COL_ACCENT TFT_CYAN
#define COL_BTN    0x2A5A
#define COL_BTN_HL 0x4A9A
#define COL_SCROLL 0x2A3A
#define COL_THUMB  0x4A6A

#define PAGE_MENU      0
#define PAGE_DASHBOARD 1
#define PAGE_FILES     2
#define PAGE_SETTINGS  3
#define PAGE_TRASH     4

#define FMENU_COPY   0
#define FMENU_CUT    1
#define FMENU_PASTE  2
#define FMENU_DELETE 3
#define FMENU_RENAME 4
#define FMENU_MKDIR  5
#define FMENU_TRASH  6
#define FMENU_COUNT  7

#define ARROW_X     (CONTENT_W + 1)
#define ARROW_UP_Y  2
#define ARROW_DN_Y  (CONTENT_H - 22)
#define ARROW_SIZE  18

const char* fmenuLabels[] = {
  "Copy", "Cut", "Paste", "Delete", "Rename", "New Folder", "Trash"
};

int currentPage  = PAGE_MENU;
int scrollOffset = 0;
int selectedFile = -1;
bool showFileMenu = false;

FileItem fileItems[64];
int fileCount = 0;
char currentMount[32] = "";
char currentPath[256] = "";

TrashItem trashItems[64];
int trashCount = 0;
int trashScrollOffset = 0;

char clipMount[32] = "";
char clipPath[256] = "";
bool clipCut  = false;
bool hasClip  = false;

int kbSelectedFile = -1;

// ─── KEYBOARD ───────────────────────────────────────────
char kbBuffer[64] = "";
bool kbActive  = false;
bool kbSymMode = false;
bool kbShift   = false;
void (*kbCallback)(const char*) = nullptr;
char kbTitle[32] = "";

const char* kbRows[] = {
  "QWERTYUIOP",
  "ASDFGHJKL",
  "ZXCVBNM"
};

const char* kbSymRows[] = {
  "1234567890",
  "!@#$%^&*()",
  "-_=+[]{}\\|"
};

// ─── TOUCH ──────────────────────────────────────────────
void mapTouch(uint16_t rawX, uint16_t rawY, uint16_t &sx, uint16_t &sy) {
  sx = map(rawY, 301, 1, 0, SCREEN_W);
  sy = map(rawX, 234, 3, 0, SCREEN_H);
  sx = constrain(sx, 0, SCREEN_W);
  sy = constrain(sy, 0, SCREEN_H);
}

// ─── HTTP ───────────────────────────────────────────────
String httpGet(String url) {
  if (WiFi.status() != WL_CONNECTED) return "";
  url.replace("AgentJee.local", resolvedIP);
  HTTPClient http;
  http.begin(url);
  http.setTimeout(10000);
  int code = http.GET();
  String result = "";
  if (code == 200) result = http.getString();
  http.end();
  return result;
}

String httpPost(String url, String body) {
  if (WiFi.status() != WL_CONNECTED) return "";
  url.replace("AgentJee.local", resolvedIP);
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  String result = "";
  if (code == 200 || code == 201) result = http.getString();
  http.end();
  return result;
}

// ─── UI HELPERS ─────────────────────────────────────────
void drawBar(int x, int y, int w, int h, float pct, uint16_t color) {
  tft.drawRect(x, y, w, h, TFT_DARKGREY);
  int filled = (w - 2) * constrain(pct, 0, 100) / 100;
  tft.fillRect(x+1, y+1, filled, h-2, color);
}

void drawScrollBar(int offset, int total, int visible) {
  tft.fillRect(CONTENT_W, 0, SCROLL_W, CONTENT_H, COL_SCROLL);
  if (total <= visible) return;
  int thumbH = max(20, CONTENT_H * visible / total);
  int thumbY = (CONTENT_H - thumbH) * offset / (total - visible);
  tft.fillRoundRect(CONTENT_W + 2, thumbY + 2, SCROLL_W - 4, thumbH - 4, 3, COL_THUMB);
}

void drawScrollArrows(bool canUp, bool canDown) {
  uint16_t upCol = canUp ? TFT_WHITE : TFT_DARKGREY;
  tft.fillTriangle(
    ARROW_X + ARROW_SIZE/2, ARROW_UP_Y,
    ARROW_X, ARROW_UP_Y + ARROW_SIZE,
    ARROW_X + ARROW_SIZE, ARROW_UP_Y + ARROW_SIZE,
    upCol);
  uint16_t dnCol = canDown ? TFT_WHITE : TFT_DARKGREY;
  tft.fillTriangle(
    ARROW_X, ARROW_DN_Y,
    ARROW_X + ARROW_SIZE, ARROW_DN_Y,
    ARROW_X + ARROW_SIZE/2, ARROW_DN_Y + ARROW_SIZE,
    dnCol);
}

bool handleScrollTouch(uint16_t x, uint16_t y, int &offset, int total, int visible) {
  if (x < CONTENT_W) return false;
  if (y >= ARROW_UP_Y && y <= ARROW_UP_Y + ARROW_SIZE + 5) {
    if (offset > 0) { offset--; return true; }
  }
  if (y >= ARROW_DN_Y && y <= ARROW_DN_Y + ARROW_SIZE + 5) {
    if (offset < total - visible) { offset++; return true; }
  }
  return false;
}

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

// ─── KEYBOARD ───────────────────────────────────────────
void drawKeyboard() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(1);
  tft.drawString(kbTitle, 4, 4);
  tft.fillRoundRect(4, 14, SCREEN_W - 8, 18, 4, COL_BTN);
  tft.setTextColor(TFT_WHITE, COL_BTN);
  tft.drawString(kbBuffer, 8, 18);

  const char** rows = kbSymMode ? kbSymRows : kbRows;
  int startY = 40;

  for (int r = 0; r < 3; r++) {
    int len = strlen(rows[r]);
    int keyW = SCREEN_W / 10;
    int keyH = 28;
    int offsetX = (SCREEN_W - len * keyW) / 2;
    for (int k = 0; k < len; k++) {
      int kx = offsetX + k * keyW;
      int ky = startY + r * (keyH + 3);
      tft.fillRoundRect(kx, ky, keyW - 2, keyH, 4, COL_BTN);
      tft.setTextColor(TFT_WHITE, COL_BTN);
      tft.setTextSize(1);
      char ch = rows[r][k];
      if (!kbSymMode) ch = kbShift ? toupper(ch) : tolower(ch);
      char label[2] = { ch, 0 };
      tft.drawString(label, kx + (keyW - 8) / 2, ky + 10);
    }
  }

  int botY = startY + 3 * 31 + 4;
  int bw = SCREEN_W / 4;

  tft.fillRoundRect(0, botY, bw - 2, 28, 4, COL_BTN);
  tft.setTextColor(kbShift ? COL_ACCENT : TFT_WHITE, COL_BTN);
  tft.setTextSize(1);
  tft.drawString("SHF", 6, botY + 10);

  tft.fillRoundRect(bw, botY, bw - 2, 28, 4, COL_BTN);
  tft.setTextColor(kbSymMode ? COL_ACCENT : TFT_WHITE, COL_BTN);
  tft.drawString("SYM", bw + 6, botY + 10);

  tft.fillRoundRect(bw * 2, botY, bw - 2, 28, 4, COL_BTN);
  tft.setTextColor(TFT_WHITE, COL_BTN);
  tft.drawString("SPC", bw * 2 + 6, botY + 10);

  tft.fillRoundRect(bw * 3, botY, bw - 2, 28, 4, COL_BTN);
  tft.setTextColor(TFT_YELLOW, COL_BTN);
  tft.drawString("DEL", bw * 3 + 6, botY + 10);

  int okY = botY + 32;
  tft.fillRoundRect(0, okY, SCREEN_W, 28, 4, 0x0640);
  tft.setTextColor(TFT_GREEN, 0x0640);
  tft.drawString("OK", SCREEN_W / 2 - 8, okY + 10);
}

bool handleKeyboardTouch(uint16_t x, uint16_t y) {
  if (!kbActive) return false;

  const char** rows = kbSymMode ? kbSymRows : kbRows;
  int startY = 40;
  int keyW = SCREEN_W / 10;
  int keyH = 28;

  for (int r = 0; r < 3; r++) {
    int len = strlen(rows[r]);
    int offsetX = (SCREEN_W - len * keyW) / 2;
    int ky = startY + r * (keyH + 3);
    if (y >= ky && y <= ky + keyH) {
      int k = (x - offsetX) / keyW;
      if (k >= 0 && k < len) {
        int bufLen = strlen(kbBuffer);
        if (bufLen < 63) {
          char ch = rows[r][k];
          if (!kbSymMode) ch = kbShift ? toupper(ch) : tolower(ch);
          kbBuffer[bufLen] = ch;
          kbBuffer[bufLen + 1] = 0;
          kbShift = false;
          drawKeyboard();
        }
        return true;
      }
    }
  }

  int botY = startY + 3 * 31 + 4;
  int bw = SCREEN_W / 4;

  if (y >= botY && y <= botY + 28) {
    if (x < bw) {
      kbShift = !kbShift;
      drawKeyboard();
    } else if (x < bw * 2) {
      kbSymMode = !kbSymMode;
      drawKeyboard();
    } else if (x < bw * 3) {
      int bufLen = strlen(kbBuffer);
      if (bufLen < 63) {
        kbBuffer[bufLen] = ' ';
        kbBuffer[bufLen + 1] = 0;
        drawKeyboard();
      }
    } else {
      int bufLen = strlen(kbBuffer);
      if (bufLen > 0) {
        kbBuffer[bufLen - 1] = 0;
        drawKeyboard();
      }
    }
    return true;
  }

  int okY = botY + 32;
  if (y >= okY && y <= okY + 28) {
    kbActive = false;
    kbSymMode = false;
    kbShift = false;
    if (kbCallback) kbCallback(kbBuffer);
    return true;
  }

  return false;
}

void showKeyboard(const char* title, void (*callback)(const char*)) {
  strlcpy(kbTitle, title, 32);
  kbBuffer[0] = 0;
  kbActive = true;
  kbSymMode = false;
  kbShift = false;
  kbCallback = callback;
  drawKeyboard();
}

// ─── MENU ───────────────────────────────────────────────
void drawMenu() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(2);
  tft.drawString("HomeAgent", 55, 10);

  const char* items[] = { "Dashboard", "Files", "Trash", "Settings" };
  const char* subs[]  = { "CPU/RAM/Disk/Temp", "File Explorer", "Deleted Files", "System Info" };

  for (int i = 0; i < 4; i++) {
    int y = 45 + i * 60;
    tft.fillRoundRect(8, y, SCREEN_W - 16, 50, 8, COL_BTN);
    tft.drawRoundRect(8, y, SCREEN_W - 16, 50, 8, COL_ACCENT);
    tft.setTextColor(TFT_WHITE, COL_BTN);
    tft.setTextSize(2);
    tft.drawString(items[i], 20, y + 8);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, COL_BTN);
    tft.drawString(subs[i], 20, y + 30);
  }
}

void showDashboard();
void loadFileDevices();
void showSettings();
void loadTrash();

void handleMenuTouch(uint16_t x, uint16_t y) {
  int items[] = { 45, 105, 165, 225 };
  for (int i = 0; i < 4; i++) {
    if (y >= items[i] && y <= items[i] + 50) {
      switch (i) {
        case 0: currentPage = PAGE_DASHBOARD; showDashboard(); break;
        case 1: currentPage = PAGE_FILES; loadFileDevices(); break;
        case 2: currentPage = PAGE_TRASH; loadTrash(); break;
        case 3: currentPage = PAGE_SETTINGS; showSettings(); break;
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
    tft.setTextSize(2);
    tft.drawString("Connection error", 10, 140);
    Btn btns[] = { {"< Back", TFT_LIGHTGREY}, {"Home", COL_ACCENT} };
    drawBtnBar(btns, 2);
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

  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY, COL_BG);
  tft.drawString("CPU Usage", 10, 42);
  tft.setTextSize(2); tft.setTextColor(TFT_BLUE, COL_BG);
  tft.drawString(String(cpu, 1) + "%", 10, 56);
  drawBar(10, 78, CONTENT_W - 20, 10, cpu, TFT_BLUE);

  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY, COL_BG);
  tft.drawString("RAM Usage", 10, 102);
  tft.setTextSize(2); tft.setTextColor(TFT_PURPLE, COL_BG);
  tft.drawString(String(ram, 1) + "%", 10, 116);
  drawBar(10, 138, CONTENT_W - 20, 10, ram, TFT_PURPLE);

  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY, COL_BG);
  tft.drawString("Disk Usage", 10, 162);
  tft.setTextSize(2); tft.setTextColor(TFT_GREEN, COL_BG);
  tft.drawString(String(disk, 1) + "%", 10, 176);
  drawBar(10, 198, CONTENT_W - 20, 10, disk, TFT_GREEN);

  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY, COL_BG);
  tft.drawString("Temperature", 10, 222);
  tft.setTextSize(2);
  uint16_t tc = temp > 70 ? TFT_RED : temp > 55 ? TFT_ORANGE : TFT_GREEN;
  tft.setTextColor(tc, COL_BG);
  tft.drawString(String(temp, 1) + " C", 10, 236);

  Btn btns[] = { {"< Back", TFT_LIGHTGREY}, {"Home", COL_ACCENT} };
  drawBtnBar(btns, 2);
}

// ─── FILES ──────────────────────────────────────────────
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

void drawFileList();

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
    if (!mount || strcmp(mount, "tmpfs") == 0) continue;

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

void onRenameCallback(const char* newName) {
  if (strlen(newName) == 0) { loadFileDir(); return; }
  String oldPath = String(currentPath);
  if (oldPath.length() > 0) oldPath += "/";
  oldPath += String(fileItems[kbSelectedFile].name);
  String body = "{\"mount\":\"" + String(currentMount) +
                "\",\"path\":\"" + oldPath +
                "\",\"new_name\":\"" + String(newName) + "\"}";
  httpPost(String(apiBase) + "/api/files/rename", body);
  loadFileDir();
}

void onMkdirCallback(const char* name) {
  if (strlen(name) == 0) { loadFileDir(); return; }
  String body = "{\"mount\":\"" + String(currentMount) +
                "\",\"path\":\"" + String(currentPath) +
                "\",\"name\":\"" + String(name) + "\"}";
  httpPost(String(apiBase) + "/api/files/mkdir", body);
  loadFileDir();
}

void drawFileList() {
  tft.fillRect(0, 0, CONTENT_W, CONTENT_H, COL_BG);
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
  drawScrollArrows(scrollOffset > 0, scrollOffset < fileCount - visibleCount);
  Btn btns[] = { {"< Back", TFT_LIGHTGREY}, {"Home", COL_ACCENT}, {"Action", TFT_YELLOW}, {"Trash", TFT_RED} };
  drawBtnBar(btns, 4);
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
      if (selectedFile < 0) return;
      kbSelectedFile = selectedFile;
      showKeyboard("Rename:", onRenameCallback);
      break;
    case FMENU_MKDIR:
      showKeyboard("New Folder:", onMkdirCallback);
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

// ─── TRASH ──────────────────────────────────────────────
void drawTrashList() {
  tft.fillRect(0, 0, CONTENT_W, CONTENT_H, COL_BG);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(2);
  tft.drawString("Trash", 85, 8);

  int visibleCount = (CONTENT_H - 36) / 28;
  int y = 36;

  for (int i = trashScrollOffset; i < trashCount && i < trashScrollOffset + visibleCount; i++) {
    bool sel = (i == selectedFile);
    uint16_t bg = sel ? COL_BTN_HL : COL_BG;
    tft.fillRect(0, y, CONTENT_W, 26, bg);
    tft.drawLine(0, y + 26, CONTENT_W, y + 26, COL_SCROLL);
    tft.setTextColor(trashItems[i].isDir ? TFT_CYAN : TFT_WHITE, bg);
    tft.setTextSize(1);
    String icon = trashItems[i].isDir ? "[D] " : "[F] ";
    String name = icon + String(trashItems[i].name);
    if (name.length() > 28) name = name.substring(0, 25) + "...";
    tft.drawString(name, 6, y + 8);
    y += 28;
  }

  drawScrollBar(trashScrollOffset, trashCount, visibleCount);
  drawScrollArrows(trashScrollOffset > 0, trashScrollOffset < trashCount - visibleCount);
  Btn btns[] = { {"< Back", TFT_LIGHTGREY}, {"Home", COL_ACCENT}, {"Restore", TFT_GREEN}, {"Delete", TFT_RED} };
  drawBtnBar(btns, 4);
}

void loadTrash() {
  String url = String(apiBase) + "/api/files/trash/list";
  String resp = httpGet(url);
  if (resp == "") return;

  JsonDocument doc;
  deserializeJson(doc, resp);
  JsonArray arr = doc.as<JsonArray>();

  trashCount = 0;
  for (JsonObject item : arr) {
    if (trashCount >= 64) break;
    strlcpy(trashItems[trashCount].name, item["name"] | "", 64);
    trashItems[trashCount].isDir = strcmp(item["type"] | "", "dir") == 0;
    trashCount++;
  }

  trashScrollOffset = 0;
  selectedFile = -1;
  currentPage = PAGE_TRASH;
  drawTrashList();
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
  if (kbActive) {
    handleKeyboardTouch(x, y);
    return;
  }

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

  // Scroll okları
  if (currentPage == PAGE_FILES) {
    int visibleCount = (CONTENT_H - 20) / 28;
    if (handleScrollTouch(x, y, scrollOffset, fileCount, visibleCount)) {
      drawFileList();
      return;
    }
  }

  if (currentPage == PAGE_TRASH) {
    int visibleCount = (CONTENT_H - 36) / 28;
    if (handleScrollTouch(x, y, trashScrollOffset, trashCount, visibleCount)) {
      drawTrashList();
      return;
    }
  }

  // Buton barı
  if (y >= CONTENT_H) {
    int btnCount = (currentPage == PAGE_FILES || currentPage == PAGE_TRASH) ? 4 : 2;
    int bw = SCREEN_W / btnCount;
    int btn = x / bw;

    if (currentPage == PAGE_DASHBOARD) {
      currentPage = PAGE_MENU; drawMenu();

    } else if (currentPage == PAGE_FILES) {
      if (btn == 0) {
        if (strlen(currentPath) == 0 && strlen(currentMount) == 0) {
          currentPage = PAGE_MENU; drawMenu();
        } else if (strlen(currentPath) == 0) {
          strcpy(currentMount, "");
          loadFileDevices();
        } else {
          String p = String(currentPath);
          int last = p.lastIndexOf('/');
          p = (last >= 0) ? p.substring(0, last) : "";
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

    } else if (currentPage == PAGE_TRASH) {
      if (btn == 0) { currentPage = PAGE_MENU; drawMenu(); }
      else if (btn == 1) { currentPage = PAGE_MENU; drawMenu(); }
      else if (btn == 2) {
        if (selectedFile >= 0) {
          String body = "{\"name\":\"" + String(trashItems[selectedFile].name) + "\"}";
          httpPost(String(apiBase) + "/api/files/trash/restore", body);
          loadTrash();
        }
      } else if (btn == 3) {
        if (selectedFile >= 0) {
          String body = "{\"name\":\"" + String(trashItems[selectedFile].name) + "\"}";
          httpPost(String(apiBase) + "/api/files/trash/delete", body);
          loadTrash();
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
      int idx = (y - 40) / 56;
      if (idx >= 0 && idx < fileCount) {
        strlcpy(currentMount, fileItems[idx].name, 32);
        strcpy(currentPath, "");
        loadFileDir();
      }
    } else {
      int visibleCount = (CONTENT_H - 20) / 28;
      int idx = (y - 18) / 28 + scrollOffset;
      if (idx >= 0 && idx < fileCount) {
        if (idx == selectedFile && fileItems[idx].isDir) {
          String newPath = getFullPath(fileItems[idx].name);
          strlcpy(currentPath, newPath.c_str(), 256);
          loadFileDir();
        } else {
          selectedFile = idx;
          drawFileList();
        }
      }
    }

  } else if (currentPage == PAGE_TRASH) {
    int visibleCount = (CONTENT_H - 36) / 28;
    int idx = (y - 36) / 28 + trashScrollOffset;
    if (idx >= 0 && idx < trashCount) {
      selectedFile = idx;
      drawTrashList();
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

  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip;
    if (WiFi.hostByName("AgentJee.local", ip)) {
      resolvedIP = ip.toString();
    } else {
      resolvedIP = "10.200.59.92";
    }
    tft.fillScreen(COL_BG);
    tft.setTextColor(TFT_GREEN, COL_BG);
    tft.setTextSize(2);
    tft.drawString("Connected!", 60, 140);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, COL_BG);
    tft.drawString(resolvedIP, 70, 170);
  } else {
    tft.fillScreen(COL_BG);
    tft.setTextColor(TFT_RED, COL_BG);
    tft.setTextSize(2);
    tft.drawString("No WiFi!", 70, 140);
  }

  delay(1500);
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

  if (currentPage == PAGE_DASHBOARD && !showFileMenu) {
    unsigned long now = millis();
    if (now - lastRefresh > 3000) {
      lastRefresh = now;
      showDashboard();
    }
  }
}