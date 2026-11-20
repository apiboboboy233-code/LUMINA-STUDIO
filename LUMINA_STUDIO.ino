#include <ArduinoJson.h>
static_assert(ARDUINOJSON_VERSION_MAJOR == 7 && ARDUINOJSON_VERSION_MINOR == 4 && ARDUINOJSON_VERSION_REVISION == 2, "Use ArduinoJson 7.4.2");
#include <Arduino.h>
#include <esp32-hal-rmt.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <atomic>
#include <cstring>
#include <cmath>
#include "lumina_build_info.h"
#include "lumina_html.h"

#if !defined(CONFIG_IDF_TARGET_ESP32C3)
#error "LUMINA STUDIO requires ESP32-C3"
#endif

namespace {
constexpr char ssid[] = "LUMINA-STUDIO";
char password[25]{};

bool loadPassword() {
  if (*password) return true;
  Preferences prefs;
  if (!prefs.begin("lumina-net", false)) return false;
  if (prefs.getString("ap-key", password, sizeof(password)) == sizeof(password)) return true;
  // Wi-Fi is already enabled, so the hardware RNG has an entropy source.
  snprintf(password, sizeof(password), "%08lx%08lx%08lx",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  if (prefs.putString("ap-key", password) == 24) return true;
  password[0] = '\0';
  return false;
}
constexpr unsigned channel = 6;
const IPAddress apIp(192, 168, 4, 1);
const IPAddress mask(255, 255, 255, 0);
std::atomic<bool> apUp{false};
httpd_handle_t server = nullptr;

constexpr unsigned maxPixels = 150;
constexpr unsigned ceiling = 128;
constexpr const char *orders[] = {"RGB", "GRB", "BRG", "RBG", "GBR", "BGR"};
struct RGB { uint8_t r, g, b; };
struct Lighting {
  bool power = false;
  unsigned brightness = 64, count = 60, order = 1, limit = 64;
  RGB color{89,225,255};
};
Lighting light;
constexpr const char *tests[] = {"off", "red", "green", "blue", "white", "first"};
unsigned testMode = 0, lastLength = 0;
int64_t testUntil = 0, saveAt = 0, sentAt = 0;
bool ready = false, dirtyFrame = true, transmitting = false, saveFailed = false;
const char *ledError = "Not initialized";
RGB completedPixels[maxPixels], pixels[maxPixels], output{}, pendingOutput{};
unsigned outputTest = 0, pendingTest = 0;
rmt_data_t symbols[maxPixels * 24 + 1];
SemaphoreHandle_t ledMutex;
std::atomic<bool> browserSeen{false}, edited{false};

int lookup(const char *value, const char *const *items, unsigned count) {
  for (unsigned i=0; i<count; ++i) if (!strcmp(value, items[i])) return int(i);
  return -1;
}

#include "lumina_color_engine.h"
#include "lumina_effects.h"

constexpr unsigned minEffectFps = 1, maxEffectFps = 60, maxEffectTransition = 10000;
struct EffectSettings {
  uint8_t version = 1, mode = Fx::Solid, fps = 30, reserved = 0;
  uint16_t transitionMs = 0;
  Fx::Params parameters{};
  uint32_t favorites = 0;
};
static_assert(Fx::effectCount < 32, "Effect favorites must fit in a bitmask");
EffectSettings effectSettings;
Fx::Engine effectEngine;
int64_t effectSaveAt = 0;
RGB renderedRaw[maxPixels]{}, completedRaw[maxPixels]{};
unsigned completedRawCount = 0, pendingRawCount = 0;

unsigned activeEffect() {
  if (studio.color.gradientMode) return Fx::Gradient;
  return Fx::isAnimated(effectSettings.mode) ? effectSettings.mode : unsigned(Fx::Solid);
}
bool effectRunning() { return ready && light.power && !testMode && Fx::isAnimated(activeEffect()); }
void markEffect() { effectSaveAt = esp_timer_get_time() + 5000000; }
void selectStudioGradient() {
  if (effectSettings.mode != Fx::Solid) {
    effectSettings.mode = Fx::Solid;
    effectSettings.parameters = {};
    effectEngine.reset();
    markEffect();
  }
}
void loadEffect() {
  Preferences prefs;
  EffectSettings saved;
  if (!prefs.begin("lumina-fx", true) || prefs.getBytesLength("settings") != sizeof(saved) ||
      prefs.getBytes("settings", &saved, sizeof(saved)) != sizeof(saved) || saved.version != 1 ||
      saved.mode >= Fx::effectCount || saved.mode == Fx::Gradient || saved.fps < minEffectFps ||
      saved.fps > maxEffectFps || saved.transitionMs > maxEffectTransition) return;
  saved.favorites &= (1UL << Fx::effectCount) - 1;
  Fx::clampParams(saved.mode, saved.parameters);
  effectSettings = saved;
}
void persistEffect(int64_t now) {
  if (!effectSaveAt || now < effectSaveAt || transmitting) return;
  effectSaveAt = 0;
  Preferences prefs;
  if (!prefs.begin("lumina-fx", false) ||
      prefs.putBytes("settings", &effectSettings, sizeof(effectSettings)) != sizeof(effectSettings))
    Serial.println("Effect settings save failed");
}

RGB effectPixel(unsigned i, float progress) {
  const RGB target = effectEngine.render(i, light.count, light.color, studio.color.secondary);
  if (progress >= 1 || i >= transitionSourceCount) return target;
  const RGB source = transitionSource[i];
  switch (runningTransition.curve) {
    case 1: progress *= progress; break;
    case 2: progress *= 2 - progress; break;
    case 3: progress = progress < .5f ? 2*progress*progress : 1-2*(1-progress)*(1-progress); break;
  }
  switch (runningTransition.style) {
    case 1: return progress < .5f ? mixRgb(source, RGB{}, 2*progress) : mixRgb(RGB{}, target, 2*progress-1);
    case 2: return mixRgb(source, target, progress*progress*(3-2*progress));
    default: return mixRgb(source, target, progress);
  }
}

bool validConfig(unsigned count, unsigned order, unsigned limit) {
  return count >= 1 && count <= maxPixels && order < 6 && limit >= 1 && limit <= ceiling;
}

bool saveLighting() {
  const uint8_t record[] = {1, uint8_t(light.count), uint8_t(light.order), uint8_t(light.limit),
                            uint8_t(light.brightness), light.color.r, light.color.g, light.color.b};
  Preferences prefs;
  return prefs.begin("lumina-led", false) && prefs.putBytes("settings", record, sizeof(record)) == sizeof(record);
}

void initLighting() {
  const int order = lookup(LUMINA_LED_COLOR_ORDER, orders, 6);
  ledMutex = xSemaphoreCreateMutex();
  // Board profile excludes USB, straps, UART0, module flash and onboard RGB.
  if (!ledMutex || (LUMINA_LED_GPIO != 3 && LUMINA_LED_GPIO != 4 && LUMINA_LED_GPIO != 5 && LUMINA_LED_GPIO != 6 && LUMINA_LED_GPIO != 7 && LUMINA_LED_GPIO != 10) ||
      !validConfig(LUMINA_LED_COUNT, unsigned(order), LUMINA_LED_MAX_BRIGHTNESS)) {
    ledError = "Invalid board profile or mutex allocation failed";
    return;
  }
  light.count = LUMINA_LED_COUNT; light.order = unsigned(order); light.limit = LUMINA_LED_MAX_BRIGHTNESS;
  Preferences prefs;
  uint8_t record[8];
  if (prefs.begin("lumina-led", true) && prefs.getBytesLength("settings") == sizeof(record) &&
      prefs.getBytes("settings", record, sizeof(record)) == sizeof(record) && record[0] == 1 &&
      validConfig(record[1], record[2], record[3])) {
    light.count = record[1]; light.order = record[2]; light.limit = record[3];
    light.brightness = record[4]; light.color = {record[5], record[6], record[7]};
  }
  loadStudio();
  loadEffect();
  if (Fx::isAnimated(effectSettings.mode)) studio.color.gradientMode = false;
  if (!rmtInit(LUMINA_LED_GPIO, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, 20000000)) {
    ledError = "RMT initialization failed";
    return;
  }
  if (!rmtSetEOT(LUMINA_LED_GPIO, 0)) {
    rmtDeinit(LUMINA_LED_GPIO);
    ledError = "RMT idle configuration failed";
    return;
  }
  ready = true;
  lastLength = maxPixels; // Clear the entire supported chain at boot, including any previously configured tail.
  ledError = "";
}

RGB scaled(RGB color, unsigned level) {
  return {uint8_t(unsigned(color.r)*level/255), uint8_t(unsigned(color.g)*level/255), uint8_t(unsigned(color.b)*level/255)};
}

void renderLighting(int64_t now) {
  if (!ready) return;
  if (transmitting) {
    if (!rmtTransmitCompleted(LUMINA_LED_GPIO)) {
      if (now - sentAt > 50000) { ready = false; transmitting = false; ledError = "RMT completion timed out"; }
      return;
    }
    transmitting = false; memcpy(completedPixels,pixels,sizeof(pixels)); output = pendingOutput; outputTest = pendingTest;
    memcpy(completedRaw, renderedRaw, sizeof(completedRaw)); completedRawCount = pendingRawCount;
  }
  if (testMode && now >= testUntil) { testMode = 0; dirtyFrame = true; }
  const bool animate = effectRunning();
  const unsigned interval = animate ? (1000000U + effectSettings.fps - 1)/effectSettings.fps : 16000;
  if ((!dirtyFrame && !animate && !transitionAt) || now - sentAt < interval) return;
  const float progress=transitionProgress(now);
  if (progress >= 1) transitionAt = 0;
  if (animate) effectEngine.update(activeEffect(), effectSettings.parameters, uint32_t(now / 1000));
  const unsigned count = light.count > lastLength ? light.count : lastLength;
  const RGB diagnostics[] = {{0,0,0},{255,0,0},{0,255,0},{0,0,255},{255,255,255},{255,255,255}};
  const unsigned level = testMode ? (light.limit < 24 ? light.limit : 24) :
                                  (light.brightness < light.limit ? light.brightness : light.limit);
  for (unsigned i=0; i<count; ++i) {
    RGB raw{};
    if (i < light.count) {
      if (testMode) raw = testMode != 5 || i == 0 ? diagnostics[testMode] : RGB{};
      else if (light.power) raw = animate ? effectPixel(i, progress) : studioPixel(i, light.count, progress);
    }
    renderedRaw[i] = raw;
    pixels[i] = scaled(raw, level);
  }
  unsigned n = 0;
  for (unsigned i=0; i<count; ++i) {
    const RGB p = pixels[i];
    for (unsigned c=0; c<3; ++c) {
      const char channel = orders[light.order][c];
      const uint8_t byte = channel == 'R' ? p.r : channel == 'G' ? p.g : p.b;
      for (unsigned bit=128; bit; bit >>= 1) {
        const bool one = byte & bit;
        auto &s = symbols[n++];
        s.level0 = 1; s.duration0 = one ? 16 : 8;
        s.level1 = 0; s.duration1 = one ? 9 : 17;
      }
    }
  }
  auto &reset = symbols[n++];
  reset.level0 = reset.level1 = 0; reset.duration0 = reset.duration1 = 3200;
  if (!rmtWriteAsync(LUMINA_LED_GPIO, symbols, n)) { ready = false; ledError = "RMT submission failed"; return; }
  pendingOutput = pixels[0]; pendingTest = testMode; pendingRawCount = count;
  lastLength = light.count; sentAt = now; transmitting = true; dirtyFrame = false;
}

void lightingTick() {
  if (!ledMutex || xSemaphoreTake(ledMutex, 0) != pdTRUE) return;
  const int64_t now = esp_timer_get_time();
  renderLighting(now);
  if (saveAt && now >= saveAt && !transmitting && !dirtyFrame) {
    saveAt = 0;
    saveFailed = !saveLighting();
  }
  persistStudio(now);
  persistEffect(now);
  xSemaphoreGive(ledMutex);
}

void diagnostic(Print &out, const char *state) {
  out.printf("\n=== LUMINA STUDIO ===\nFirmware %s | build %s | %s | %s UTC\n"
             "%s rev %u | CPU %u MHz | flash %lu bytes | free heap %lu bytes | reset reason %d\n"
             "AP: %s | SSID: %s | IP: %s | HTTP: %s\n",
             LUMINA_VERSION, LUMINA_BUILD, LUMINA_COMMIT, LUMINA_DATE,
             ESP.getChipModel(), unsigned(ESP.getChipRevision()), unsigned(ESP.getCpuFreqMHz()),
             static_cast<unsigned long>(ESP.getFlashChipSize()), static_cast<unsigned long>(ESP.getFreeHeap()),
             int(esp_reset_reason()), state, ssid, WiFi.softAPIP().toString().c_str(), server ? "ready" : "not ready");
}

void diagnostics(const char *state) {
  diagnostic(Serial, state);
#if ARDUINO_USB_CDC_ON_BOOT
  diagnostic(Serial0, state);
#endif
}

esp_err_t json(httpd_req_t *r, const char *status, const char *body) {
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "application/json; charset=utf-8");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
  return httpd_resp_sendstr(r, body);
}

esp_err_t failure(httpd_req_t *r, const char *status, const char *message) {
  char body[160];
  snprintf(body, sizeof(body), "{\"success\":false,\"error\":\"%s\"}", message);
  esp_err_t result = json(r, status, body);
  return result == ESP_OK ? ESP_FAIL : result; // Close requests with unread or invalid bodies.
}

#define DATA "{\"success\":true,\"data\":{"
#define END "}}"
const char *boolean(bool value) { return value ? "true" : "false"; }
const char *mode() { return apUp.load() ? "AP" : "OFF"; }

esp_err_t root(httpd_req_t *r) {
  browserSeen = true;
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, reinterpret_cast<const char *>(LUMINA_HTML), sizeof(LUMINA_HTML) - 1);
}

