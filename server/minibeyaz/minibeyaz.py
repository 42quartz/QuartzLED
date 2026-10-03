#!/usr/bin/env python3
"""MiniBeyaz — QuartzLED's server-side companion (first slice).

* Speaks hard-to-notice mode changes (Ayılma plan, sunrise/sunset ramps, timers) in Turkish via Piper.
* Morning wake survey (who, when you could wake up, how tired you were last night) served on HTTP.
* Adapts the interval plan's pre-wake light length from survey answers when "Uyanış öncesi" is on.

All data stays on this machine (SQLite). Consent for any future, anonymous data sharing is recorded
per person and defaults to off; nothing is sent anywhere today.

Env: MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS, PIPER_MODEL, HTTP_PORT, SURVEY_URL, DATA_DIR, VOLUME
"""
from __future__ import annotations

import asyncio
import datetime as dt
import json
import logging
import os
import pathlib
import shutil
import sqlite3
import subprocess
import tempfile
import wave

import paho.mqtt.client as mqtt
from aiohttp import web

from speech_tr import at_time, parse_hhmm, until_time

log = logging.getLogger("minibeyaz")
HERE = pathlib.Path(__file__).resolve().parent
DATA = pathlib.Path(os.environ.get("DATA_DIR", HERE / "data"))
PREWAKE_DEFAULT, PREWAKE_MIN, PREWAKE_MAX, PREWAKE_STEP = 30, 15, 60, 5


# ---------------------------------------------------------------- speech
class Speaker:
    """Serialises announcements; synthesis runs in a thread so MQTT/HTTP stay responsive."""

    def __init__(self, model: str | None, volume: float):
        self.queue: asyncio.Queue[str] = asyncio.Queue()
        self.volume = volume
        self.voice = None
        if model and pathlib.Path(model).exists():
            from piper import PiperVoice  # heavy import; only when a model is present

            self.voice = PiperVoice.load(model)
        else:
            log.warning("no Piper model at %s; announcements are logged only", model)

    def say(self, text: str) -> None:
        log.info("say: %s", text)
        self.queue.put_nowait(text)

    def _speak_blocking(self, text: str) -> None:
        if not self.voice:
            return
        with tempfile.NamedTemporaryFile(suffix=".wav") as f:
            with wave.open(f.name, "wb") as wf:
                if hasattr(self.voice, "synthesize_wav"):  # piper-tts >= 1.3
                    self.voice.synthesize_wav(text, wf)
                else:
                    self.voice.synthesize(text, wf)
            if shutil.which("pw-play"):
                cmd = ["pw-play", f"--volume={self.volume}", f.name]
            else:  # container: PulseAudio client talking to the host's PipeWire
                cmd = ["paplay", f"--volume={int(self.volume * 65536)}", f.name]
            subprocess.run(cmd, check=False)

    async def run(self) -> None:
        loop = asyncio.get_running_loop()
        while True:
            text = await self.queue.get()
            try:
                await loop.run_in_executor(None, self._speak_blocking, text)
            except Exception:
                log.exception("speech failed")


