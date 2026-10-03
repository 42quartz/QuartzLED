#include "led_engine.h"

#include <FastLED.h>

const char* const kEffects[FX_COUNT] = {"solid", "rainbow", "chase", "breathe", "fire", "twinkle", "marks"};
const char* const kChips[CHIP_COUNT] = {"ws2812", "ws2811_400", "ucs1903"};

int effectFromName(const char* name) {
  for (int i = 0; i < FX_COUNT; i++)
    if (strcasecmp(name, kEffects[i]) == 0) return i;
  return -1;
}

int chipFromName(const char* name) {
  for (int i = 0; i < CHIP_COUNT; i++)
    if (strcasecmp(name, kChips[i]) == 0) return i;
  return -1;
}

namespace led {
namespace {

CRGB frame[LED_MAX];  // logical colors (r,g,b)
CRGB wire[LED_MAX];   // reordered for the chip; controller declared as RGB
uint8_t heat[LED_MAX];
uint8_t orderMap[3] = {1, 0, 2};  // wire byte i = logical component orderMap[i]

LedState st;
LedConfig cfg;
float curBri = 0;  // animated brightness incl. on/off fade
uint32_t lastFrame = 0;
uint32_t phase = 0;  // effect clock, advanced by speed

void buildOrderMap(const char* order) {
  for (int i = 0; i < 3; i++) {
    switch (toupper(order[i])) {
      case 'R': orderMap[i] = 0; break;
      case 'G': orderMap[i] = 1; break;
      default: orderMap[i] = 2; break;
    }
  }
}

void render(uint16_t n) {
  CRGB c(st.r, st.g, st.b);
  uint8_t t8 = phase >> 8;

  if (st.probe >= 0) {
    fill_solid(frame, n, CRGB::Black);
    if (st.probe < n) frame[st.probe] = CRGB::White;
    return;
  }

  switch (st.effect) {
    case FX_SOLID:
      fill_solid(frame, n, c);
      break;
    case FX_RAINBOW:
      fill_rainbow(frame, n, t8, max(1, 255 / max<int>(n, 1)));
      break;
    case FX_CHASE: {
      fadeToBlackBy(frame, n, 40);
      uint16_t pos = (phase >> 6) % n;
      frame[pos] = c;
      break;
    }
    case FX_BREATHE: {
      uint8_t k = quadwave8(t8);
      CRGB s = c;
      s.nscale8_video(max<uint8_t>(k, 8));
      fill_solid(frame, n, s);
      break;
    }
    case FX_FIRE: {  // Fire2012
      for (uint16_t i = 0; i < n; i++) heat[i] = qsub8(heat[i], random8(0, ((55 * 10) / n) + 2));
      for (int k = n - 1; k >= 2; k--) heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
      if (random8() < 120) {
        int y = random8(min<int>(7, n));
        heat[y] = qadd8(heat[y], random8(160, 255));
      }
      for (uint16_t j = 0; j < n; j++) frame[j] = HeatColor(heat[j]);
      break;
    }
    case FX_TWINKLE:
      fadeToBlackBy(frame, n, 20);
      if (random8() < 80) frame[random16(n)] = c;
      break;
    case FX_MARKS:  // counting aid: every 10th red, every 5th green
      for (uint16_t i = 0; i < n; i++)
        frame[i] = (i % 10 == 0) ? CRGB::Red : (i % 5 == 0) ? CRGB::Green : CRGB::Black;
      break;
  }
}

}  // namespace

void begin(const LedConfig& c) {
  cfg = c;
  switch (cfg.chip) {
    case CHIP_WS2811_400: FastLED.addLeds<WS2811_400, LED_PIN, RGB>(wire, LED_MAX); break;
    case CHIP_UCS1903: FastLED.addLeds<UCS1903, LED_PIN, RGB>(wire, LED_MAX); break;
    default: FastLED.addLeds<WS2812B, LED_PIN, RGB>(wire, LED_MAX); break;
  }
  FastLED.clear(true);
  setConfig(cfg);
}

void setConfig(const LedConfig& c) {
  uint16_t old = cfg.count;
  cfg = c;
  cfg.count = constrain(cfg.count, 1, LED_MAX);
  buildOrderMap(cfg.order);
  // Power limiting is done in loop() so brightness scales inside the budget instead of clipping at it.
  // Blank pixels beyond the new length before shrinking the controller.
  if (cfg.count < old) {
    fill_solid(wire, LED_MAX, CRGB::Black);
    FastLED[0].setLeds(wire, LED_MAX);
    FastLED.show();
  }
  FastLED[0].setLeds(wire, cfg.count);
}

void setState(const LedState& s) { st = s; }

void blank() {
  fill_solid(wire, LED_MAX, CRGB::Black);
  FastLED.show();
}

void loop() {
  uint32_t now = millis();
  if (now - lastFrame < 16) return;
  uint32_t dt = now - lastFrame;
  lastFrame = now;

  phase += dt * st.speed / 4;  // speed 0 freezes the animation

  float target = st.on || st.probe >= 0 ? (st.probe >= 0 ? max<uint8_t>(st.bri, 96) : st.bri) : 0;
  float step = dt * 0.4f;  // ~650 ms full-scale fade
  if (fabsf(target - curBri) <= step) curBri = target;
  else curBri += target > curBri ? step : -step;

  uint16_t n = cfg.count;
  render(n);
  for (uint16_t i = 0; i < n; i++) {
    const uint8_t* p = frame[i].raw;
    wire[i] = CRGB(p[orderMap[0]], p[orderMap[1]], p[orderMap[2]]);
  }
  // Perceptual curve, then scale into the brightest level the power budget allows for this frame.
  uint8_t user = (uint8_t)curBri;
  uint16_t perceived = user ? max<uint16_t>(1, (uint16_t)user * user / 255) : 0;
  uint8_t ceiling = calculate_max_brightness_for_power_vmA(wire, n, 255, 5, cfg.powerMa);
  uint8_t out = perceived * ceiling / 255;
  if (user && !out) out = 1;
  FastLED.setBrightness(out);
  FastLED.show();
}

}  // namespace led
