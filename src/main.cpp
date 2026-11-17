// =====================================================================
//  WiFi Repeater untuk LilyGo T-Display (TTGO, ESP32 biasa)
//  Mode AP+STA dengan NAT (lwIP NAPT). Layar 240x135, dua tombol, web UI.
//
//  Tombol atas  (GPIO35): pilih / ketik.  Dobel klik = power off.
//  Tombol bawah (GPIO0) : klik = lanjut,  tahan = menu / kembali.
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
extern "C" {
#include "lwip/lwip_napt.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
}
#include "fonts.h"
#include "webui.h"

// ------------------------------ pin & konstanta ----------------------
#define PIN_BTN_TOP 35
#define PIN_BTN_BOT 0
#define PIN_BL      4
#define PIN_BAT_ADC 34
#define PIN_BAT_EN  14

#define FW_VERSION "1.0"
#ifndef ROT_A
#define ROT_A 1          // rotasi "Normal"; kalau terbalik ganti -DROT_A=3 -DROT_B=1
#endif
#ifndef ROT_B
#define ROT_B 3
#endif

#define HIST_N 56
static const uint8_t  BR_DUTY[5]  = {26, 64, 128, 191, 255};
static const char*    BR_NAME[5]  = {"10%", "25%", "50%", "75%", "100%"};
static const uint16_t DIM_SEC[5]  = {0, 15, 30, 60, 300};
static const char*    DIM_NAME[5] = {"Tidak", "15 dtk", "30 dtk", "1 mnt", "5 mnt"};
static const uint8_t  DIM_DUTY    = 6;
static const uint16_t DBL_MS      = 450;   // jarak antar-penekanan untuk dobel klik
static const uint16_t LONG_MS     = 600;   // tahan tombol bawah

// ------------------------------ warna --------------------------------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
static const uint16_t C_BG    = rgb(14, 19, 17);
static const uint16_t C_LINE  = rgb(38, 48, 43);
static const uint16_t C_DIM   = rgb(124, 138, 130);
static const uint16_t C_TXT   = rgb(221, 227, 221);
static const uint16_t C_AMB   = rgb(255, 176, 32);
static const uint16_t C_AMB_D = rgb(60, 45, 14);
static const uint16_t C_OK    = rgb(91, 209, 139);
static const uint16_t C_ERR   = rgb(229, 96, 77);

// ------------------------------ objek global -------------------------
TFT_eSPI    tft;
TFT_eSprite spr(&tft);
WebServer   server(80);
Preferences prefs;

struct Config {
  String  staSsid, staPass, apSsid, apPass;
  uint8_t bright, dimIdx, rot, apNet;
} cfg;

// status jaringan
volatile bool     staOnline  = false;
volatile bool     gotIp      = false;
volatile uint16_t lastReason = 0;
bool natOn = false;
uint32_t apRestartAt = 0, rebootAt = 0;

// trafik
static volatile uint32_t cntDn = 0, cntUp = 0;
static netif_input_fn      origIn  = nullptr;
static netif_linkoutput_fn origOut = nullptr;
uint32_t lastDn = 0, lastUp = 0, rateDn = 0, rateUp = 0;
uint64_t totDn = 0, totUp = 0;
uint32_t histDn[HIST_N], histUp[HIST_N];
uint8_t  histPos = 0;

// klien
struct Client { uint8_t mac[6]; uint32_t ip; int8_t rssi; };
Client  clients[10];
uint8_t nClients = 0;

// pindai
struct Net { char ssid[33]; int8_t rssi; uint8_t ch; bool sec; };
Net     nets[24];
uint8_t nNets = 0;
bool    scanning = false;
uint32_t scanStarted = 0;

// baterai
int batMv = -1;

// ------------------------------ UI state -----------------------------
enum Screen : uint8_t { SC_MAIN, SC_MENU, SC_SCAN, SC_KEYS };
enum MenuItem : uint8_t { M_SCAN, M_BRIGHT, M_DIM, M_ROT, M_APSSID, M_APPASS, M_FORGET, M_REBOOT, M_OFF, M_BACK, M_COUNT };
enum KeyTarget : uint8_t { KT_STA_PASS, KT_STA_SSID, KT_AP_SSID, KT_AP_PASS };

Screen  screen = SC_MAIN;
uint8_t page = 0, clientScroll = 0, menuSel = 0, scanSel = 0;
bool    dirty = true, dimmed = false;
uint32_t lastActivity = 0;
uint32_t menuConfirmUntil = 0;
char    toastMsg[40] = "";
uint32_t toastUntil = 0;

