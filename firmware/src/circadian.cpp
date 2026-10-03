#include "circadian.h"

#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

#include "app.h"
#include "events.h"
#include "led_engine.h"

namespace circadian {
namespace {

enum Variant : uint8_t { VAR_INTERVAL, VAR_AUTO };

struct Config {
  bool armed = false;
  uint8_t variant = VAR_INTERVAL;
  bool located = false;
  float lat = 0, lon = 0;
  uint16_t wakeMin = 7 * 60, wakeDur = 20;    // sunrise ramp ends at wake
  uint16_t sleepMin = 23 * 60, sleepDur = 20; // sunset ramp starts at sleep
  char tz[32] = "<+03>-3";                    // POSIX TZ; default Europe/Istanbul
};

struct Window {
  bool valid = false;
  time_t start = 0, end = 0;
};

Preferences prefs;
Config cfg;
constexpr uint8_t kVer = 1;
bool ntpStarted = false;
int lastRiseDay = -1, lastSetDay = -1;
uint32_t lastTick = 0;

void save() {
  uint8_t buf[sizeof(Config) + 1] = {kVer};
  memcpy(buf + 1, &cfg, sizeof(Config));
  prefs.putBytes("cfg", buf, sizeof(buf));
}

bool timeValid(time_t now) { return now > 1700000000; }  // NTP has synced

// Days since 1970-01-01 for a civil date (H. Hinnant).
long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

float deg2rad(float d) { return d * (float)M_PI / 180.0f; }
float rad2deg(float r) { return r * 180.0f / (float)M_PI; }
float norm(float v, float m) { v = fmodf(v, m); return v < 0 ? v + m : v; }

// Almanac sunrise/sunset algorithm. Returns UT hours, or NAN if the sun never crosses `zenith`.
float solarUT(int dayOfYear, float lat, float lon, float zenith, bool rising) {
  float lngHour = lon / 15.0f;
  float t = dayOfYear + ((rising ? 6.0f : 18.0f) - lngHour) / 24.0f;
  float M = 0.9856f * t - 3.289f;
  float L = norm(M + 1.916f * sinf(deg2rad(M)) + 0.020f * sinf(deg2rad(2 * M)) + 282.634f, 360);
  float RA = norm(rad2deg(atanf(0.91764f * tanf(deg2rad(L)))), 360);
  RA += floorf(L / 90) * 90 - floorf(RA / 90) * 90;
  RA /= 15.0f;
  float sinDec = 0.39782f * sinf(deg2rad(L));
  float cosDec = cosf(asinf(sinDec));
  float cosH = (cosf(deg2rad(zenith)) - sinDec * sinf(deg2rad(lat))) / (cosDec * cosf(deg2rad(lat)));
  if (cosH > 1 || cosH < -1) return NAN;
  float H = (rising ? 360 - rad2deg(acosf(cosH)) : rad2deg(acosf(cosH))) / 15.0f;
  float T = H + RA - 0.06571f * t - 6.622f;
  return norm(T - lngHour, 24);
}

// Today's (local date) ramp windows.
void windows(time_t now, Window& rise, Window& set) {
  struct tm lt;
  localtime_r(&now, &lt);
  time_t localMidnight = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
  rise = set = Window();

  if (cfg.variant == VAR_INTERVAL) {
    rise.end = localMidnight + cfg.wakeMin * 60;
    rise.start = rise.end - cfg.wakeDur * 60;
    set.start = localMidnight + cfg.sleepMin * 60;
    set.end = set.start + cfg.sleepDur * 60;
    rise.valid = set.valid = true;
    return;
  }
  if (!cfg.located) return;

  time_t utcMidnight = (time_t)daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400;
  auto event = [&](float zenith, bool rising, time_t& out) {
    float ut = solarUT(lt.tm_yday + 1, cfg.lat, cfg.lon, zenith, rising);
    if (isnan(ut)) return false;
    out = utcMidnight + (time_t)(ut * 3600);
    while (out < localMidnight) out += 86400;  // keep it on the local date
    while (out >= localMidnight + 86400) out -= 86400;
    return true;
  };
  constexpr float kOfficial = 90.833f, kCivil = 96.0f;
  rise.valid = event(kCivil, true, rise.start) && event(kOfficial, true, rise.end);
  set.valid = event(kOfficial, false, set.start) && event(kCivil, false, set.end);
  // Keep ramps sensible at high latitudes / odd seasons.
  if (rise.valid && rise.end - rise.start < 600) rise.start = rise.end - 600;
  if (set.valid && set.end - set.start < 600) set.end = set.start + 600;
}

String hhmm(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", lt.tm_hour, lt.tm_min);
  return b;
}

String hhmmMin(uint16_t m) {
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", m / 60, m % 60);
  return b;
}

int parseHHMM(const char* s) {
  int h, m;
  if (!s || sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

void trigger(bool rise, const Window& w, time_t now) {
  LedState s = app::state();
  if (!rise && !s.on) return;  // nothing to wind down
  s.effect = rise ? FX_SUNRISE : FX_SUNSET;
  s.on = true;
  if (s.bri < 200) s.bri = 255;
  app::commitState(s);  // must precede startRamp: an effect change resets the ramp clock
  led::startRamp((uint32_t)(w.end - w.start) * 1000, (uint32_t)(now - w.start) * 1000);
  app::setTimer(rise ? 0 : (uint16_t)((w.end - now + 59) / 60));

  JsonDocument ev;
  ev["event"] = "circadian";
  ev["phase"] = rise ? "sunrise" : "sunset";
  ev["until"] = hhmm(w.end);
  events::emit(ev);
}

void startNtp() {
  configTzTime(cfg.tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  ntpStarted = true;
}

}  // namespace

void begin() {
  prefs.begin("circ", false);
  uint8_t buf[sizeof(Config) + 1];
  if (prefs.isKey("cfg") && prefs.getBytes("cfg", buf, sizeof(buf)) == sizeof(buf) && buf[0] == kVer)
    memcpy(&cfg, buf + 1, sizeof(Config));
  setenv("TZ", cfg.tz, 1);
  tzset();
}

void loop() {
  if (millis() - lastTick < 5000) return;
  lastTick = millis();
  if (!ntpStarted) {
    if (WiFi.status() == WL_CONNECTED) startNtp();  // lwIP must be up before SNTP starts
    return;
  }
  time_t now = time(nullptr);
  if (!cfg.armed || !timeValid(now)) return;

  Window rise, set;
  windows(now, rise, set);
  struct tm lt;
  localtime_r(&now, &lt);
  if (rise.valid && now >= rise.start && now < rise.end && lastRiseDay != lt.tm_yday) {
    lastRiseDay = lt.tm_yday;
    trigger(true, rise, now);
  }
  if (set.valid && now >= set.start && now < set.end && lastSetDay != lt.tm_yday) {
    lastSetDay = lt.tm_yday;
    trigger(false, set, now);
  }
}

void command(JsonObjectConst req, JsonDocument& resp) {
  bool changed = false;
  if (req["armed"].is<bool>()) { cfg.armed = req["armed"]; changed = true; }
  if (req["variant"].is<const char*>()) {
    const char* v = req["variant"];
    if (!strcmp(v, "auto")) cfg.variant = VAR_AUTO;
    else if (!strcmp(v, "interval")) cfg.variant = VAR_INTERVAL;
    else { resp["error"] = "variant: interval|auto"; return; }
    changed = true;
  }
  if (req["lat"].is<float>() && req["lon"].is<float>()) {
    float la = req["lat"], lo = req["lon"];
    if (la < -90 || la > 90 || lo < -180 || lo > 180) { resp["error"] = "bad lat/lon"; return; }
    cfg.lat = la, cfg.lon = lo, cfg.located = true;
    changed = true;
  }
  for (auto f : {"wake", "sleep"}) {
    if (!req[f].is<const char*>()) continue;
    int m = parseHHMM(req[f]);
    if (m < 0) { resp["error"] = "time must be HH:MM"; return; }
    (strcmp(f, "wake") ? cfg.sleepMin : cfg.wakeMin) = m;
    changed = true;
  }
  if (req["wake_dur"].is<int>()) { cfg.wakeDur = constrain(req["wake_dur"].as<int>(), 5, 120); changed = true; }
  if (req["sleep_dur"].is<int>()) { cfg.sleepDur = constrain(req["sleep_dur"].as<int>(), 5, 120); changed = true; }
  if (req["tz"].is<const char*>()) {
    strlcpy(cfg.tz, req["tz"], sizeof(cfg.tz));
    setenv("TZ", cfg.tz, 1);
    tzset();
    changed = true;
  }
  if (changed) {
    save();
    lastRiseDay = lastSetDay = -1;  // re-evaluate today with the new plan
  }

  resp["ok"] = true;
  JsonObject o = resp["circadian"].to<JsonObject>();
  o["armed"] = cfg.armed;
  o["variant"] = cfg.variant == VAR_AUTO ? "auto" : "interval";
  o["located"] = cfg.located;
  if (cfg.located) {
    o["lat"] = serialized(String(cfg.lat, 4));
    o["lon"] = serialized(String(cfg.lon, 4));
  }
  o["wake"] = hhmmMin(cfg.wakeMin);
  o["wake_dur"] = cfg.wakeDur;
  o["sleep"] = hhmmMin(cfg.sleepMin);
  o["sleep_dur"] = cfg.sleepDur;
  o["tz"] = cfg.tz;
  time_t now = time(nullptr);
  o["synced"] = timeValid(now);
  if (!timeValid(now)) return;
  o["now"] = hhmm(now);
  Window rise, set;
  windows(now, rise, set);
  JsonObject today = o["today"].to<JsonObject>();
  if (rise.valid) { today["rise_start"] = hhmm(rise.start); today["rise_end"] = hhmm(rise.end); }
  if (set.valid) { today["set_start"] = hhmm(set.start); today["set_end"] = hhmm(set.end); }
}

}  // namespace circadian