# ---------------------------------------------------------------- storage
class Store:
    def __init__(self, path: pathlib.Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.executescript(
            """
            CREATE TABLE IF NOT EXISTS people(
              id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL,
              consent INTEGER NOT NULL DEFAULT 0, created TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS surveys(
              id INTEGER PRIMARY KEY, person_id INTEGER NOT NULL REFERENCES people(id),
              date TEXT NOT NULL, planned_wake TEXT, woke_at TEXT NOT NULL,
              tiredness INTEGER NOT NULL, prewake_min INTEGER, created TEXT NOT NULL,
              UNIQUE(person_id, date));
            CREATE TABLE IF NOT EXISTS kv(k TEXT PRIMARY KEY, v TEXT);
            """
        )

    def get(self, k, default=None):
        row = self.db.execute("SELECT v FROM kv WHERE k=?", (k,)).fetchone()
        return json.loads(row["v"]) if row else default

    def put(self, k, v):
        self.db.execute("INSERT OR REPLACE INTO kv VALUES(?,?)", (k, json.dumps(v)))
        self.db.commit()

    def people(self):
        return [dict(r) for r in self.db.execute("SELECT id,name,consent FROM people ORDER BY name")]


# ---------------------------------------------------------------- app
class MiniBeyaz:
    def __init__(self):
        self.loop: asyncio.AbstractEventLoop | None = None
        self.speaker = Speaker(os.environ.get("PIPER_MODEL"), float(os.environ.get("VOLUME", "0.8")))
        self.store = Store(DATA / "minibeyaz.db")
        self.base: str | None = None  # quartzled/<id>, learned from retained state
        self.plan: dict = {}          # last circadian status from the device
        self.state: dict = {}
        self.survey_url = os.environ.get("SURVEY_URL", "")
        self.quiet_until = 0.0  # skip the device's echo of our own plan changes
        self.mqtt = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="minibeyaz")
        self.mqtt.username_pw_set(os.environ["MQTT_USER"], os.environ["MQTT_PASS"])
        self.mqtt.on_connect = self._on_connect
        self.mqtt.on_message = lambda c, u, m: self.loop.call_soon_threadsafe(self._handle, m.topic, m.payload)

    # -- mqtt
    def _on_connect(self, client, userdata, flags, reason, props):
        log.info("mqtt connected: %s", reason)
        client.subscribe([("quartzled/+/state", 0), ("quartzled/+/event", 0), ("quartzled/+/resp", 0)])

    def send(self, req: dict) -> None:
        if req.get("cmd") == "circadian" and len(req) > 2:
            self.quiet_until = self.loop.time() + 5
        if self.base:
            self.mqtt.publish(f"{self.base}/cmd", json.dumps({"v": 1, "source": "minibeyaz", **req}))

    def _handle(self, topic: str, payload: bytes) -> None:
        try:
            msg = json.loads(payload)
        except ValueError:
            return
        base, leaf = topic.rsplit("/", 1)
        if leaf == "state":
            first = self.base is None
            self.base, self.state = base, msg
            if first:
                self.send({"cmd": "circadian", "id": "mb-plan"})
        elif leaf == "resp" and msg.get("id") == "mb-plan" and "circadian" in msg:
            self.plan = msg["circadian"]
            self._ensure_prewake_default()
        elif leaf == "event":
            self._on_event(msg)

    # -- announcements
    def _on_event(self, ev: dict) -> None:
        kind, phase = ev.get("event"), ev.get("phase")
        if kind == "circadian" and phase in ("armed", "disarmed", "plan"):
            self.plan.update({k: ev[k] for k in ("armed", "variant", "prewake") if k in ev})
            self.send({"cmd": "circadian", "id": "mb-plan"})  # refresh full plan (times, durations)
            text = self._plan_sentence(ev)
            if text and self.loop.time() >= self.quiet_until:
                self.speaker.say(text)
        elif kind == "circadian" and phase in ("sunrise", "sunset"):
            end = until_time(*parse_hhmm(ev["until"]))
            auto = ev.get("variant") == "auto"
            if phase == "sunrise":
                self.speaker.say(
                    f"Şafak söküyor. QuartzLED gün ışığıyla birlikte {end} aydınlanacak." if auto
                    else f"Uyanış ışığı başladı. QuartzLED {end} yavaş yavaş aydınlanacak.")
            else:
                self.speaker.say(
                    f"Gün batıyor. QuartzLED alacakaranlıkla birlikte {end} kararıp kapanacak." if auto
                    else f"Uyku ışığı başladı. QuartzLED {end} kararıp kapanacak.")
        elif kind == "ramp":
            n = ev.get("minutes", 0)
            self.speaker.say(f"Gün doğumu başladı, {n} dakika sürecek." if phase == "sunrise"
                             else f"Gün batımı başladı. QuartzLED {n} dakika sonra kapanacak.")
        elif kind == "timer":
            n = ev.get("minutes", 0)
            self.speaker.say(f"QuartzLED {n} dakika sonra kapanacak." if n else "Kapanma zamanlayıcısı iptal edildi.")

    @staticmethod
    def _day_words(info: dict) -> str:
        h, _ = parse_hhmm(info["start"])
        if info.get("day") == "tomorrow":
            return "Yarın sabah" if h < 12 else "Yarın"
        return "Bu sabah" if h < 12 else "Bugün"

    def _plan_sentence(self, ev: dict) -> str | None:
        if ev.get("phase") == "disarmed":
            return "Ayılma otomatik kurulumu kapatıldı. QuartzLED kendiliğinden açılmayacak."
        if not ev.get("armed"):
            return None
        rise = ev.get("sunrise")
        head = "Ayılma otomatik kuruldu." if ev["phase"] == "armed" else "Ayılma planı güncellendi."
        if not rise:
            return head + " Saat henüz senkron değil ya da konum eksik."
        day = self._day_words(rise)
        if ev.get("variant") == "auto":
            return f"{head} {day} {at_time(*parse_hhmm(rise['end']))} gün aydınlanacak, QuartzLED de eşzamanlı aydınlanacak."
        return (f"{head} {day} uyanış ışığı {at_time(*parse_hhmm(rise['start']))} başlayacak, "
                f"{at_time(*parse_hhmm(rise['end']))} tam aydınlık olacak.")

    # -- pre-wake adaptation
    def _ensure_prewake_default(self) -> None:
        if self.plan.get("prewake") and self.store.get("prewake_min") is None:
            self.store.put("prewake_min", PREWAKE_DEFAULT)
            if self.plan.get("wake_dur") != PREWAKE_DEFAULT:
                self.send({"cmd": "circadian", "wake_dur": PREWAKE_DEFAULT})

    def planned_wake(self) -> str | None:
        """Today's intended wake time: interval wake time, or sunrise in auto mode."""
        if self.plan.get("variant") == "interval":
            return self.plan.get("wake")
        return (self.plan.get("today") or {}).get("rise_end")

    def adapt(self) -> str | None:
        """Nudge pre-wake light length from the last 3 days of answers (all people averaged).

        Woke >10 min late and tired (>=3/5)  -> more light before waking (+5 min)
        Woke >15 min early and rested (<=2/5) -> the light wakes you too early (-5 min)
        Heuristic, not medical advice; bounded to 15-60 min and applied at most once a day.
        """
        if not (self.plan.get("variant") == "interval" and self.plan.get("prewake")):
            return None
        today = dt.date.today().isoformat()
        if self.store.get("last_adapt") == today:
            return None
        since = (dt.date.today() - dt.timedelta(days=2)).isoformat()
        rows = self.store.db.execute(
            "SELECT planned_wake, woke_at, tiredness FROM surveys WHERE date>=? AND planned_wake IS NOT NULL",
            (since,)).fetchall()
        if not rows:
            return None
        score = 0.0
        for r in rows:
            ph, pm = parse_hhmm(r["planned_wake"])
            wh, wm = parse_hhmm(r["woke_at"])
            delta = (wh * 60 + wm) - (ph * 60 + pm)
            score += (delta > 10 and r["tiredness"] >= 3) - (delta < -15 and r["tiredness"] <= 2)
        score /= len(rows)
        cur = self.store.get("prewake_min", PREWAKE_DEFAULT)
        new = cur + PREWAKE_STEP if score > 0.5 else cur - PREWAKE_STEP if score < -0.5 else cur
        new = max(PREWAKE_MIN, min(PREWAKE_MAX, new))
        if new == cur:
            return None
        self.store.put("prewake_min", new)
        self.store.put("last_adapt", today)
        self.send({"cmd": "circadian", "wake_dur": new})
        msg = f"Uyanış öncesi ışık süresi {cur} dakikadan {new} dakikaya ayarlandı."
        self.speaker.say(msg)
        return msg

    # -- morning prompt
    async def scheduler(self) -> None:
        while True:
            await asyncio.sleep(30)
            if self.base and (dt.datetime.now().minute % 10 == 0):
                self.send({"cmd": "circadian", "id": "mb-plan"})  # keep plan/times fresh
            wake = self.planned_wake()
            if not (self.plan.get("armed") and wake):
                continue
            h, m = parse_hhmm(wake)
            now = dt.datetime.now()
            due = now.replace(hour=h, minute=m, second=0, microsecond=0) + dt.timedelta(minutes=10)
            today = now.date().isoformat()
            if due <= now < due + dt.timedelta(minutes=5) and self.store.get("last_prompt") != today:
                self.store.put("last_prompt", today)
                self.speaker.say("Günaydın! Kaçta uyanabildiğini ve dün gece ne kadar yorgun olduğunu "
                                 "uyanış anketine yazar mısın? Böylece ışığı sana göre ayarlarım.")

    # -- http
    def routes(self) -> web.Application:
        app = web.Application()
        app.router.add_get("/", lambda r: web.FileResponse(HERE / "survey.html"))
        app.router.add_get("/api/status", self.http_status)
        app.router.add_post("/api/people", self.http_add_person)
        app.router.add_post("/api/consent", self.http_consent)
        app.router.add_post("/api/survey", self.http_survey)
        return app

    async def http_status(self, request):
        today = dt.date.today().isoformat()
        answered = [r["person_id"] for r in self.store.db.execute("SELECT person_id FROM surveys WHERE date=?", (today,))]
        history = [dict(r) for r in self.store.db.execute(
            "SELECT s.date, p.name, s.planned_wake, s.woke_at, s.tiredness, s.prewake_min FROM surveys s "
            "JOIN people p ON p.id=s.person_id ORDER BY s.date DESC, p.name LIMIT 21")]
        return web.json_response({
            "people": self.store.people(), "answered_today": answered, "history": history,
            "planned_wake": self.planned_wake(), "prewake": bool(self.plan.get("prewake")),
            "prewake_min": self.store.get("prewake_min", self.plan.get("wake_dur")),
            "variant": self.plan.get("variant"), "armed": self.plan.get("armed"),
        })

    async def http_add_person(self, request):
        name = (await request.json()).get("name", "").strip()[:40]
        if not name:
            raise web.HTTPBadRequest(text="name required")
        self.store.db.execute("INSERT OR IGNORE INTO people(name, created) VALUES(?,?)",
                              (name, dt.datetime.now().isoformat(timespec="seconds")))
        self.store.db.commit()
        return await self.http_status(request)

    async def http_consent(self, request):
        body = await request.json()
        self.store.db.execute("UPDATE people SET consent=? WHERE id=?", (int(bool(body["consent"])), int(body["person_id"])))
        self.store.db.commit()
        return await self.http_status(request)

    async def http_survey(self, request):
        body = await request.json()
        woke = body.get("woke_at", "")
        tired = int(body.get("tiredness", 0))
        try:
            parse_hhmm(woke)
        except ValueError:
            raise web.HTTPBadRequest(text="woke_at must be HH:MM")
        if not 1 <= tired <= 5:
            raise web.HTTPBadRequest(text="tiredness 1-5")
        self.store.db.execute(
            "INSERT OR REPLACE INTO surveys(person_id,date,planned_wake,woke_at,tiredness,prewake_min,created) "
            "VALUES(?,?,?,?,?,?,?)",
            (int(body["person_id"]), dt.date.today().isoformat(), self.planned_wake(), woke, tired,
             self.store.get("prewake_min", self.plan.get("wake_dur")), dt.datetime.now().isoformat(timespec="seconds")))
        self.store.db.commit()
        change = self.adapt()
        return web.json_response({"ok": True, "message": change or "Teşekkürler, kaydedildi."})

    # -- main
    async def main(self) -> None:
        self.loop = asyncio.get_running_loop()
        self.mqtt.connect_async(os.environ.get("MQTT_HOST", "127.0.0.1"), int(os.environ.get("MQTT_PORT", "1883")))
        self.mqtt.loop_start()
        runner = web.AppRunner(self.routes())
        await runner.setup()
        await web.TCPSite(runner, "127.0.0.1", int(os.environ.get("HTTP_PORT", "8790"))).start()
        log.info("survey on http://127.0.0.1:%s", os.environ.get("HTTP_PORT", "8790"))
        await asyncio.gather(self.speaker.run(), self.scheduler())


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    asyncio.run(MiniBeyaz().main())
