/*
  Dasai_Smart - ESP32 + 0.96" SSD1306 OLED + touch sensor
  - 53 animated faces stored inside the sketch (faces.h)
  - Mood engine: picks expressions by time of day, touch, weather and air quality
  - After 30 s idle: shows temperature, AQI, time and phone notifications
  - Phone notifications arrive over Wi-Fi (HTTP), see README

  Libraries: U8g2, ArduinoJson (7.x)
  Board: ESP32 Dev Module
*/
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "faces.h"

// ================== EDIT THESE ==================
const char* WIFI_SSID   = "YOURWIFI_NAME";
const char* WIFI_PASS   = "WIFI_PASSWORD";
const float LAT = 17.3850;          // your latitude  (Hyderabad)
const float LON = 78.4867;          // your longitude
const long  GMT_OFFSET_SEC = 19800; // India = +5:30
const char* NOTIFY_KEY  = "dasai123"; // secret word the phone must send
#define TOUCH_PIN     33
#define TOUCH_ACTIVE  HIGH          // TTP223 modules give HIGH when touched
const uint32_t IDLE_MS        = 30000;   // idle time before info screens
const uint32_t INFO_SLIDE_MS  = 4000;    // each info screen duration
const uint32_t WEATHER_EVERY  = 15UL * 60UL * 1000UL;
// ================================================

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// If your screen shows shifted/garbled pixels, use this instead:
// U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

WebServer server(80);

// ---------- shared data ----------
volatile float gTemp = NAN, gHum = NAN;
volatile int   gAqi = -1, gWcode = -1;
volatile bool  gHaveWeather = false, gHaveAqi = false;

struct Notif { char app[16]; char text[100]; };
Notif nq[5];
int nCount = 0;

// ---------- animation player ----------
uint8_t fb[1024];
struct Player {
  AnimId id; const uint8_t* p; uint8_t frame; uint8_t loopsLeft;
  uint32_t nextAt; bool active; bool finishing;
} pl = {A_BLINK, nullptr, 0, 0, 0, false, false};

void startAnim(AnimId id, uint8_t loops) {
  memset(fb, 0, sizeof(fb));
  pl.id = id; pl.p = ANIMS[id].data; pl.frame = 0; pl.loopsLeft = loops;
  pl.nextAt = 0; pl.active = true; pl.finishing = false;
}

void stepAnim() {
  if (!pl.active) return;
  uint32_t now = millis();
  if (now < pl.nextAt) return;
  if (pl.finishing) { pl.active = false; return; }

  // decode next delta frame into fb
  const uint8_t* p = pl.p;
  int i = 0;
  while (i < 1024) {
    uint8_t b = pgm_read_byte(p++);
    if (b & 0x80) { i += b & 0x7F; }
    else { for (uint8_t k = 0; k < b; k++) fb[i++] ^= pgm_read_byte(p++); }
  }
  pl.p = p;
  u8g2.clearBuffer();
  u8g2.drawXBM(0, 0, 128, 64, fb);
  u8g2.sendBuffer();

  uint16_t d = pgm_read_byte(ANIMS[pl.id].delays10ms + pl.frame) * 10;
  pl.nextAt = now + d;
  pl.frame++;
  if (pl.frame >= ANIMS[pl.id].frames) {
    pl.frame = 0; pl.p = ANIMS[pl.id].data; memset(fb, 0, sizeof(fb));
    if (pl.loopsLeft > 0) pl.loopsLeft--;
    if (pl.loopsLeft == 0) pl.finishing = true;   // wait out last frame delay, then stop
  }
}

