# QuartzLED

Open firmware and home-server setup that replaces the closed controller of a cheap USB LED strip
(sold with the "MR Star" app) with an ESP32 you control: Apple Home / Siri, Home Assistant, Google Home,
a built-in web UI, MQTT and a JSON API.

Built on a **Deneyap Kart** (1st gen, ESP32-D0WD-V3, 4 MB flash, no PSRAM) driving a 5 V WS2812 strip
(159 LEDs, GRB). Any classic ESP32 board works with a pin change.

> 🇹🇷 Türkçe özet aşağıda.

## Features

- **23 effects**: solid, rainbow, color loop, breathe, comet, scanner, meteor, theater, two-color, gradient,
  palette wave, Perlin flow, confetti, juggle, fire, candle, twinkle, sparkle, pulse, heartbeat, police,
  sunrise and sunset ramps
- **11 palettes**, secondary color, per-effect intensity, reverse, mirror-from-center
- **12 built-in scenes** + up to 8 user scenes stored on the device
- **Sleep timer**, **sunrise wake-up** and **sunset wind-down** (ends in deep red, then switches off)
- **"Ayılma" daily light plan** that runs even while the light is off:
  - *interval*: wake/sleep clock times with chosen ramp lengths
  - *automatic*: follows the real sky at your location — civil dawn → sunrise, sunset → civil dusk
    (on-device solar calculation, NTP time; location from the browser or entered by hand, never sent elsewhere)
  - *pre-wake (adaptive)*: a morning survey (wake time, tiredness) tunes how long the light rises before waking
- **Spoken announcements** on the home server for modes you can't easily see starting (MiniBeyaz)
- **Power-budgeted brightness**: output is scaled inside a configurable mA budget, never clipped
- **Low-end-dense sliders** everywhere (50 % slider = 10 % value) because perceived brightness and
  animation speed change fastest at the bottom