// keyboard
KeyTarget keyTarget = KT_STA_PASS;
String  keyBuf, keyTitle, keySub, pendingSsid;
bool    pendingSec = true, keyShow = false;
uint8_t keySet = 0, keyRow = 0, keyMax = 63;
int8_t  keyCol = -1;   // -1 = mode pilih baris
static const char* KEYROWS[3][4] = {
  {"abcdefghij", "klmnopqrst", "uvwxyz0123", "456789.-_@"},
  {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789.-_@"},
  {"!@#$%^&*()", "-_=+[]{}\\|", "'\",.<>/?`~", ";:"}
};
static const char* SETNAME[3] = {"abc", "ABC", "#+="};

// ------------------------------ tombol -------------------------------
struct Btn {
  uint8_t  pin = 0;
  bool     raw = false, st = false, longFired = true;
  uint32_t tEdge = 0, tDown = 0;
  bool     ePress = false, eClick = false, eLong = false;
  // Baca ulang status tombol saat start/bangun: klik pembangun tidak dihitung.
  void begin(uint8_t p, bool pullup) {
    pin = p; pinMode(p, pullup ? INPUT_PULLUP : INPUT);
    raw = st = (digitalRead(p) == LOW);
    tEdge = tDown = millis(); longFired = true;
  }
  void update(uint32_t now) {
    ePress = eClick = eLong = false;
    bool r = (digitalRead(pin) == LOW);
    if (r != raw) { raw = r; tEdge = now; }
    if (st != raw && now - tEdge >= 25) {
      st = raw;
      if (st) { ePress = true; tDown = now; longFired = false; }
      else if (!longFired) eClick = true;
    }
    if (st && !longFired && now - tDown >= LONG_MS) { longFired = true; eLong = true; }
  }
} top, bot;
uint32_t topPrev = 0; bool haveTopPrev = false, botSwallow = false;

// ------------------------------ util ---------------------------------
String jesc(const String& s) {
  String o; o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else o += c;
  }
  return o;
}
String macStr(const uint8_t* m) {
  char b[18]; snprintf(b, sizeof(b), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}
void toast(const char* m) { strlcpy(toastMsg, m, sizeof(toastMsg)); toastUntil = millis() + 2200; dirty = true; }
uint8_t rotation() { return cfg.rot ? ROT_B : ROT_A; }

String fmtUptime() {
  unsigned long s = millis() / 1000; char b[20];
  if (s >= 86400) snprintf(b, sizeof(b), "%lud %02lu:%02lu", s / 86400, (s % 86400) / 3600, (s % 3600) / 60);
  else snprintf(b, sizeof(b), "%02lu:%02lu:%02lu", s / 3600, (s % 3600) / 60, s % 60);
  return String(b);
}
void fmtRate(uint32_t Bps, char* num, size_t n, const char** unit) {
  uint64_t bits = (uint64_t)Bps * 8;
  if (bits >= 1000000) { snprintf(num, n, "%.1f", bits / 1e6); *unit = "Mb/s"; }
  else { snprintf(num, n, "%lu", (unsigned long)(bits / 1000)); *unit = "kb/s"; }
}
String fmtBytes(uint64_t b) {
  char s[16];
  if (b >= (1ULL << 30)) snprintf(s, sizeof(s), "%.2f GB", b / 1073741824.0);
  else if (b >= (1ULL << 20)) snprintf(s, sizeof(s), "%.1f MB", b / 1048576.0);
  else snprintf(s, sizeof(s), "%lu KB", (unsigned long)(b / 1024));
  return String(s);
}

// ------------------------------ konfigurasi --------------------------
void loadCfg() {
  prefs.begin("rpt", false);
  cfg.staSsid = prefs.getString("ssid", "");
  cfg.staPass = prefs.getString("pass", "");
  cfg.apSsid  = prefs.getString("apssid", "TTGO-EXT");
  cfg.apPass  = prefs.getString("appass", "ttgo-repeater");
  cfg.bright  = min<uint8_t>(prefs.getUChar("br", 3), 4);
  cfg.dimIdx  = min<uint8_t>(prefs.getUChar("dim", 2), 4);
  cfg.rot     = prefs.getUChar("rot", 0) ? 1 : 0;
  cfg.apNet   = prefs.getUChar("apnet", 4);
  prefs.end();
}
void saveCfg() {
  prefs.begin("rpt", false);
  prefs.putString("ssid", cfg.staSsid);
  prefs.putString("pass", cfg.staPass);
  prefs.putString("apssid", cfg.apSsid);
  prefs.putString("appass", cfg.apPass);
  prefs.putUChar("br", cfg.bright);
  prefs.putUChar("dim", cfg.dimIdx);
  prefs.putUChar("rot", cfg.rot);
  prefs.putUChar("apnet", cfg.apNet);
  prefs.end();
}

// ------------------------------ layar & backlight --------------------
void applyBrightness() { ledcWrite(0, dimmed ? DIM_DUTY : BR_DUTY[cfg.bright]); }

void powerOff() {
  while (digitalRead(PIN_BTN_TOP) == LOW) delay(10);   // tunggu tombol dilepas
  delay(80);
  ledcDetachPin(PIN_BL);
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, LOW);                           // backlight LOW
  tft.writecommand(TFT_DISPOFF);
  tft.writecommand(TFT_SLPIN);
  delay(120);
  gpio_hold_en((gpio_num_t)PIN_BL);                    // jaga backlight tetap mati
  gpio_deep_sleep_hold_en();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);        // nyala: cukup tekan sekali
  esp_deep_sleep_start();
}

// ------------------------------ baterai ------------------------------
void readBattery() {
  pinMode(PIN_BAT_EN, OUTPUT);
  digitalWrite(PIN_BAT_EN, HIGH);                      // HIGH hanya saat mengukur
  delay(5);
  uint32_t s = 0;
  for (int i = 0; i < 8; i++) s += analogReadMilliVolts(PIN_BAT_ADC);
  digitalWrite(PIN_BAT_EN, LOW);
  int mv = (int)(s / 8) * 2;
  batMv = (mv > 2800) ? mv : -1;                       // valid jika di atas 2800 mV
}

// ------------------------------ hitung trafik ------------------------
static err_t hookIn(struct pbuf* p, struct netif* n) { cntDn += p->tot_len; return origIn(p, n); }
static err_t hookOut(struct netif* n, struct pbuf* p) { cntUp += p->tot_len; return origOut(n, p); }

void installHooks() {
  if (origIn) return;
  esp_netif_t* st = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!st) return;
  struct netif* n = (struct netif*)esp_netif_get_netif_impl(st);
  if (!n || !n->input || !n->linkoutput) return;
  origIn = n->input; origOut = n->linkoutput;
  n->input = hookIn; n->linkoutput = hookOut;
}
void sampleTraffic() {
  uint32_t d = cntDn, u = cntUp;
  rateDn = d - lastDn; rateUp = u - lastUp;
  lastDn = d; lastUp = u;
  totDn += rateDn; totUp += rateUp;
  histDn[histPos] = rateDn; histUp[histPos] = rateUp;
  histPos = (histPos + 1) % HIST_N;
}