// ---------- expression groups ----------
const AnimId G_CALM[]    = {A_BLINK, A_BLINK, A_LEFT, A_RIGHT, A_DISTRACTED, A_SQUINT, A_SMIRK, A_SERENE, A_SMILE, A_GROWING};
const AnimId G_HAPPY[]   = {A_HAPPY, A_SMILE, A_GIGGLE, A_LAUGHING, A_GLOWING, A_SPARKLE, A_WINK, A_HANDSOME, A_PLAYFUL, A_ENCOURAGEMENT, A_HELLO};
const AnimId G_EXCITED[] = {A_ENERGETIC, A_DANCING, A_BUZZING, A_SPEED, A_BRAVE, A_TEASING, A_PLAYFUL};
const AnimId G_LOVE[]    = {A_LOVE, A_ADORE, A_SHY, A_GLOWING, A_SPARKLE};
const AnimId G_SLEEPY[]  = {A_SLEEPY, A_DROWSY, A_YAWN, A_DOWN, A_BLINK};
const AnimId G_GRUMPY[]  = {A_IRRITATED, A_CONTEMPT, A_ANGRY, A_TOUGH, A_FIERCE, A_MENACING};
const AnimId G_RAGE[]    = {A_FURIOUS, A_ENRAGED, A_EVIL, A_DEVIL, A_ANGRY, A_DIZZY};
const AnimId G_SICK[]    = {A_SICK, A_SNEEZE, A_SMOKE, A_DIZZY, A_SHRINK, A_SCARED};
const AnimId G_RAINY[]   = {A_RAIN, A_SPLASH, A_SPRAYING, A_WEEPING, A_DOWN, A_SHRINK};
const AnimId G_HOT[]     = {A_DROWSY, A_SICK, A_SPLASH, A_SPRAYING, A_SQUINT, A_SLEEPY};
const AnimId G_SURPRISE[]= {A_SURPRISED, A_SCARED, A_HELLO};
#define PICK(g) pickFrom(g, sizeof(g) / sizeof(g[0]))

AnimId recent[6] = {A_COUNT, A_COUNT, A_COUNT, A_COUNT, A_COUNT, A_COUNT};
bool wasRecent(AnimId a) { for (auto r : recent) if (r == a) return true; return false; }
void remember(AnimId a) { for (int i = 5; i > 0; i--) recent[i] = recent[i - 1]; recent[0] = a; }

AnimId pickFrom(const AnimId* g, int n) {
  for (int t = 0; t < 12; t++) {            // avoid repeating the last 6 faces
    AnimId a = g[random(n)];
    if (!wasRecent(a)) return a;
  }
  return g[random(n)];
}

// ---------- state ----------
enum Mode { M_FACE, M_INFO, M_NOTIF };
Mode mode = M_FACE;
uint32_t lastActivity = 0;       // last touch / notification
uint32_t faceStartedAt = 0;      // when the current face round began
uint32_t modeSince = 0;
int infoPage = 0;
int tapCount = 0; uint32_t tapWindowStart = 0;
uint32_t lastInfoRound = 0;
bool pendingInfo = false;
bool lastTouch = false; uint32_t touchDownAt = 0;
bool longHandled = false;
int  affection = 0;              // rises with touches, fades with time
uint32_t lastDecay = 0;

int hourNow() {
  struct tm t;
  if (getLocalTime(&t, 0)) return t.tm_hour;
  return -1;
}
bool isRainCode(int c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || (c >= 95 && c <= 99); }

// Choose an expression that fits the situation
AnimId chooseIdleFace() {
  int h = hourNow();
  uint32_t quiet = millis() - lastActivity;
  bool night = (h >= 23 || (h >= 0 && h < 6));
  int roll = random(100);

  if (night) return roll < 80 ? PICK(G_SLEEPY) : PICK(G_CALM);
  if (quiet > 5UL * 60000) return roll < 70 ? PICK(G_SLEEPY) : PICK(G_CALM);      // bored for a long time
  if (gHaveAqi && gAqi > 150 && roll < 30) return PICK(G_SICK);                    // bad air
  if (gHaveWeather && isRainCode(gWcode) && roll < 30) return PICK(G_RAINY);       // raining
  if (gHaveWeather && gTemp > 35.0f && roll < 30) return PICK(G_HOT);              // very hot
  if (affection > 4 && roll < 45) return PICK(G_LOVE);                             // recently petted a lot
  if (h >= 6 && h < 10 && roll < 25) return PICK(G_EXCITED);                       // morning energy
  if (quiet > 2UL * 60000) { if (roll < 40) return PICK(G_SLEEPY); }               // getting sleepy
  if (roll < 45) return PICK(G_CALM);
  if (roll < 80) return PICK(G_HAPPY);
  return PICK(G_EXCITED);
}