- Interfaces, all funnelling into one versioned JSON command schema:
  - Web UI at `http://quartzled.local/` (any browser; installable on phones)
  - Apple Home / Siri via [HomeSpan](https://github.com/HomeSpan/HomeSpan) (light + effect switches + speed)
  - MQTT with Home Assistant discovery (light, effect/palette selects, scenes, numbers, switches, buttons)
  - Google Home through Home Assistant
  - HTTP `/api`, USB serial shell
- Password-protected OTA updates; per-device secrets set at runtime over USB (no rebuild needed)

## Repository layout

| Path | What |
|---|---|
| `firmware/` | PlatformIO project (Arduino-ESP32 2.0.x, FastLED 3.9, HomeSpan 1.9, ArduinoJson 7, PubSubClient) |
| `server/` | Docker Compose for Mosquitto + Home Assistant, one-shot `setup.sh`, notes on Tailscale access |
| `server/minibeyaz/` | MiniBeyaz: Turkish voice announcements (Piper) and an adaptive wake survey, via MQTT |
| `tools/` | `ledbridge.py` (keeps the serial port open and logs it), `ledctl.py` (send commands, Wi-Fi setup) |
| `PLAN.md` | Architecture and roadmap (Turkish) |

## Build and flash

```bash
python3 -m venv .venv && .venv/bin/pip install platformio
cp firmware/secrets.ini.example firmware/secrets.ini   # set an OTA password and an 8-digit HomeKit code
cd firmware
../.venv/bin/pio run -e deneyap -t upload               # first time, over USB
../.venv/bin/pio run -e ota -t upload --upload-port <board-ip>   # afterwards, over Wi-Fi
```

Wiring: strip **DA → D0 (GPIO 23)**, **GND → GND**, **VCC → 5 V**. On other boards change `LED_PIN` in
`platformio.ini`. Set your LED count and power budget once connected (`count 159`, `power 800`).

Wi-Fi credentials are entered on your own machine and never echoed:

```bash
.venv/bin/python tools/ledbridge.py &          # holds the serial port
.venv/bin/python tools/ledctl.py wifi-setup    # asks SSID and password (hidden)
```

The ESP32 needs a **2.4 GHz WPA2** network. WPA3-only networks and band-steered SSIDs commonly fail.

### Secrets, pairing and the `genel` build

| Env | Secrets | Use |
|---|---|---|
| `deneyap` / `ota` | compile-time `OTA_PASSWORD` / `HOMEKIT_CODE` from `secrets.ini` | owner builds |
| `genel` | none (empty strings, no `secrets.ini` reference) | distribution image bundled in the Quartz app |

At runtime the effective values are: NVS namespace `quartz` (keys `anahtar`, `ota`, `hk`) first, then the
compile-time value. With no OTA password at all, OTA stays **off** and the network `homekit`/`mqtt` admin
commands are refused; with no valid HomeKit code the board uses a random, never-shown setup code, so it
cannot be added to Home until it is paired.

Pairing is **serial-only** (refused over HTTP with 403 and over MQTT); the board replies, then reboots:

```json
{"v":1,"cmd":"esle","id":1,"anahtar":"<64 lowercase hex>","ota":"<16-64 printable ASCII>","homekit":"<8 digits>"}
{"v":1,"cmd":"esle","sifirla":true}
```

The HomeKit code must be 8 digits, not all the same, not `12345678`/`87654321`. `info` reports
`"eslesmis": true|false`, `"cihaz": "quartzled_xxxxxx"` (same as the MQTT id; `"id"` stays the echoed
request id) and `"host"`. `genel` boards announce a unique mDNS name, `quartzled-xxxxxx.local`; owner
builds keep `quartzled.local`. Pairing and `sifirla` also forget all Home controllers (on the next boot)
and the stored MQTT broker, so no older control path survives. `reboot` is USB-only (refused over HTTP
and MQTT).

Once an `anahtar` is stored, every `/api` request (GET and POST) must send `X-Quartz-Anahtar: <anahtar>`,
otherwise `401 {"error":"anahtar gerekli"}`. On a paired board the built-in web UI shows that the light
is managed by the Quartz app and disables its controls. Unpaired boards behave as before.

## Command schema (v1)

Every interface sends the same JSON:

```json
{"v":1,"cmd":"set","on":true,"bri":180,"color":[255,80,0],"effect":"noise","palette":"aurora","speed":120}
{"v":1,"cmd":"preset","name":"okuma"}
{"v":1,"cmd":"sunset","minutes":20}
{"v":1,"cmd":"circadian","variant":"auto","lat":41.01,"lon":28.98,"armed":true}
{"v":1,"cmd":"get"}
```

MQTT topics: `quartzled/<id>/cmd`, `/resp`, `/state` (retained), `/event`, `/avail` (LWT).

## Home server

See [`server/README.md`](server/README.md): Mosquitto + Home Assistant on any Debian box, Google Home via a
Tailscale Funnel, and tailnet-only access to the web UI with `tailscale serve`.

## Türkçe özet

QuartzLED, "MR Star" uygulamasıyla satılan USB LED şeritlerin kapalı kaynak kontrol kutusunu bir ESP32 ile
değiştirir. Ev uygulaması / Siri, Home Assistant, Google Home, kart üzerinde web arayüzü, MQTT ve JSON API
desteği vardır. 23 efekt, 11 palet, 12 hazır sahne, uyku zamanlayıcısı, gün doğumu ile uyanış ve gün batımı ile
kararma içerir. "Ayılma" planı ışık kapalıyken bile her gün çalışır: aralıklı (saat belirlenir) ya da
otomatik (konuma göre gerçek şafak/gün doğumu ve gün batımı/alacakaranlık). Arayüzler Türkçedir.

Gizli bilgiler artık kartta çalışırken ayarlanır: Quartz uygulaması kartı USB üzerinden `{"cmd":"esle"}` ile
eşler ve NVS'e (`quartz`: `anahtar`, `ota`, `hk`) bir API anahtarı, OTA parolası ve HomeKit kodu yazar; komut
yalnızca seri porttan kabul edilir. Eşlenmiş kartta her `/api` isteği `X-Quartz-Anahtar` başlığını ister (yoksa
401); eşlenmiş kartta web arayüzü yalnızca "Quartz uygulamasından kontrol edilir" der. Eşleme eski HomeKit denetleyicilerini ve MQTT ayarını siler; genel kartlar `quartzled-xxxxxx.local` adını kullanır, eşlenmemiş genel kart HomeKit'e eklenemez. `pio run -e genel` hiçbir gizli bilgi içermeyen dağıtım
yazılımını üretir (OTA, eşlenene kadar kapalıdır); `deneyap`/`ota` ortamları `secrets.ini` ile çalışmaya devam eder.

## License

[MIT](LICENSE)