// ------------------------------ WiFi / NAT ---------------------------
void applyApIp(uint8_t net) {
  IPAddress ip(192, 168, net, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
}
void startAp() {
  uint8_t ch = staOnline ? WiFi.channel() : 1;
  const char* pw = (cfg.apPass.length() >= 8) ? cfg.apPass.c_str() : nullptr;
  WiFi.softAP(cfg.apSsid.c_str(), pw, ch, 0, 6);
}
void setDhcpDns() {
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) return;
  IPAddress d = WiFi.dnsIP(0);
  if (d == IPAddress(0, 0, 0, 0)) d = IPAddress(1, 1, 1, 1);
  esp_netif_dns_info_t info; memset(&info, 0, sizeof(info));
  info.ip.u_addr.ip4.addr = (uint32_t)d;
  info.ip.type = ESP_IPADDR_TYPE_V4;
  uint8_t offer = 2;                                   // OFFER_DNS
  esp_netif_dhcps_stop(ap);
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
  esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &info);
  esp_netif_dhcps_start(ap);
}
void natSetup() {
  IPAddress sta = WiFi.localIP();
  uint8_t net = cfg.apNet;
  if (sta[0] == 192 && sta[1] == 168 && sta[2] == net) net = (net == 4) ? 5 : 4;   // hindari subnet bentrok
  if (net != cfg.apNet) { cfg.apNet = net; saveCfg(); applyApIp(net); }
  setDhcpDns();
  ip_napt_enable((uint32_t)WiFi.softAPIP(), 1);
  natOn = true;
}
void connectSta() {
  if (cfg.staSsid.isEmpty()) return;
  lastReason = 0;
  WiFi.disconnect(false, false);
  WiFi.begin(cfg.staSsid.c_str(), cfg.staPass.length() ? cfg.staPass.c_str() : nullptr);
}
void applySta(const String& ssid, const String& pass) {
  cfg.staSsid = ssid; cfg.staPass = pass; saveCfg();
  staOnline = false;
  connectSta();
}
void forgetSta() {
  cfg.staSsid = ""; cfg.staPass = ""; saveCfg();
  staOnline = false; lastReason = 0;
  WiFi.disconnect(false, false);
}
void onWiFiEvent(WiFiEvent_t ev, WiFiEventInfo_t info) {
  if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP) { lastReason = 0; staOnline = true; gotIp = true; }
  else if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) { staOnline = false; lastReason = info.wifi_sta_disconnected.reason; }
}

struct StatusInfo { const char* text; uint16_t color; };
StatusInfo statusInfo() {
  if (cfg.staSsid.isEmpty()) return {"Belum diatur", C_DIM};
  if (staOnline) return {"Online", C_OK};
  switch (lastReason) {
    case 2: case 15: case 202: case 204: case 205: return {"Sandi salah?", C_ERR};
    case 201: return {"AP tak ditemukan", C_ERR};
  }
  return {"Menyambung", C_AMB};
}
const char* staStateName() {
  if (cfg.staSsid.isEmpty()) return "unset";
  return staOnline ? "online" : "connecting";
}
const char* reasonText() {
  switch (lastReason) {
    case 2: case 15: case 202: case 204: case 205: return "Sandi salah atau ditolak oleh router.";
    case 201: return "Jaringan tidak ditemukan. Cek nama dan jangkauan.";
  }
  return "";
}

void refreshClients() {
  nClients = 0;
  wifi_sta_list_t wl; memset(&wl, 0, sizeof(wl));
  if (esp_wifi_ap_get_sta_list(&wl) != ESP_OK) return;
  esp_netif_sta_list_t nl; memset(&nl, 0, sizeof(nl));
  esp_netif_get_sta_list(&wl, &nl);                    // IP bisa kosong kalau DHCP belum selesai
  for (int i = 0; i < wl.num && i < 10; i++) {
    memcpy(clients[i].mac, wl.sta[i].mac, 6);
    clients[i].rssi = wl.sta[i].rssi;
    clients[i].ip = 0;
    for (int k = 0; k < nl.num; k++)
      if (!memcmp(nl.sta[k].mac, wl.sta[i].mac, 6)) { clients[i].ip = nl.sta[k].ip.addr; break; }
    nClients++;
  }
}

// ------------------------------ pindai WiFi --------------------------
void startScan() {
  if (scanning) return;
  scanning = true; nNets = 0; scanStarted = millis();
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false, false, 300);
}
void pollScan() {
  if (!scanning) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { if (millis() - scanStarted > 15000) scanning = false; return; }
  scanning = false; dirty = true;
  if (n < 0) return;
  nNets = 0;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0 || s.length() > 32) continue;
    int rssi = WiFi.RSSI(i); bool dup = false;
    for (int k = 0; k < nNets; k++) if (s == nets[k].ssid) {
      dup = true;
      if (rssi > nets[k].rssi) { nets[k].rssi = rssi; nets[k].ch = WiFi.channel(i); nets[k].sec = WiFi.encryptionType(i) != WIFI_AUTH_OPEN; }
      break;
    }
    if (dup) continue;
    if (nNets >= 24) break;
    strlcpy(nets[nNets].ssid, s.c_str(), sizeof(nets[nNets].ssid));
    nets[nNets].rssi = rssi; nets[nNets].ch = WiFi.channel(i);
    nets[nNets].sec = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    nNets++;
  }
  WiFi.scanDelete();
  for (int i = 1; i < nNets; i++) {                    // urut sinyal terkuat
    Net t = nets[i]; int j = i - 1;
    while (j >= 0 && nets[j].rssi < t.rssi) { nets[j + 1] = nets[j]; j--; }
    nets[j + 1] = t;
  }
  scanSel = 0;
}