void playFace(AnimId a, bool allowLoops = true) {
  remember(a);
  uint32_t total = 0;
  for (int i = 0; i < ANIMS[a].frames; i++) total += pgm_read_byte(ANIMS[a].delays10ms + i) * 10;
  uint8_t loops = 1;
  if (allowLoops && total < 2200) loops = 2;
  if (allowLoops && total < 1200) loops = 3;
  startAnim(a, loops);
}

void goFace() { mode = M_FACE; modeSince = millis(); playFace(chooseIdleFace()); }

// ---------- reacting to touch ----------
void reactToTap() {
  uint32_t now = millis();
  if (now - tapWindowStart > 4000) { tapWindowStart = now; tapCount = 0; }
  tapCount++;
  affection = min(affection + 1, 12);
  AnimId a;
  if (tapCount >= 8)      a = PICK(G_RAGE);
  else if (tapCount >= 5) a = PICK(G_GRUMPY);
  else if (tapCount == 1 && random(100) < 35) a = PICK(G_SURPRISE);
  else if (affection > 5 && random(100) < 50) a = PICK(G_LOVE);
  else                    a = random(100) < 70 ? PICK(G_HAPPY) : PICK(G_EXCITED);
  mode = M_FACE; modeSince = now;
  playFace(a, false);
}

void reactToLongPress() {
  affection = min(affection + 3, 12);
  if (nCount > 0) { nCount = 0; }       // long press clears notifications
  mode = M_FACE; modeSince = millis();
  playFace(PICK(G_LOVE), true);
}

// ---------- drawing text screens ----------
String ascii(const String& s) {
  String o; for (size_t i = 0; i < s.length(); i++) { unsigned char c = s[i]; if (c >= 32 && c < 127) o += (char)c; else if (c == '\n') o += ' '; }
  return o;
}

void drawWrapped(const String& txt, int x, int y, int maxW, int lineH, int maxLines) {
  String line = ""; int ln = 0; int start = 0;
  String s = txt + " ";
  while (start < (int)s.length() && ln < maxLines) {
    int sp = s.indexOf(' ', start);
    if (sp < 0) sp = s.length();
    String w = s.substring(start, sp);
    String test = line.length() ? line + " " + w : w;
    if (u8g2.getStrWidth(test.c_str()) > maxW && line.length()) {
      u8g2.drawStr(x, y + ln * lineH, line.c_str()); ln++; line = w;
    } else line = test;
    start = sp + 1;
  }
  if (ln < maxLines && line.length()) u8g2.drawStr(x, y + ln * lineH, line.c_str());
}