esp_err_t status(httpd_req_t *r) {
  char body[320];
  snprintf(body, sizeof(body), DATA "\"device\":\"LUMINA STUDIO\",\"status\":\"%s\","
           "\"firmware\":\"%s\",\"build\":\"%s\",\"mode\":\"%s\",\"ap\":%s,\"ip\":\"%s\","
           "\"uptime\":%llu,\"clients\":%u" END,
           apUp.load() ? "ready" : "recovering", LUMINA_VERSION, LUMINA_BUILD, mode(), boolean(apUp.load()),
           WiFi.softAPIP().toString().c_str(), static_cast<unsigned long long>(esp_timer_get_time() / 1000000),
           unsigned(WiFi.softAPgetStationNum()));
  return json(r, "200 OK", body);
}

esp_err_t systemInfo(httpd_req_t *r) {
  char body[256];
  snprintf(body, sizeof(body), DATA "\"chip\":\"%s\",\"revision\":%u,\"cpu\":%u,"
           "\"heap\":%lu,\"min_heap\":%lu,\"flash\":%lu,\"flash_hz\":%lu,\"reset\":%d" END,
           ESP.getChipModel(), unsigned(ESP.getChipRevision()), unsigned(ESP.getCpuFreqMHz()),
           static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
           static_cast<unsigned long>(ESP.getFlashChipSize()), static_cast<unsigned long>(ESP.getFlashChipSpeed()), int(esp_reset_reason()));
  return json(r, "200 OK", body);
}