// ------------------------------ gambar -------------------------------
void txt(const GFXfont* f, int x, int y, const char* s, uint16_t col, int8_t align = 0) {
  spr.setFreeFont(f);
  spr.setTextColor(col);
  if (align) { int w = spr.textWidth(s); x -= (align == 1) ? w / 2 : w; }
  spr.setCursor(x, y);                                 // y = garis dasar huruf
  spr.print(s);
}
String fit(const String& s, const GFXfont* f, int maxw) {
  spr.setFreeFont(f);
  if (spr.textWidth(s) <= maxw) return s;
  String t = s;
  while (t.length() > 1 && spr.textWidth(t + "..") > maxw) t.remove(t.length() - 1);
  return t + "..";
}
void drawBars(int x, int yb, int rssi, bool on) {
  int lv = !on ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  for (int i = 0; i < 4; i++) { int h = 3 + i * 2; spr.fillRect(x + i * 4, yb - h, 3, h, i < lv ? C_TXT : C_LINE); }
}
void drawBat(int x, int y) {
  if (batMv < 0) { txt(&InterS, x + 20, y + 8, "USB", C_DIM, 2); return; }
  int pct = constrain(map(batMv, 3300, 4150, 0, 100), 0, 100);
  spr.drawRoundRect(x, y, 18, 9, 2, C_DIM);
  spr.fillRect(x + 18, y + 3, 2, 3, C_DIM);
  int w = pct * 14 / 100;
  if (w > 0) spr.fillRect(x + 2, y + 2, w, 5, pct < 20 ? C_ERR : C_TXT);
}
void drawLock(int x, int y, uint16_t c) { spr.drawRoundRect(x + 1, y, 5, 6, 2, c); spr.fillRect(x, y + 4, 7, 5, c); }
void scrollbar(int first, int rows, int total, int y0, int h) {
  if (total <= rows) return;
  spr.fillRect(238, y0, 2, h, C_LINE);
  int th = max(6, h * rows / total);
  spr.fillRect(238, y0 + (h - th) * first / (total - rows), 2, th, C_DIM);
}
void drawHeader(const char* title, uint16_t dot, bool sig) {
  int x = 8;
  if (dot) { spr.fillCircle(10, 9, 3, dot); x = 18; }
  txt(&InterMB, x, 13, title, C_TXT);
  if (sig) {
    drawBat(214, 4);
    drawBars(188, 13, WiFi.RSSI(), staOnline);
    if (staOnline) { char b[8]; snprintf(b, sizeof(b), "%d", WiFi.RSSI()); txt(&InterS, 184, 12, b, C_DIM, 2); }
  }
  spr.drawFastHLine(0, 17, 240, C_LINE);
}
void drawFooter(const char* right) {
  spr.drawFastHLine(0, 120, 240, C_LINE);
  txt(&InterS, 8, 131, fmtUptime().c_str(), C_DIM);
  for (int i = 0; i < 3; i++) {
    if (i == page) spr.fillCircle(112 + i * 8, 127, 2, C_AMB); else spr.fillCircle(112 + i * 8, 127, 1, C_DIM);
  }
  if (right && *right) txt(&InterS, 232, 131, right, C_DIM, 2);
}
void drawRate(int x, int y, uint32_t Bps, uint16_t col) {
  char n[12]; const char* u; fmtRate(Bps, n, sizeof(n), &u);
  txt(&InterMB, x, y, n, col);
  spr.setFreeFont(&InterMB);
  int w = spr.textWidth(n);
  txt(&InterS, x + w + 3, y, u, C_DIM);
}
void drawGraph(int gx, int gy, int gw, int gh) {
  uint32_t mx = 20000;
  for (int i = 0; i < HIST_N; i++) { mx = max(mx, histDn[i]); mx = max(mx, histUp[i]); }
  spr.drawFastHLine(gx, gy + gh, gw, C_LINE);
  int px = -1, py = 0;
  for (int i = 0; i < HIST_N; i++) {
    int k = (histPos + i) % HIST_N;
    int x = gx + i * 2;
    int h = (int)((uint64_t)histDn[k] * (gh - 2) / mx);
    if (histDn[k] && h < 1) h = 1;
    if (h) spr.fillRect(x, gy + gh - h, 2, h, C_AMB);
    int y = gy + gh - 1 - (int)((uint64_t)histUp[k] * (gh - 3) / mx);
    if (px >= 0) spr.drawLine(px, py, x + 1, y, C_TXT);
    px = x + 1; py = y;
  }
}

// --- halaman utama ---
void pageDash() {
  StatusInfo si = statusInfo();
  drawHeader(si.text, si.color, true);

  txt(&InterS, 8, 29, "Upstream", C_DIM);
  String ip = staOnline ? WiFi.localIP().toString() : String("-");
  txt(&InterS, 232, 29, ip.c_str(), C_DIM, 2);
  String ss = cfg.staSsid.length() ? cfg.staSsid : String("(belum dipilih)");
  txt(&InterMB, 8, 46, fit(ss, &InterMB, 224).c_str(), cfg.staSsid.length() ? C_TXT : C_DIM);
  spr.drawFastHLine(0, 52, 240, C_LINE);

  txt(&InterS, 8, 65, "Repeater", C_DIM);
  txt(&InterS, 142, 65, "Klien", C_DIM);
  txt(&InterS, 192, 65, "Kanal", C_DIM);
  txt(&InterMB, 8, 81, fit(cfg.apSsid, &InterMB, 126).c_str(), C_TXT);
  char b[8]; snprintf(b, sizeof(b), "%u", nClients);
  txt(&InterMB, 142, 81, b, nClients ? C_AMB : C_TXT);
  if (staOnline) snprintf(b, sizeof(b), "%d", WiFi.channel()); else strcpy(b, "-");
  txt(&InterMB, 192, 81, b, C_TXT);
  spr.drawFastHLine(0, 88, 240, C_LINE);

  txt(&InterS, 8, 102, "Unduh", C_DIM);
  drawRate(56, 102, rateDn, C_AMB);
  txt(&InterS, 8, 116, "Unggah", C_DIM);
  drawRate(56, 116, rateUp, C_TXT);
  drawGraph(120, 92, 112, 26);

  drawFooter(fmtBytes(totDn + totUp).c_str());
}
void pageClients() {
  char t[24]; snprintf(t, sizeof(t), "Klien (%u)", nClients);
  drawHeader(t, 0, true);
  if (nClients == 0) {
    txt(&InterMB, 120, 66, "Belum ada perangkat", C_TXT, 1);
    String s = "Sambung ke " + cfg.apSsid;
    txt(&InterS, 120, 84, fit(s, &InterS, 220).c_str(), C_DIM, 1);
  } else {
    if (clientScroll >= nClients) clientScroll = 0;
    for (int r = 0; r < 5; r++) {
      int i = clientScroll + r; if (i >= nClients) break;
      int y = 21 + r * 19;
      String ip = clients[i].ip ? IPAddress(clients[i].ip).toString() : String("...");
      txt(&InterMB, 8, y + 14, ip.c_str(), C_TXT);
      char m[12]; snprintf(m, sizeof(m), "%02x:%02x:%02x", clients[i].mac[3], clients[i].mac[4], clients[i].mac[5]);
      txt(&InterS, 128, y + 13, m, C_DIM);
      char rs[8]; snprintf(rs, sizeof(rs), "%d", clients[i].rssi);
      txt(&InterM, 232, y + 14, rs, C_TXT, 2);
      if (r < 4 && i + 1 < nClients) spr.drawFastHLine(8, y + 18, 224, C_LINE);
    }
    scrollbar(clientScroll, 5, nClients, 21, 95);
  }
  char r[16] = ""; if (nClients > 5) snprintf(r, sizeof(r), "atas: geser");
  drawFooter(r);
}
void pageSystem() {
  drawHeader("Sistem", 0, true);
  struct Row { const char* k; String v; } rows[6] = {
    {"Web UI",   WiFi.softAPIP().toString()},
    {"Sandi AP", cfg.apPass.length() ? cfg.apPass : String("terbuka")},
    {"IP STA",   staOnline ? WiFi.localIP().toString() : String("-")},
    {"Baterai",  batMv > 0 ? String(batMv / 1000.0, 2) + " V" : String("USB")},
    {"Trafik",   fmtBytes(totDn) + " / " + fmtBytes(totUp)},
    {"Memori",   String(ESP.getFreeHeap() / 1024) + " KB bebas"},
  };
  for (int i = 0; i < 6; i++) {
    int y = 32 + i * 16;
    txt(&InterS, 8, y, rows[i].k, C_DIM);
    txt(&InterM, 82, y + 1, fit(rows[i].v, &InterM, 150).c_str(), C_TXT);
  }
  drawFooter("v" FW_VERSION);
}