void drawInfoPage(int page) {
  u8g2.clearBuffer();
  char buf[32];
  if (page == 0) {
    u8g2.setFont(u8g2_font_6x12_tf); u8g2.drawStr(0, 10, "TEMPERATURE");
    u8g2.setFont(u8g2_font_logisoso28_tn);
    if (gHaveWeather) { snprintf(buf, sizeof buf, "%.0f", (double)gTemp); u8g2.drawStr(8, 52, buf);
      int w = u8g2.getStrWidth(buf);
      u8g2.setFont(u8g2_font_helvB14_tf); u8g2.drawStr(8 + w + 4, 30, "C");
      u8g2.setFont(u8g2_font_6x12_tf); snprintf(buf, sizeof buf, "Humidity %.0f%%", (double)gHum); u8g2.drawStr(60, 60, buf);
    } else { u8g2.setFont(u8g2_font_helvB14_tf); u8g2.drawStr(8, 45, "--"); }
  } else if (page == 1) {
    u8g2.setFont(u8g2_font_6x12_tf); u8g2.drawStr(0, 10, "AIR QUALITY (AQI)");
    u8g2.setFont(u8g2_font_logisoso28_tn);
    if (gHaveAqi) {
      snprintf(buf, sizeof buf, "%d", gAqi); u8g2.drawStr(8, 52, buf);
      const char* lbl = gAqi <= 50 ? "Good" : gAqi <= 100 ? "Moderate" : gAqi <= 150 ? "Unhealthy-sens." : gAqi <= 200 ? "Unhealthy" : gAqi <= 300 ? "Very unhealthy" : "Hazardous";
      u8g2.setFont(u8g2_font_helvB10_tf); u8g2.drawStr(60, 40, lbl);
    } else { u8g2.setFont(u8g2_font_helvB14_tf); u8g2.drawStr(8, 45, "--"); }
  } else if (page == 2) {
    struct tm t;
    u8g2.setFont(u8g2_font_6x12_tf); u8g2.drawStr(0, 10, "TIME");
    if (getLocalTime(&t, 0)) {
      u8g2.setFont(u8g2_font_logisoso28_tn); snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min); u8g2.drawStr(6, 50, buf);
      u8g2.setFont(u8g2_font_6x12_tf); strftime(buf, sizeof buf, "%a %d %b", &t); u8g2.drawStr(70, 62, buf);
    } else { u8g2.setFont(u8g2_font_helvB14_tf); u8g2.drawStr(8, 45, "--:--"); }
  } else {
    u8g2.setFont(u8g2_font_6x12_tf); u8g2.drawStr(0, 10, "NOTIFICATIONS");
    u8g2.setFont(u8g2_font_logisoso28_tn); snprintf(buf, sizeof buf, "%d", nCount); u8g2.drawStr(8, 52, buf);
    u8g2.setFont(u8g2_font_6x12_tf); u8g2.drawStr(60, 40, nCount ? "waiting" : "none");
  }
  u8g2.sendBuffer();
}

int infoPageCount() { return 3 + (nCount > 0 ? 1 : 0); }
int notifShown = 0;

void drawNotif(int idx) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawBox(0, 0, 128, 12); u8g2.setDrawColor(0);
  char hdr[40]; snprintf(hdr, sizeof hdr, "%s (%d/%d)", nq[idx].app, idx + 1, nCount);
  u8g2.drawStr(2, 10, hdr); u8g2.setDrawColor(1);
  drawWrapped(String(nq[idx].text), 0, 24, 128, 12, 4);
  u8g2.sendBuffer();
}

// ---------- notifications (HTTP) ----------
void addNotif(const String& app, const String& text) {
  if (nCount == 5) { for (int i = 1; i < 5; i++) nq[i - 1] = nq[i]; nCount = 4; }
  String a = ascii(app), t = ascii(text);
  if (!a.length()) a = "Phone"; if (!t.length()) t = "(new message)";
  strlcpy(nq[nCount].app, a.c_str(), sizeof nq[nCount].app);
  strlcpy(nq[nCount].text, t.c_str(), sizeof nq[nCount].text);
  nCount++;
  lastActivity = millis();
  // react: surprised face, then show the message
  mode = M_NOTIF; modeSince = millis(); notifShown = nCount - 1;
  startAnim(A_SURPRISED, 1);
}

void handleNotify() {
  if (server.arg("key") != NOTIFY_KEY) { server.send(403, "text/plain", "bad key"); return; }
  addNotif(server.arg("app"), server.arg("text"));
  server.send(200, "text/plain", "ok");
}

// ---------- weather task ----------
bool httpsGetJson(const String& url, JsonDocument& doc) {
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h; h.setTimeout(8000);
  if (!h.begin(c, url)) return false;
  int code = h.GET(); bool ok = false;
  if (code == 200) { String payload = h.getString(); ok = !deserializeJson(doc, payload); }
  h.end(); return ok;
}

