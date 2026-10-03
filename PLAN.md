# QuartzLED — Plan

Durum: 2026-10-03 · Hedef: MR Star kontrolcüsünü Deneyap Kart (ESP32) üzerinde kendi firmware'imizle değiştirmek (proje adı: **QuartzLED**; AI asistanın adı: **MiniBeyaz**); HomeKit, müzik modu ve ProBook'taki "MiniBeyaz" AI servisiyle entegre etmek.

## Donanım

| Parça | Bilgi |
|---|---|
| Kart | Deneyap Kart (1. nesil) — ESP32-D0WD-V3, 240 MHz çift çekirdek, **4 MB flash, PSRAM yok (~320 KB RAM)** |
| USB | CP2102N → `/dev/cu.usbserial-1140` (hub üzerinden; 115200 baud güvenilir, 460800+ bozuluyor) |
| Şerit | 5V adreslenebilir, VCC / DA / GND (WS2812 uyumlu varsayımı; Adım 2'de doğrulanacak) |
| Veri pini | **D0 = GPIO 23** (önerilen). Not: D0 ≠ GPIO 32 |
| Mikrofon | Kart üstü PDM: MICD = GPIO 12, MICC = GPIO 13 |
| Buton | GPKEY = GPIO 0 → mikrofon susturma / mod |
| Kart LED'i | LEDB = GPIO 4 (LEDR/LEDG, TX/RX ile ortak — kullanılmayacak) |
| Kaçınılacak | GPIO 1/3 (seri), 0/2/12/15 (strapping — dikkatli), 6–11 (flash) |

Yedekler: `backup/deneyap_original_4MB.bin` (eski sketch), `wled/` (WLED 16.0.1). Orijinal MR Star kutusu duruyor.

## Mimari

```
 iPad / iPhone / Mac ──HomeKit (HAP)──┐
 Redmi (LineageOS) ──web/HTTP─────────┤
                                      ▼
                         ┌──────── ESP32 "uç cihaz" ────────┐
                         │ LED motoru (FastLED, efektler)   │
                         │ Komut çekirdeği (JSON şema v1)   │
                         │ HomeSpan · HTTP API · OTA · mDNS │
                         │ MQTT istemcisi                   │
                         │ Mikrofon: yerel FFT (müzik modu) │
                         │           + ses akışı (isteğe)   │
                         └──────────────┬───────────────────┘
                                        │ MQTT (komut/durum) + UDP/WebSocket (ses)
                                        ▼
                ┌──────── ProBook (Debian) — "MiniBeyaz" ────────┐
                │ Mosquitto (MQTT broker)                        │
                │ Uyandırma kelimesi (openWakeWord)              │
                │ STT (whisper.cpp) · TTS (isteğe, Piper)        │
                │ LLM orkestratörü (yerel model / API)           │
                │   → araçlar = JSON komut şeması                │
                │ Hafıza, sahneler, zamanlamalar, otomasyon      │
                └────────────────────────────────────────────────┘
```

**İlke:** ESP aptal ama sağlam; zeka sunucuda. Ağ/sunucu yokken ESP tek başına çalışmaya devam eder (HomeKit, web, müzik modu, kumanda).

### Komut şeması (tek doğruluk kaynağı)

Tüm arayüzler (seri, HTTP, MQTT, HomeKit köprüsü, LLM araçları) aynı çekirdeğe gider:

```json
{"v":1,"cmd":"set","on":true,"bri":180,"color":[255,80,0],"effect":"breathe","speed":120,"transition_ms":800}
{"v":1,"cmd":"get"}
{"v":1,"cmd":"scene","name":"akşam"}
{"v":1,"cmd":"mic","mode":"off|music|stream"}
```

Durum yayını (MQTT `led/<id>/state`, retained) aynı alanları içerir. Şema sürümlü (`v`), bilinmeyen alanlar yok sayılır → sunucu tarafı firmware güncellemeden genişleyebilir.

MQTT konuları: `led/<id>/cmd` · `led/<id>/state` · `led/<id>/event` (buton, kumanda, ses algılandı) · `led/<id>/avail` (LWT).

## Aşamalar

### 0 — Güç teşhisi
WLED-AP kopmaları reset (brownout) mu? Seri log ile ölçülür. Reset varsa: kartı doğrudan Mac'e / iyi kabloya al, şeridi testte karttan besleme.

### 1 — Altyapı
- PlatformIO (proje içi `.pio-core`), `espressif32` platformu, Arduino çekirdeği, FastLED.
- `firmware/` (PlatformIO projesi), `tools/ledctl.py` (seri/HTTP istemci), `server/` (MiniBeyaz, sonra).
- Bölüm tablosu: `min_spiffs` (2 × 1.9 MB OTA). Yerel git deposu.

### 2 — Firmware v1: USB kontrol + kalibrasyon
- Seri komut kabuğu (insan okunur + JSON satırları).
- FastLED, RMT sürücü; güç sınırı varsayılan 5 V / 400 mA.
- Efektler: solid, rainbow, chase, breathe, fire, twinkle; yumuşak geçişler.
- NVS'de kalıcı ayar; açılışta reset nedeni raporu.
- Kalibrasyon: çip tipi → renk sırası → **LED sayısı (ikili arama, "ışık var/yok")**.

### 3 — Wi-Fi
- Tarama (SSID, kanal, RSSI, auth türü), bağlantı olaylarının sebep kodlarıyla loglanması.
- Kimlik bilgileri `ledctl.py wifi` ile kullanıcı tarafından girilir (getpass); asla loglanmaz.
- Router: ESP32 için saf WPA2, 2.4 GHz bir ağ; misafir ağlarında istemci izolasyonu kapalı olmalı. WPA3 sorunu sürerse Arduino 3.x / IDF 5 (pioarduino) denenir.

### 4 — Ağ kontrolü
- HTTP JSON API + basit web arayüzü, mDNS `quartzled.local`, ArduinoOTA.
- MQTT istemcisi (broker ProBook'ta; yoksa ESP sessizce bekler).

### 5 — HomeKit (HomeSpan)
- Lightbulb servisi (on/bri/hue/sat) + efektler için ek anahtarlar/sahneler.
- Eşleştirme kodu seri porttan verilir. Siri / Ev uygulaması / otomasyonlar.
- RAM bütçesi kritik: HomeSpan + Wi-Fi + MQTT + ses akışı birlikte ölçülecek.

### 6 — Müzik modu
- I2S PDM ile mikrofon, ~16 kHz; FFT (bantlar + ritim tespiti) ESP'de, sunucusuz.
- Efektler: VU, spektrum, ritimde flaş/renk değişimi. Hassasiyet ve otomatik kazanç.

### 7 — MiniBeyaz (ProBook)
- 7a: Mosquitto + küçük Python servisi; MQTT üzerinden LED kontrolü, metin komutları.
- 7b: Ses: ESP → (buton veya ses seviyesi tetikli) ses akışı → openWakeWord → whisper.cpp.
- 7c: LLM orkestratörü: araçlar = komut şeması; sahneler, zamanlama, bağlam/hafıza.
- 7d: Yanıt: LED ile görsel geri bildirim (dinliyor/düşünüyor/tamam), isteğe TTS (Mac/iPad/sunucu hoparlörü).
- Gizlilik: ses ev ağından çıkmaz (yerel modeller); GPKEY ile donanımsal susturma; susturma durumu kart LED'inde.

### 8 — Fiziksel kumanda (koşullu)
Orijinal kontrol kartı açılıp fotoğraflanır. Seçenekler:
1. Alıcı modülün (433 MHz / 2.4 GHz) veri çıkışını ESP'ye bağlayıp kodları çözmek.
2. Orijinal kartın LED veri çıkışını ESP'de RMT ile okuyup tuş basımını dolaylı anlamak.
3. Orijinal MCU'nun buton/UART hatlarını dinlemek.
Uygulanabilir değilse aşama iptal.

### 9 — Kalıcı kurulum
USB şarj adaptörü ile besleme, güç bütçesine göre parlaklık sınırı, gerekirse 74AHCT125 seviye dönüştürücü, 330 Ω seri direnç, kutu.

## Riskler

| Risk | Önlem |
|---|---|
| Brownout (ince kablo/hub) | Aşama 0 ölçümü; Wi-Fi TX gücünü düşürme; iyi besleme |
| RAM yetersizliği (PSRAM yok) | Modüler derleme bayrakları; ses akışı ve HomeSpan ölçülerek; gerekirse ESP32-S3'e geçiş (aynı kod tabanı) |
| WPA3 uyumsuzluğu | Teşhis firmware'i; IDF 5; router ayarı |
| 3.3 V veri sinyali | Seviye dönüştürücü |
| Flash boyutu | `min_spiffs`, gzip'li web arayüzü |
