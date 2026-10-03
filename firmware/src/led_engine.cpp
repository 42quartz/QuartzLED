#include "led_engine.h"

#include <FastLED.h>

const char* const kEffects[FX_COUNT] = {
    "solid", "rainbow", "chase", "breathe", "fire", "twinkle", "marks",
    "colorloop", "gradient", "scanner", "theater", "wave", "noise", "confetti",
    "candle", "sparkle", "police", "pulse", "juggle", "meteor", "sunrise",
    "twocolor", "heartbeat", "sunset"};
const char* const kPalettes[PAL_COUNT] = {"rainbow", "party", "ocean", "lava", "forest", "heat",
                                          "cloud", "sunset", "aurora", "pastel", "colors"};
const char* const kChips[CHIP_COUNT] = {"ws2812", "ws2811_400", "ucs1903"};

static int indexOf(const char* const* names, int n, const char* name) {
  for (int i = 0; i < n; i++)
    if (strcasecmp(name, names[i]) == 0) return i;
  return -1;
}
int effectFromName(const char* name) { return indexOf(kEffects, FX_COUNT, name); }
int paletteFromName(const char* name) { return indexOf(kPalettes, PAL_COUNT, name); }
int chipFromName(const char* name) { return indexOf(kChips, CHIP_COUNT, name); }

bool effectUsesColor(uint8_t fx) {
  switch (fx) {
    case FX_SOLID: case FX_BREATHE: case FX_CHASE: case FX_TWINKLE: case FX_SCANNER: case FX_METEOR:
    case FX_THEATER: case FX_TWOCOLOR: case FX_GRADIENT: case FX_CANDLE: case FX_SPARKLE: case FX_PULSE:
    case FX_HEARTBEAT:
      return true;
    default:
      return false;
  }
}

DEFINE_GRADIENT_PALETTE(gpSunset){0, 120, 0, 0, 22, 179, 22, 0, 51, 255, 104, 0, 85, 167, 22, 18,
                                  135, 100, 0, 103, 198, 16, 0, 130, 255, 0, 0, 160};
DEFINE_GRADIENT_PALETTE(gpAurora){0, 0, 20, 10, 60, 0, 200, 80, 120, 20, 255, 160,
                                  170, 80, 40, 200, 220, 150, 0, 160, 255, 0, 20, 10};
DEFINE_GRADIENT_PALETTE(gpPastel){0, 255, 150, 180, 64, 255, 220, 150, 128, 150, 255, 200,
                                  192, 150, 190, 255, 255, 255, 150, 180};
// Starts as a faint, already visible deep red (no blue: sunset end stays melatonin-friendly).
DEFINE_GRADIENT_PALETTE(gpSunrise){0, 24, 1, 0, 50, 90, 6, 0, 120, 200, 40, 0,
                                   185, 255, 120, 20, 255, 255, 200, 140};

