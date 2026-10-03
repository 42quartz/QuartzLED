#pragma once
#include <Arduino.h>

// HomeKit speaks HSV (h 0-360, s/v 0-100); the engine stores RGB at full value.
inline void hsvToRgb(float h, float s, uint8_t& r, uint8_t& g, uint8_t& b) {
  s = constrain(s, 0.0f, 100.0f) / 100.0f;
  h = fmodf(fmaxf(h, 0.0f), 360.0f) / 60.0f;
  int i = (int)h;
  float f = h - i, p = 1 - s, q = 1 - s * f, t = 1 - s * (1 - f);
  float rf, gf, bf;
  switch (i) {
    case 0: rf = 1, gf = t, bf = p; break;
    case 1: rf = q, gf = 1, bf = p; break;
    case 2: rf = p, gf = 1, bf = t; break;
    case 3: rf = p, gf = q, bf = 1; break;
    case 4: rf = t, gf = p, bf = 1; break;
    default: rf = 1, gf = p, bf = q; break;
  }
  r = lroundf(rf * 255), g = lroundf(gf * 255), b = lroundf(bf * 255);
}

inline void rgbToHs(uint8_t r, uint8_t g, uint8_t b, float& h, float& s) {
  float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
  float mx = fmaxf(rf, fmaxf(gf, bf)), mn = fminf(rf, fminf(gf, bf)), d = mx - mn;
  s = mx <= 0 ? 0 : d / mx * 100.0f;
  if (d <= 0) h = 0;
  else if (mx == rf) h = 60.0f * fmodf((gf - bf) / d + 6.0f, 6.0f);
  else if (mx == gf) h = 60.0f * ((bf - rf) / d + 2.0f);
  else h = 60.0f * ((rf - gf) / d + 4.0f);
}