esp_err_t network(httpd_req_t *r) {
  char body[256];
  uint8_t mac[6]{};
  WiFi.softAPmacAddress(mac);
  snprintf(body, sizeof(body), DATA "\"ssid\":\"%s\",\"ip\":\"%s\",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
           "\"mode\":\"%s\",\"ap\":%s,\"clients\":%u,\"channel\":%u" END,
           ssid, WiFi.softAPIP().toString().c_str(), unsigned(mac[0]), unsigned(mac[1]), unsigned(mac[2]),
           unsigned(mac[3]), unsigned(mac[4]), unsigned(mac[5]), mode(), boolean(apUp.load()),
           unsigned(WiFi.softAPgetStationNum()), unsigned(WiFi.channel()));
  return json(r, "200 OK", body);
}

esp_err_t health(httpd_req_t *r) {
  if (!apUp.load()) return failure(r, "503 Service Unavailable", "Access point recovering");
  return json(r, "200 OK", DATA "\"status\":\"ok\",\"device\":\"LUMINA STUDIO\"" END);
}

esp_err_t events(httpd_req_t *r) {
  char body[240];
  snprintf(body, sizeof(body), DATA "\"events\":[\"Device started\",\"Wi-Fi AP initialized\",\"Web server started\"%s%s]" END,
           browserSeen.load() ? ",\"Browser requested the interface\"" : "",
           edited.load() ? ",\"Lighting state updated\"" : "");
  return json(r, "200 OK", body);
}