// --- menu ---
const char* menuLabel(int i) {
  static const char* L[M_COUNT] = {"Cari WiFi", "Kecerahan", "Redup otomatis", "Putar layar", "Nama AP", "Sandi AP", "Lupakan jaringan", "Mulai ulang", "Matikan", "Kembali"};
  return L[i];
}
String menuValue(int i) {
  switch (i) {
    case M_BRIGHT: return BR_NAME[cfg.bright];
    case M_DIM:    return DIM_NAME[cfg.dimIdx];
    case M_ROT:    return cfg.rot ? "Terbalik" : "Normal";
    case M_APSSID: return cfg.apSsid;
    case M_APPASS: { if (!cfg.apPass.length()) return "Terbuka"; String s; for (size_t k = 0; k < cfg.apPass.length() && k < 10; k++) s += '*'; return s; }
    case M_FORGET: return (millis() < menuConfirmUntil) ? "Yakin? klik lagi" : (cfg.staSsid.length() ? "" : "-");
  }
  return "";
}
void drawMenu() {
  drawHeader("Pengaturan", 0, false);
  char b[12]; snprintf(b, sizeof(b), "%u/%u", menuSel + 1, (unsigned)M_COUNT);
  txt(&InterS, 232, 12, b, C_DIM, 2);
  const int rows = 6, rh = 17, y0 = 20;
  int first = constrain((int)menuSel - 2, 0, (int)M_COUNT - rows);
  for (int r = 0; r < rows; r++) {
    int i = first + r; if (i >= M_COUNT) break;
    int y = y0 + r * rh; bool sel = (i == menuSel);
    if (sel) { spr.fillRect(4, y, 230, rh - 1, C_AMB_D); spr.fillRect(4, y, 2, rh - 1, C_AMB); }
    txt(&InterM, 12, y + 13, menuLabel(i), sel ? C_TXT : C_DIM);
    String v = menuValue(i);
    if (v.length()) txt(&InterM, 228, y + 13, fit(v, &InterM, 118).c_str(), sel ? C_AMB : C_DIM, 2);
  }
  scrollbar(first, rows, M_COUNT, y0, rows * rh - 1);
  txt(&InterS, 120, 131, "Atas: pilih     Bawah: lanjut", C_DIM, 1);
}

// --- pilih jaringan ---
void drawScan() {
  drawHeader("Pilih jaringan", 0, false);
  if (scanning) {
    String d = "Memindai"; for (int i = 0; i < (int)((millis() / 400) % 4); i++) d += '.';
    txt(&InterMB, 80, 72, d.c_str(), C_TXT);
    txt(&InterS, 120, 131, "Tahan bawah: batal", C_DIM, 1);
    return;
  }
  char b[12]; snprintf(b, sizeof(b), "%u jaringan", nNets);
  txt(&InterS, 232, 12, b, C_DIM, 2);
  const int rows = 5, rh = 19, y0 = 20; int total = nNets + 3;
  int first = constrain((int)scanSel - 2, 0, max(0, total - rows));
  for (int r = 0; r < rows; r++) {
    int i = first + r; if (i >= total) break;
    int y = y0 + r * rh; bool sel = (i == scanSel);
    if (sel) { spr.fillRect(4, y, 230, rh - 1, C_AMB_D); spr.fillRect(4, y, 2, rh - 1, C_AMB); }
    uint16_t c = sel ? C_TXT : C_DIM;
    if (i < nNets) {
      if (nets[i].sec) drawLock(12, y + 4, c);
      txt(&InterM, 24, y + 14, fit(nets[i].ssid, &InterM, 138).c_str(), c);
      drawBars(196, y + 14, nets[i].rssi, true);
      char rs[8]; snprintf(rs, sizeof(rs), "%d", nets[i].rssi);
      txt(&InterS, 232, y + 13, rs, C_DIM, 2);
    } else {
      const char* e[3] = {"Ketik nama WiFi", "Pindai ulang", "Kembali"};
      txt(&InterM, 12, y + 14, e[i - nNets], c);
    }
  }
  scrollbar(first, rows, total, y0, rows * rh - 1);
  txt(&InterS, 120, 131, "Atas: pilih     Bawah: lanjut", C_DIM, 1);
}