namespace led {
namespace {

CRGB frame[LED_MAX];  // logical colors (r,g,b), length = render length
CRGB wire[LED_MAX];   // reordered/mirrored for the chip; controller declared as RGB
uint8_t heat[LED_MAX];
uint8_t orderMap[3] = {1, 0, 2};  // wire byte i = logical component orderMap[i]

LedState st;
LedConfig cfg;
uint8_t lastEffect = 255;
float curBri = 0;  // animated brightness incl. on/off fade
uint32_t lastFrame = 0;
uint32_t phase = 0;  // effect clock, advanced by speed
uint32_t sunriseStart = 0, sunriseMs = 20UL * 60 * 1000;

void buildOrderMap(const char* order) {
  for (int i = 0; i < 3; i++) {
    switch (toupper(order[i])) {
      case 'R': orderMap[i] = 0; break;
      case 'G': orderMap[i] = 1; break;
      default: orderMap[i] = 2; break;
    }
  }
}

CRGBPalette16 palette(uint8_t id, CRGB c1, CRGB c2) {
  switch (id) {
    case PAL_PARTY: return PartyColors_p;
    case PAL_OCEAN: return OceanColors_p;
    case PAL_LAVA: return LavaColors_p;
    case PAL_FOREST: return ForestColors_p;
    case PAL_HEAT: return HeatColors_p;
    case PAL_CLOUD: return CloudColors_p;
    case PAL_SUNSET: return gpSunset;
    case PAL_AURORA: return gpAurora;
    case PAL_PASTEL: return gpPastel;
    case PAL_COLORS: return CRGBPalette16(c1, c2, c1);
    default: return RainbowColors_p;
  }
}

CRGB scaled(CRGB c, uint8_t k) { return c.nscale8_video(k); }

// Smooth bump for heartbeat: 0..255 inside [start, start+width), else 0.
uint8_t bump(uint16_t u, uint16_t start, uint16_t width) {
  if (u < start || u >= start + width) return 0;
  return sin8((u - start) * 128 / width);
}

void render(uint16_t n) {
  const CRGB c(st.r, st.g, st.b), c2(st.r2, st.g2, st.b2);
  const uint8_t t8 = phase >> 8, k = st.intensity;
  const CRGBPalette16 pal = palette(st.palette, c, c2);

  switch (st.effect) {
    case FX_SOLID:
      fill_solid(frame, n, c);
      break;
    case FX_RAINBOW:
      fill_rainbow(frame, n, t8, max(1, (k / 32 + 1) * 255 / max<int>(n, 1)));
      break;
    case FX_CHASE:  // comet; intensity = tail length
      fadeToBlackBy(frame, n, 255 - scale8(k, 240));
      frame[(phase >> 6) % n] = c;
      break;
    case FX_BREATHE:
      fill_solid(frame, n, scaled(c, max<uint8_t>(quadwave8(t8), 8)));
      break;
    case FX_FIRE: {  // Fire2012; intensity = sparking
      for (uint16_t i = 0; i < n; i++) heat[i] = qsub8(heat[i], random8(0, ((55 * 10) / n) + 2));
      for (int j = n - 1; j >= 2; j--) heat[j] = (heat[j - 1] + heat[j - 2] + heat[j - 2]) / 3;
      if (random8() < 40 + k / 2) {
        int y = random8(min<int>(7, n));
        heat[y] = qadd8(heat[y], random8(160, 255));
      }
      for (uint16_t j = 0; j < n; j++) frame[j] = HeatColor(heat[j]);
      break;
    }
    case FX_TWINKLE:
      fadeToBlackBy(frame, n, 20);
      if (random8() < 20 + k / 2) frame[random16(n)] = c;
      break;
    case FX_MARKS:  // counting aid: every 10th red, every 5th green
      for (uint16_t i = 0; i < n; i++)
        frame[i] = (i % 10 == 0) ? CRGB::Red : (i % 5 == 0) ? CRGB::Green : CRGB::Black;
      break;
    case FX_COLORLOOP:
      fill_solid(frame, n, CHSV(t8, 255 - k / 2, 255));
      break;
    case FX_GRADIENT: {  // c -> c2 -> c, scrolling
      CRGBPalette16 g(c, c2, c);
      for (uint16_t i = 0; i < n; i++) frame[i] = ColorFromPalette(g, i * 255 / n + t8, 255, LINEARBLEND);
      break;
    }
    case FX_SCANNER: {  // Larson scanner; intensity = eye width
      fadeToBlackBy(frame, n, 48);
      uint16_t span = max<uint16_t>(1, 2 * (n - 1));
      uint16_t p = (phase >> 6) % span;
      if (p >= n) p = span - p;
      int w = 1 + k / 40;
      for (int d = -w; d <= w; d++) {
        int i = p + d;
        if (i >= 0 && i < n) frame[i] = scaled(c, 255 - abs(d) * 200 / (w + 1));
      }
      break;
    }
    case FX_THEATER: {  // every 3rd LED in c over a dimmed c2
      uint8_t off = (phase >> 9) % 3;
      for (uint16_t i = 0; i < n; i++) frame[i] = ((i + off) % 3 == 0) ? c : scaled(c2, 40);
      break;
    }
    case FX_WAVE:  // palette scrolling; intensity = how many repeats along the strip
      for (uint16_t i = 0; i < n; i++)
        frame[i] = ColorFromPalette(pal, (uint8_t)(i * (k / 16 + 1) * 256 / n) + t8, 255, LINEARBLEND);
      break;
    case FX_NOISE:  // Perlin flow through the palette; intensity = grain
      for (uint16_t i = 0; i < n; i++)
        frame[i] = ColorFromPalette(pal, inoise8(i * (k / 4 + 8), phase >> 4), 255, LINEARBLEND);
      break;
    case FX_CONFETTI:
      fadeToBlackBy(frame, n, 10);
      if (random8() < 32 + k / 3) frame[random16(n)] += ColorFromPalette(pal, random8());
      break;
    case FX_CANDLE:  // warm flicker; intensity = flicker depth
      for (uint16_t i = 0; i < n; i++) {
        uint8_t f = inoise8(i * 40, phase >> 2);
        frame[i] = scaled(c, 255 - scale8(255 - f, k));
      }
      break;
    case FX_SPARKLE:  // white glints over the base color
      fill_solid(frame, n, scaled(c, 160));
      for (uint8_t j = 0; j < 1 + k / 64; j++)
        if (random8() < 60) frame[random16(n)] = CRGB::White;
      break;
    case FX_POLICE: {
      bool left = ((phase >> 10) & 1) == 0;
      bool flash = (phase >> 7) & 1;
      uint16_t half = n / 2;
      for (uint16_t i = 0; i < n; i++) {
        bool isLeft = i < half;
        frame[i] = (flash && isLeft == left) ? (isLeft ? CRGB::Red : CRGB::Blue) : CRGB::Black;
      }
      break;
    }
    case FX_PULSE:  // rings travelling out from the center
      for (uint16_t i = 0; i < n; i++) {
        uint16_t d = abs((int)i - (int)n / 2);
        frame[i] = scaled(c, cubicwave8(d * (k / 16 + 4) - t8 * 2));
      }
      break;
    case FX_JUGGLE: {
      fadeToBlackBy(frame, n, 24);
      uint8_t dots = 2 + k / 32;
      for (uint8_t j = 0; j < dots; j++) {
        uint16_t pos = scale16((uint16_t)(sin16((phase >> 2) * (j + 3) + j * 8000) + 32768), n - 1);
        frame[pos] |= ColorFromPalette(pal, j * 256 / dots);
      }
      break;
    }
    case FX_METEOR: {  // bright head, crumbling tail; intensity = size
      for (uint16_t i = 0; i < n; i++)
        if (random8() < 80) frame[i].fadeToBlackBy(64);
      int size = 3 + k / 24;
      int head = (phase >> 6) % (n + size * 4);
      for (int j = 0; j < size; j++)
        if (head - j >= 0 && head - j < n) frame[head - j] = c;
      break;
    }
    case FX_SUNRISE:    // dark -> red -> orange -> warm white over sunriseMs
    case FX_SUNSET: {   // the same ramp backwards
      uint32_t el = millis() - sunriseStart;
      uint8_t p = el >= sunriseMs ? 255 : (uint8_t)((uint64_t)el * 255 / sunriseMs);
      if (st.effect == FX_SUNSET) p = 255 - p;
      fill_solid(frame, n, ColorFromPalette(CRGBPalette16(gpSunrise), p, 255, LINEARBLEND));
      break;
    }
    case FX_TWOCOLOR: {  // alternating blocks; intensity = block size
      uint8_t size = 1 + k / 16;
      uint16_t off = phase >> 8;
      for (uint16_t i = 0; i < n; i++) frame[i] = (((i + off) / size) & 1) ? c2 : c;
      break;
    }
    case FX_HEARTBEAT: {  // lub-dub
      uint16_t u = (phase >> 3) & 1023;
      uint8_t v = qadd8(bump(u, 0, 140), scale8(bump(u, 220, 160), 150));
      fill_solid(frame, n, scaled(c, max<uint8_t>(v, 6)));
      break;
    }
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
  if (cfg.count < old) {  // blank pixels beyond the new length before shrinking the controller
    fill_solid(wire, LED_MAX, CRGB::Black);
    FastLED[0].setLeds(wire, LED_MAX);
    FastLED.show();
  }
  FastLED[0].setLeds(wire, cfg.count);
}

void setState(const LedState& s) {
  st = s;
  if (st.effect != lastEffect) {
    if (st.effect == FX_SUNRISE || st.effect == FX_SUNSET) sunriseStart = millis();
    if (st.effect == FX_FIRE) memset(heat, 0, sizeof(heat));
    lastEffect = st.effect;
  }
}

void startSunrise(uint16_t minutes) { startRamp((uint32_t)max<uint16_t>(minutes, 1) * 60 * 1000, 0); }

void startRamp(uint32_t totalMs, uint32_t elapsedMs) {
  sunriseMs = max<uint32_t>(totalMs, 1000);
  sunriseStart = millis() - min(elapsedMs, sunriseMs);
}

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

  float target = st.probe >= 0 ? max<uint8_t>(st.bri, 96) : st.on ? st.bri : 0;
  float step = dt * 0.4f;  // ~650 ms full-scale fade
  if (fabsf(target - curBri) <= step) curBri = target;
  else curBri += target > curBri ? step : -step;

  uint16_t n = cfg.count;
  uint16_t m = st.mirror ? (n + 1) / 2 : n;  // render length
  if (st.probe >= 0) {
    fill_solid(frame, n, CRGB::Black);
    if (st.probe < n) frame[st.probe] = CRGB::White;
  } else {
    render(m);
  }
  for (uint16_t i = 0; i < n; i++) {
    uint16_t src = i;
    if (st.probe < 0) {
      if (st.mirror) src = i < m ? m - 1 - i : i - (n - m);  // frame[0] sits at the center
      if (st.reverse) src = m - 1 - src;
    }
    const uint8_t* p = frame[src].raw;
    wire[i] = CRGB(p[orderMap[0]], p[orderMap[1]], p[orderMap[2]]);
  }

  // Scale the requested level into the brightest output the power budget allows for this frame.
  uint8_t user = (uint8_t)curBri;
  uint8_t ceiling = calculate_max_brightness_for_power_vmA(wire, n, 255, 5, cfg.powerMa);
  uint8_t out = (uint16_t)user * ceiling / 255;
  if (user && !out) out = 1;
  FastLED.setBrightness(out);
  FastLED.show();
}

}  // namespace led