enum Action {Legacy, Power, Color, Brightness, Config, Test};
Action actions[]={Power,Color,Brightness,Config,Test};
esp_err_t ledReply(httpd_req_t *r) {
  char body[640];
  snprintf(body, sizeof(body), DATA "\"power\":%s,\"brightness\":%u,\"brightness_percent\":%u,"
    "\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"effect\":\"%s\",\"led_count\":%u,\"data_pin\":%d,"
    "\"color_order\":\"%s\",\"max_brightness\":%u,\"hardware_ready\":%s,\"rendering\":%s,"
    "\"output\":{\"r\":%u,\"g\":%u,\"b\":%u},\"test\":\"%s\",\"output_test\":\"%s\","
    "\"hardware_error\":\"%s\",\"save_failed\":%s" END,
    boolean(light.power),light.brightness,(light.brightness*100+127)/255,
    unsigned(light.color.r),unsigned(light.color.g),unsigned(light.color.b),Fx::metadata[activeEffect()].id,light.count,LUMINA_LED_GPIO,
    orders[light.order],light.limit,boolean(ready),boolean(ready&&(dirtyFrame||transmitting)),
    unsigned(output.r),unsigned(output.g),unsigned(output.b),tests[testMode],tests[outputTest],ledError,boolean(saveFailed));
  xSemaphoreGive(ledMutex);
  return json(r,"200 OK",body);
}