// --- keyboard dua tombol (pilih baris, lalu kolom) ---
int rowLen(int r) { return r < 4 ? (int)strlen(KEYROWS[keySet][r]) : 6; }
void openKeys(KeyTarget t, const char* title, const String& sub, const String& init, uint8_t maxLen) {
  keyTarget = t; keyTitle = title; keySub = sub; keyBuf = init; keyMax = maxLen;
  keyShow = (t == KT_AP_SSID || t == KT_STA_SSID);
  keySet = 0; keyRow = 0; keyCol = -1; screen = SC_KEYS;
}
void drawKeys() {
  txt(&InterMB, 8, 13, keyTitle.c_str(), C_TXT);
  spr.setFreeFont(&InterMB);
  int tw = spr.textWidth(keyTitle.c_str());
  if (keySub.length()) txt(&InterS, 8 + tw + 8, 12, fit(keySub, &InterS, 150 - tw).c_str(), C_DIM);
  char cnt[12]; snprintf(cnt, sizeof(cnt), "%u/%u", (unsigned)keyBuf.length(), (unsigned)keyMax);
  txt(&InterS, 232, 12, cnt, C_DIM, 2);
  spr.drawFastHLine(0, 17, 240, C_LINE);

  spr.drawRoundRect(6, 21, 228, 19, 3, C_LINE);
  bool caret = (millis() / 500) % 2 == 0; int cx = 12;
  if (keyShow) {
    String t = keyBuf; spr.setFreeFont(&InterM);
    while (t.length() && spr.textWidth(t) > 208) t.remove(0, 1);
    txt(&InterM, 12, 35, t.c_str(), C_TXT);
    cx = 12 + (t.length() ? spr.textWidth(t) + 1 : 0);
  } else {
    int n = min((int)keyBuf.length(), 23);
    for (int i = 0; i < n; i++) spr.fillCircle(15 + i * 9, 30, 2, C_TXT);
    cx = 12 + n * 9;
  }
  if (caret) spr.fillRect(cx, 25, 1, 11, C_AMB);

  for (int r = 0; r < 5; r++) {
    int y = 44 + r * 17; bool rs = (r == keyRow); int len = rowLen(r);
    if (rs) { spr.fillRect(4, y, 232, 16, keyCol < 0 ? C_AMB_D : C_LINE); spr.fillRect(4, y, 2, 16, C_AMB); }
    if (r < 4) {
      for (int c = 0; c < len; c++) {
        int x = 10 + c * 20; bool cs = rs && keyCol == c;
        if (cs) spr.fillRoundRect(x, y, 19, 16, 2, C_AMB);
        char s[2] = {KEYROWS[keySet][r][c], 0};
        txt(&InterMB, x + 9, y + 13, s, cs ? C_BG : (rs ? C_TXT : C_DIM), 1);
      }
      if (rs && keyCol >= 0) {
        int x = 10 + len * 20; bool cs = (keyCol == len);
        if (cs) spr.fillRoundRect(x, y, 19, 16, 2, C_AMB);
        txt(&InterMB, x + 9, y + 13, "<", cs ? C_BG : C_DIM, 1);
      }
    } else {
      const char* lab[6] = {SETNAME[keySet], "Spasi", "Hapus", keyShow ? "Tutup" : "Lihat", "OK", "Batal"};
      for (int c = 0; c < 6; c++) {
        int x = 8 + c * 35; bool cs = rs && keyCol == c;
        if (cs) spr.fillRoundRect(x, y, 34, 16, 2, C_AMB);
        txt(&InterS, x + 17, y + 12, lab[c], cs ? C_BG : (rs ? C_TXT : C_DIM), 1);
      }
      if (rs && keyCol >= 0) {
        int x = 8 + 6 * 35; bool cs = (keyCol == 6);
        if (cs) spr.fillRoundRect(x, y, 19, 16, 2, C_AMB);
        txt(&InterMB, x + 9, y + 13, "<", cs ? C_BG : C_DIM, 1);
      }
    }
  }
}

void drawToast() {
  spr.fillRect(0, 119, 240, 16, C_AMB);
  txt(&InterMB, 120, 131, toastMsg, C_BG, 1);
}
void render() {
  spr.fillSprite(C_BG);
  switch (screen) {
    case SC_MAIN: if (page == 0) pageDash(); else if (page == 1) pageClients(); else pageSystem(); break;
    case SC_MENU: drawMenu(); break;
    case SC_SCAN: drawScan(); break;
    case SC_KEYS: drawKeys(); break;
  }
  if (millis() < toastUntil) drawToast();
  spr.pushSprite(0, 0);
}

// ------------------------------ keyboard logika ----------------------
void keysCancel() {
  screen = (keyTarget == KT_AP_SSID || keyTarget == KT_AP_PASS) ? SC_MENU : SC_SCAN;
}
void keysSubmit() {
  switch (keyTarget) {
    case KT_STA_PASS: {
      if (keyBuf.length() == 0 && pendingSec) { toast("Sandi wajib diisi"); return; }
      if (keyBuf.length() > 0 && keyBuf.length() < 8) { toast("Sandi minimal 8 karakter"); return; }
      applySta(pendingSsid, keyBuf); screen = SC_MAIN; page = 0; toast("Menyambung...");
      break;
    }
    case KT_STA_SSID: {
      if (keyBuf.length() == 0) { toast("Nama WiFi kosong"); return; }
      pendingSsid = keyBuf; pendingSec = false;        // sandi boleh kosong (jaringan terbuka)
      openKeys(KT_STA_PASS, "Sandi", pendingSsid, "", 63);
      break;
    }
    case KT_AP_SSID: {
      if (keyBuf.length() == 0) { toast("Nama AP kosong"); return; }
      cfg.apSsid = keyBuf; saveCfg(); apRestartAt = millis() + 300; screen = SC_MENU; toast("Nama AP disimpan");
      break;
    }
    case KT_AP_PASS: {
      if (keyBuf.length() > 0 && keyBuf.length() < 8) { toast("Sandi minimal 8 karakter"); return; }
      cfg.apPass = keyBuf; saveCfg(); apRestartAt = millis() + 300; screen = SC_MENU;
      toast(keyBuf.length() ? "Sandi AP disimpan" : "AP kini terbuka");
      break;
    }
  }
}
void keysBotClick() {
  if (keyCol < 0) keyRow = (keyRow + 1) % 5;
  else { keyCol++; if (keyCol > rowLen(keyRow)) keyCol = 0; }
}
void keysBotLong() {
  if (keyCol >= 0) keyCol = -1;                        // kembali ke pilih baris
  else if (keyBuf.length()) keyBuf.remove(keyBuf.length() - 1);   // hapus karakter
}
void keysTop() {
  if (keyCol < 0) { keyCol = 0; return; }
  int len = rowLen(keyRow);
  if (keyCol == len) { keyCol = -1; return; }
  if (keyRow < 4) {
    if (keyBuf.length() < keyMax) keyBuf += KEYROWS[keySet][keyRow][keyCol];
    keyCol = -1; return;
  }
  int c = keyCol; keyCol = -1;
  switch (c) {
    case 0: keySet = (keySet + 1) % 3; break;
    case 1: if (keyBuf.length() < keyMax) keyBuf += ' '; break;
    case 2: if (keyBuf.length()) keyBuf.remove(keyBuf.length() - 1); break;
    case 3: keyShow = !keyShow; break;
    case 4: keysSubmit(); break;
    case 5: keysCancel(); break;
  }
}