void fetchWeather() {
  JsonDocument doc;
  String u = String("https://api.open-meteo.com/v1/forecast?latitude=") + LAT + "&longitude=" + LON +
             "&current=temperature_2m,relative_humidity_2m,weather_code";
  if (httpsGetJson(u, doc)) {
    gTemp = doc["current"]["temperature_2m"] | NAN;
    gHum = doc["current"]["relative_humidity_2m"] | NAN;
    gWcode = doc["current"]["weather_code"] | -1;
    gHaveWeather = !isnan(gTemp);
  }
  JsonDocument d2;
  u = String("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=") + LAT + "&longitude=" + LON + "&current=us_aqi";
  if (httpsGetJson(u, d2)) {
    gAqi = d2["current"]["us_aqi"] | -1;
    gHaveAqi = gAqi >= 0;
  }
}

void weatherTask(void*) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) fetchWeather();
    vTaskDelay(pdMS_TO_TICKS((gHaveWeather && gHaveAqi) ? WEATHER_EVERY : 60000));
  }
}

// ---------- setup / loop ----------
void showMsg(const char* a, const char* b = "") {
  u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 24, a); u8g2.drawStr(0, 40, b); u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT);
  u8g2.begin(); u8g2.setBusClock(400000);
  randomSeed(esp_random());

  showMsg("Connecting WiFi...");
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    configTime(GMT_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
    server.on("/notify", handleNotify);
    server.on("/", []() { server.send(200, "text/plain", "Dasai is alive"); });
    server.begin();
    String ip = WiFi.localIP().toString();
    Serial.println("IP: " + ip);
    showMsg("WiFi OK", ip.c_str()); delay(2500);
  } else {
    showMsg("No WiFi", "faces only"); delay(1500);
  }
  xTaskCreatePinnedToCore(weatherTask, "wx", 12288, nullptr, 1, nullptr, 0);

  lastActivity = millis(); lastInfoRound = millis();
  startAnim(A_HELLO, 1);       // greet on boot
  mode = M_FACE; modeSince = millis();
}

void loop() {
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) server.handleClient();

  // affection fades slowly
  if (now - lastDecay > 20000) { lastDecay = now; if (affection > 0) affection--; }

  // ----- touch handling -----
  bool t = (digitalRead(TOUCH_PIN) == TOUCH_ACTIVE);
  if (t && !lastTouch) { touchDownAt = now; longHandled = false; }
  if (t && !longHandled && now - touchDownAt > 900) { longHandled = true; lastActivity = now; lastInfoRound = now; reactToLongPress(); }
  if (!t && lastTouch && !longHandled && now - touchDownAt > 40) { lastActivity = now; lastInfoRound = now; reactToTap(); }
  lastTouch = t;

  // ----- modes -----
  if (mode == M_FACE) {
    stepAnim();
    if (!pl.active) {
      if (now - lastInfoRound > IDLE_MS && now - lastActivity > IDLE_MS) {
        // idle long enough: switch to info screens (only between animations)
        mode = M_INFO; modeSince = now; infoPage = 0; drawInfoPage(0);
      } else {
        goFace();
      }
    }
  } else if (mode == M_INFO) {
    if (now - modeSince > INFO_SLIDE_MS) {
      infoPage++; modeSince = now;
      if (infoPage >= infoPageCount()) { lastInfoRound = now; goFace(); }
      else drawInfoPage(infoPage);
    }
  } else if (mode == M_NOTIF) {
    if (pl.active) stepAnim();                          // surprised reaction first
    else {
      static int drawnIdx = -1; static uint32_t shownAt = 0;
      if (drawnIdx != notifShown || shownAt < modeSince) { drawNotif(notifShown); drawnIdx = notifShown; shownAt = now; }
      if (now - shownAt > 7000) { drawnIdx = -1; lastInfoRound = now; goFace(); }
    }
  }
  delay(2);
}