esp_err_t getLed(httpd_req_t *r) {
  if (!ledMutex || xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100)) != pdTRUE)
    return failure(r,"503 Service Unavailable","LED engine unavailable or busy");
  return ledReply(r);
}

// Bounded flat JSON: fixed keys, no duplicates, raw ASCII strings, unsigned decimal or boolean.
void skipSpace(const char *&p) { while (*p && strchr(" \t\r\n",*p)) ++p; }
bool take(const char *&p,char c) { skipSpace(p); if (*p!=c) return false; ++p; return true; }
bool readJsonString(const char *&p,char *out,size_t cap) {
  if (!take(p,'"')) return false;
  size_t n=0;
  while (*p && *p!='"') {
    if (n+1>=cap || *p<32 || *p=='\\') return false;
    out[n++]=*p++;
  }
  out[n]=0;
  return *p && *p++=='"';
}
bool number(const char *&p,unsigned &v) {
  skipSpace(p);
  if (*p<'0'||*p>'9'||(*p=='0'&&p[1]>='0'&&p[1]<='9')) return false;
  v=0;
  while (*p>='0'&&*p<='9') { v=v*10+unsigned(*p++-'0'); if(v>255)return false; }
  return true;
}
struct Field { const char *key; char type; unsigned n; char text[12]; };
bool parseFields(const char *p,Field *fields,unsigned count) {
  if (!take(p,'{')) return false;
  unsigned seen=0;
  for(unsigned k=0;k<count;++k) {
    char key[20];
    if ((k&&!take(p,','))||!readJsonString(p,key,sizeof(key))||!take(p,':')) return false;
    unsigned i=0; while(i<count && strcmp(key,fields[i].key)) ++i;
    if(i==count||(seen&(1U<<i))) return false;
    seen|=1U<<i;
    auto &f=fields[i];
    if(f.type=='n') { if(!number(p,f.n)) return false; }
    else if(f.type=='s') { if(!readJsonString(p,f.text,sizeof(f.text)))return false; }
    else {
      skipSpace(p);
      if(!strncmp(p,"true",4)){p+=4;f.n=1;}
      else if(!strncmp(p,"false",5)){p+=5;f.n=0;}
      else return false;
    }
  }
  if(!take(p,'}'))return false;
  skipSpace(p); return !*p;
}