// ------------------------------ input per layar ----------------------
void menuAction(int i) {
  switch (i) {
    case M_SCAN:   screen = SC_SCAN; scanSel = 0; startScan(); break;
    case M_BRIGHT: cfg.bright = (cfg.bright + 1) % 5; saveCfg(); applyBrightness(); break;
    case M_DIM:    cfg.dimIdx = (cfg.dimIdx + 1) % 5; saveCfg(); break;
    case M_ROT:    cfg.rot ^= 1; saveCfg(); tft.setRotation(rotation()); break;
    case M_APSSID: openKeys(KT_AP_SSID, "Nama AP", "", cfg.apSsid, 32); break;
    case M_APPASS: openKeys(KT_AP_PASS, "Sandi AP", "kosong = terbuka", cfg.apPass, 63); break;
    case M_FORGET:
      if (millis() < menuConfirmUntil) { forgetSta(); menuConfirmUntil = 0; toast("Jaringan dilupakan"); }
      else menuConfirmUntil = millis() + 3000;
      break;
    case M_REBOOT: ESP.restart(); break;
    case M_OFF:    powerOff(); break;
    case M_BACK:   screen = SC_MAIN; break;
  }
}
void onTopClick() {
  switch (screen) {
    case SC_MAIN:
      if (page == 1 && nClients > 5) { clientScroll += 5; if (clientScroll >= nClients) clientScroll = 0; }
      break;
    case SC_MENU: menuAction(menuSel); break;
    case SC_SCAN: {
      if (scanning) break;
      if (scanSel < nNets) {
        pendingSsid = nets[scanSel].ssid; pendingSec = nets[scanSel].sec;
        if (pendingSec) openKeys(KT_STA_PASS, "Sandi", pendingSsid, "", 63);
        else { applySta(pendingSsid, ""); screen = SC_MAIN; page = 0; toast("Menyambung..."); }
      } else {
        int k = scanSel - nNets;
        if (k == 0) openKeys(KT_STA_SSID, "Nama WiFi", "", "", 32);
        else if (k == 1) { startScan(); scanSel = 0; }
        else screen = SC_MENU;
      }
      break;
    }
    case SC_KEYS: keysTop(); break;
  }
}
void onBotClick() {
  switch (screen) {
    case SC_MAIN: page = (page + 1) % 3; clientScroll = 0; break;
    case SC_MENU: menuSel = (menuSel + 1) % M_COUNT; menuConfirmUntil = 0; break;
    case SC_SCAN: if (!scanning) scanSel = (scanSel + 1) % (nNets + 3); break;
    case SC_KEYS: keysBotClick(); break;
  }
}
void onBotLong() {
  switch (screen) {
    case SC_MAIN: screen = SC_MENU; menuSel = 0; menuConfirmUntil = 0; break;
    case SC_MENU: screen = SC_MAIN; break;
    case SC_SCAN: screen = SC_MENU; break;
    case SC_KEYS: keysBotLong(); break;
  }
}
void handleInput(uint32_t now) {
  bool wasDim = dimmed;
  if (top.ePress || bot.ePress || bot.eLong) {
    lastActivity = now; dirty = true;
    if (dimmed) { dimmed = false; applyBrightness(); }
  }
  if (bot.ePress) botSwallow = wasDim;                 // penekanan pembangun layar tidak beraksi
  if (top.ePress) {
    bool dbl = haveTopPrev && (now - topPrev < DBL_MS);   // dobel klik: terdeteksi saat penekanan kedua
    topPrev = now; haveTopPrev = true;
    if (dbl && screen != SC_KEYS) { haveTopPrev = false; powerOff(); }
    else if (!wasDim) onTopClick();                    // klik pertama tetap langsung beraksi
  }
  if (bot.eLong && !botSwallow) onBotLong();
  if (bot.eClick && !botSwallow) onBotClick();
}