esp_err_t putLed(httpd_req_t *r) {
  char body[257], type[48], host[24];
  if (httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host)) != ESP_OK ||
      (strcmp(host,"192.168.4.1") && strcmp(host,"192.168.4.1:80")))
    return failure(r,"403 Forbidden","Use the controller IP address");
  if (r->content_len >= sizeof(body)) return failure(r,"413 Content Too Large","Body exceeds 256 bytes");
  if (httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type)) != ESP_OK ||
      (strcmp(type,"application/json") && strcmp(type,"application/json; charset=utf-8")))
    return failure(r,"415 Unsupported Media Type","Use application/json");
  size_t received=0;
  const int64_t deadline=esp_timer_get_time()+1500000;
  while(received<r->content_len) {
    if(esp_timer_get_time()>=deadline) return failure(r,"408 Request Timeout","Body read timed out");
    const int n=httpd_req_recv(r,body+received,r->content_len-received);
    if(n<=0)return failure(r,"408 Request Timeout","Incomplete body");
    received+=size_t(n);
  }
  body[received]=0;
  const Action action = r->user_ctx ? *static_cast<const Action *>(r->user_ctx) : Legacy;
  Field f[3]{};
  unsigned count=1;
  switch(action) {
    case Legacy: f[0]={"power",'b'}; f[1]={"brightness",'n'}; f[2]={"color",'s'}; count=3; break;
    case Power: f[0]={"power",'b'}; break;
    case Color: f[0]={"r",'n'}; f[1]={"g",'n'}; f[2]={"b",'n'}; count=3; break;
    case Brightness: f[0]={"brightness",'n'}; break;
    case Config: f[0]={"led_count",'n'}; f[1]={"color_order",'s'}; f[2]={"max_brightness",'n'}; count=3; break;
    case Test: f[0]={"test",'s'}; break;
  }
  if(memchr(body,0,received)||!parseFields(body,f,count))
    return failure(r,"400 Bad Request","Invalid fields or value types");
  int choice=0;
  RGB legacy{};
  if(action==Legacy) {
    const char *hex=f[2].text;
    if(strlen(hex)!=7||hex[0]!='#'||strspn(hex+1,"0123456789abcdefABCDEF")!=6)
      return failure(r,"400 Bad Request","Expected #RRGGBB");
    unsigned long packed=strtoul(hex+1,nullptr,16);
    legacy={uint8_t(packed>>16),uint8_t(packed>>8),uint8_t(packed)};
  }
  if(action==Config) {
    choice=lookup(f[1].text,orders,6);
    if(!validConfig(f[0].n,unsigned(choice),f[2].n))
      return failure(r,"400 Bad Request","Count 1-150, valid RGB order, ceiling 1-128 required");
  }
  if(action==Test && (choice=lookup(f[0].text,tests,6))<0) return failure(r,"400 Bad Request","Unknown diagnostic");
  if(!ledMutex||xSemaphoreTake(ledMutex,pdMS_TO_TICKS(100))!=pdTRUE)
    return failure(r,"503 Service Unavailable","LED engine busy or unavailable");
  if(!ready) { xSemaphoreGive(ledMutex); return failure(r,"503 Service Unavailable","LED driver unavailable; inspect GET /api/led"); }
  const Lighting before=light;
  switch(action) {
    case Legacy: light.power=f[0].n; light.brightness=f[1].n; light.color=legacy; break;
    case Power: light.power=f[0].n; break;
    case Color: light.color={uint8_t(f[0].n),uint8_t(f[1].n),uint8_t(f[2].n)}; break;
    case Brightness: light.brightness=f[0].n; break;
    case Config: light.count=f[0].n; light.order=unsigned(choice); light.limit=f[2].n; break;
    case Test: testMode=unsigned(choice); testUntil=esp_timer_get_time()+5000000; break;
  }
  if(action==Legacy || action==Color) {
    selectStudioGradient(); // Clear the active animation before applying a legacy solid color.
    legacySolid(before.color);
  }
  if(action!=Test && testMode) { testMode=0; dirtyFrame=true; }
  const bool persist=before.count!=light.count||before.order!=light.order||before.limit!=light.limit||
    before.brightness!=light.brightness||before.color.r!=light.color.r||before.color.g!=light.color.g||before.color.b!=light.color.b;
  if(persist) saveAt=esp_timer_get_time()+5000000;
  dirtyFrame |= persist || before.power!=light.power || action==Test;
  edited=true;
  return ledReply(r);
}

#include "lumina_color_api.h"
#include "lumina_effect_api.h"

esp_err_t httpError(httpd_req_t *r, httpd_err_code_t code) {
  if (code == HTTPD_405_METHOD_NOT_ALLOWED) return failure(r, "405 Method Not Allowed", "Unsupported method");
  return failure(r, "404 Not Found", "Endpoint not found");
}