// ------------------------------ web server ---------------------------
bool guard() {                                          // web UI hanya lewat hotspot repeater
  if (server.client().localIP() == WiFi.softAPIP()) return true;
  server.send(403, "text/plain", "Buka lewat WiFi repeater");
  return false;
}
void jsend(bool ok, const String& msg) {
  String j = "{\"ok\":"; j += (ok ? "true" : "false");
  j += ",\"msg\":\""; j += jesc(msg); j += "\"}";
  server.send(200, "application/json", j);
}
void handleRoot() {
  if (!guard()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", WEBUI_HTML);
}
void handleStatus() {
  if (!guard()) return;
  String j; j.reserve(2600);
  j += F("{\"fw\":\""); j += FW_VERSION;
  j += F("\",\"up\":"); j += (unsigned long)(millis() / 1000);
  j += F(",\"heap\":"); j += (unsigned long)ESP.getFreeHeap();
  j += F(",\"bat\":"); j += batMv;
  j += F(",\"sta\":{\"ssid\":\""); j += jesc(cfg.staSsid);
  j += F("\",\"state\":\""); j += staStateName();
  j += F("\",\"rssi\":"); j += (staOnline ? WiFi.RSSI() : 0);
  j += F(",\"ch\":"); j += (staOnline ? (int)WiFi.channel() : 0);
  j += F(",\"ip\":\""); j += (staOnline ? WiFi.localIP().toString() : String(""));
  j += F("\",\"reason\":\""); j += reasonText(); j += F("\"}");
  j += F(",\"ap\":{\"ssid\":\""); j += jesc(cfg.apSsid);
  j += F("\",\"open\":"); j += (cfg.apPass.length() ? "false" : "true");
  j += F(",\"ip\":\""); j += WiFi.softAPIP().toString();
  j += F("\",\"clients\":[");
  for (int i = 0; i < nClients; i++) {
    if (i) j += ',';
    j += F("{\"ip\":\""); j += (clients[i].ip ? IPAddress(clients[i].ip).toString() : String(""));
    j += F("\",\"mac\":\""); j += macStr(clients[i].mac);
    j += F("\",\"rssi\":"); j += (int)clients[i].rssi; j += '}';
  }
  j += F("]},\"net\":{\"dn\":"); j += (unsigned long)rateDn;
  j += F(",\"up\":"); j += (unsigned long)rateUp;
  j += F(",\"dnT\":"); j += (unsigned long)(totDn / 1024);
  j += F(",\"upT\":"); j += (unsigned long)(totUp / 1024);
  j += F(",\"hd\":[");
  for (int i = 0; i < HIST_N; i++) { if (i) j += ','; j += (unsigned long)histDn[(histPos + i) % HIST_N]; }
  j += F("],\"hu\":[");
  for (int i = 0; i < HIST_N; i++) { if (i) j += ','; j += (unsigned long)histUp[(histPos + i) % HIST_N]; }
  j += F("]},\"cfg\":{\"br\":"); j += cfg.bright;
  j += F(",\"dim\":"); j += cfg.dimIdx;
  j += F(",\"rot\":"); j += cfg.rot; j += F("}}");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}
void handleScan() {
  if (!guard()) return;
  if (server.hasArg("start")) startScan();
  String j = "{\"scanning\":"; j += (scanning ? "true" : "false"); j += ",\"list\":[";
  for (int i = 0; i < nNets; i++) {
    if (i) j += ',';
    j += F("{\"ssid\":\""); j += jesc(nets[i].ssid);
    j += F("\",\"rssi\":"); j += (int)nets[i].rssi;
    j += F(",\"ch\":"); j += (int)nets[i].ch;
    j += F(",\"sec\":"); j += (nets[i].sec ? "true" : "false"); j += '}';
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}
void apiSta() {
  if (!guard()) return;
  String s = server.arg("ssid"), p = server.arg("pass"); s.trim();
  if (s.length() < 1 || s.length() > 32) { jsend(false, "Nama WiFi tidak valid"); return; }
  if (p.length() && (p.length() < 8 || p.length() > 63)) { jsend(false, "Sandi harus 8 sampai 63 karakter"); return; }
  jsend(true, "Menyambung ke " + s);
  applySta(s, p); dirty = true;
}
void apiAp() {
  if (!guard()) return;
  String s = server.arg("ssid"), p = server.arg("pass"); s.trim();
  bool open = server.arg("open") == "1";
  if (s.length() < 1 || s.length() > 32) { jsend(false, "Nama hotspot tidak valid"); return; }
  if (!open && p.length() && (p.length() < 8 || p.length() > 63)) { jsend(false, "Sandi harus 8 sampai 63 karakter"); return; }
  String newPass = open ? String("") : (p.length() ? p : cfg.apPass);
  if (!open && newPass.isEmpty()) { jsend(false, "Isi sandi atau centang tanpa sandi"); return; }
  cfg.apSsid = s; cfg.apPass = newPass; saveCfg();
  jsend(true, "Hotspot diperbarui. Sambungkan ulang ke " + s);
  apRestartAt = millis() + 800; dirty = true;
}
void apiDisp() {
  if (!guard()) return;
  cfg.bright = constrain(server.arg("br").toInt(), 0, 4);
  cfg.dimIdx = constrain(server.arg("dim").toInt(), 0, 4);
  uint8_t rot = server.arg("rot").toInt() ? 1 : 0;
  bool rotChanged = (rot != cfg.rot);
  cfg.rot = rot; saveCfg();
  if (rotChanged) tft.setRotation(rotation());
  lastActivity = millis(); dimmed = false; applyBrightness(); dirty = true;
  jsend(true, "Tampilan diperbarui");
}
void apiForget() { if (!guard()) return; forgetSta(); jsend(true, "Jaringan sumber dilupakan"); dirty = true; }
void apiReboot() { if (!guard()) return; jsend(true, "Memulai ulang..."); rebootAt = millis() + 600; }

// ------------------------------ setup & loop -------------------------
void splash() {
  spr.fillSprite(C_BG);
  txt(&InterL, 14, 66, "Repeater", C_TXT);
  spr.fillRect(14, 74, 28, 2, C_AMB);
  txt(&InterS, 14, 92, "TTGO T-Display  v" FW_VERSION, C_DIM);
  spr.pushSprite(0, 0);
}
void setup() {
  gpio_deep_sleep_hold_dis();                          // lepas hold backlight setelah deep sleep
  gpio_hold_dis((gpio_num_t)PIN_BL);
  Serial.begin(115200);
  loadCfg();

  tft.init();
  tft.setRotation(rotation());
  tft.fillScreen(C_BG);
  spr.setColorDepth(16);
  if (!spr.createSprite(240, 135)) Serial.println("sprite gagal dibuat");
  spr.setTextWrap(false, false);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BL, 0);
  ledcWrite(0, 0);
  splash();
  ledcWrite(0, BR_DUTY[cfg.bright]);

  top.begin(PIN_BTN_TOP, false);                       // GPIO35: pull-up eksternal di board
  bot.begin(PIN_BTN_BOT, true);
  readBattery();

  WiFi.persistent(false);
  WiFi.setHostname("ttgo-repeater");
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWiFiEvent);
  installHooks();
  applyApIp(cfg.apNet);
  startAp();
  if (cfg.staSsid.length()) connectSta();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/sta", HTTP_POST, apiSta);
  server.on("/api/ap", HTTP_POST, apiAp);
  server.on("/api/disp", HTTP_POST, apiDisp);
  server.on("/api/forget", HTTP_POST, apiForget);
  server.on("/api/reboot", HTTP_POST, apiReboot);
  server.onNotFound([]() { server.send(404, "text/plain", "Tidak ada"); });
  server.begin();

  delay(600);
  lastActivity = millis();
  if (cfg.staSsid.isEmpty()) { screen = SC_SCAN; startScan(); }
}

void loop() {
  static uint32_t lastSec = 0, lastBat = 0, lastDraw = 0;
  uint32_t now = millis();

  server.handleClient();
  pollScan();
  top.update(now); bot.update(now);
  handleInput(now);

  if (gotIp) { gotIp = false; installHooks(); natSetup(); dirty = true; }
  if (apRestartAt && (int32_t)(now - apRestartAt) >= 0) { apRestartAt = 0; startAp(); }
  if (rebootAt && (int32_t)(now - rebootAt) >= 0) ESP.restart();

  if (now - lastSec >= 1000) {
    lastSec = now;
    if (!origIn) installHooks();
    sampleTraffic();
    refreshClients();
  }
  if (now - lastBat >= 15000) { lastBat = now; readBattery(); }

  if (!dimmed && cfg.dimIdx && now - lastActivity > DIM_SEC[cfg.dimIdx] * 1000UL) { dimmed = true; applyBrightness(); }

  if (dirty || now - lastDraw >= 200) { lastDraw = now; dirty = false; render(); }
  delay(2);
}