bool startServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_open_sockets = 4;
  cfg.max_uri_handlers = 30;
  cfg.stack_size = 8192;
  cfg.lru_purge_enable = true;
  cfg.recv_wait_timeout = 1;
  cfg.send_wait_timeout = 1;
  if (httpd_start(&server, &cfg) != ESP_OK) { server = nullptr; return false; }
  const httpd_uri_t routes[] = {
    {"/", HTTP_GET, root, nullptr}, {"/api/status", HTTP_GET, status, nullptr},
    {"/api/system", HTTP_GET, systemInfo, nullptr}, {"/api/network", HTTP_GET, network, nullptr},
    {"/api/health", HTTP_GET, health, nullptr}, {"/api/events", HTTP_GET, events, nullptr},
    {"/api/led", HTTP_GET, getLed, nullptr}, {"/api/led", HTTP_PUT, putLed, nullptr},
    {"/api/led/power", HTTP_POST, putLed, &actions[0]},
    {"/api/led/color", HTTP_POST, putLed, &actions[1]},
    {"/api/led/brightness", HTTP_POST, putLed, &actions[2]},
    {"/api/led/config", HTTP_POST, putLed, &actions[3]},
    {"/api/led/test", HTTP_POST, putLed, &actions[4]},
    {"/api/effects", HTTP_GET, getEffects, nullptr},
    {"/api/led/effect", HTTP_GET, getEffectState, nullptr},
    {"/api/led/effect", HTTP_POST, postEffect, &effectActions[0]},
    {"/api/led/parameters", HTTP_GET, getEffectState, nullptr},
    {"/api/led/parameters", HTTP_POST, postEffect, &effectActions[1]},
    {"/api/led/effect/reset", HTTP_POST, postEffect, &effectActions[2]},
    {"/api/led/effect/stop", HTTP_POST, postEffect, &effectActions[3]},
    {"/api/color",HTTP_GET,getColor,nullptr}, {"/api/color",HTTP_POST,postColor,nullptr},
    {"/api/palettes",HTTP_GET,getPalettes,nullptr}, {"/api/palettes",HTTP_POST,postPalettes,nullptr},
    {"/api/palettes",HTTP_DELETE,deletePalettes,nullptr},
    {"/api/color/history",HTTP_GET,getHistory,nullptr}, {"/api/color/history",HTTP_POST,postHistory,nullptr},
    {"/api/color/favorites",HTTP_GET,getFavorites,nullptr}, {"/api/color/favorites",HTTP_POST,postFavorites,nullptr},
  };
  for (const auto &route : routes) {
    if (httpd_register_uri_handler(server, &route) != ESP_OK) {
      httpd_stop(server); server = nullptr; return false;
    }
  }
  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, httpError);
  httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, httpError);
  return true;
}

void serialProvisioning() {
  // The key is never sent over HTTP or printed without a physical serial request.
  if (Serial.available() && Serial.read() == '?') {
    if (apUp.load()) Serial.printf("AP password: %s\n", password);
    diagnostics(apUp.load() ? "ready" : "recovering");
  }
#if ARDUINO_USB_CDC_ON_BOOT
  if (Serial0.available() && Serial0.read() == '?') {
    if (apUp.load()) Serial0.printf("AP password: %s\n", password);
    diagnostics(apUp.load() ? "ready" : "recovering");
  }
#endif
}

void serviceNetwork() {
  static uint32_t previousAttempt = millis() - 10000U;
  if (millis() - previousAttempt < 10000U) return;
  if (apUp.load() && server && WiFi.softAPIP() == apIp) {
    previousAttempt = millis() - 9000U; // Sample healthy links once per second.
    return;
  }
  previousAttempt = millis();
  if (!apUp.load() || WiFi.softAPIP() != apIp) {
    if (server) { httpd_stop(server); server = nullptr; }
    if (!(WiFi.mode(WIFI_AP) && loadPassword() && WiFi.softAPConfig(apIp, apIp, mask) &&
          WiFi.softAP(ssid, password, channel, false, 4) && WiFi.softAPIP() == apIp)) {
      WiFi.mode(WIFI_OFF);
      apUp = false;
      diagnostics("failed; retry in 10 seconds");
      return;
    }
    apUp = true;
  }
  if (!server && !startServer()) diagnostics("ready; HTTP failed, retry in 10 seconds");
  else diagnostics("ready");
}
} // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("LUMINA STUDIO: initializing access point");
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(115200);
#endif
  initLighting();
  lightingTick();
  Serial.printf("LED GPIO %d: %s\n", LUMINA_LED_GPIO, ready ? "RMT initialized" : ledError);
  WiFi.onEvent([](arduino_event_id_t event) {
    if (event == ARDUINO_EVENT_WIFI_AP_STOP) apUp = false;
  });
  serviceNetwork();
}

void loop() {
  lightingTick();
  serialProvisioning();
  serviceNetwork();
  delay(10);
}
