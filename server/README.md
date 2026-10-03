# QuartzLED home server (ProBook)

Mosquitto (MQTT, :1883) + Home Assistant (:8123) in Docker. Files live in `~/quartzled-server` on the ProBook.

## First-time setup

```bash
scp -r server/* eck@<probook>:quartzled-server/
ssh -t eck@<probook> 'sudo bash ~/quartzled-server/setup.sh'
```

`setup.sh` installs Debian's `docker.io` + `docker-compose`, generates MQTT passwords into `secrets.env`
(users `quartzled`, `homeassistant`, `minibeyaz`) and starts both containers. Run it with `LC_ALL=C`
semantics (it sets that itself): under `tr_TR`, `${var^^}` turns `i` into `İ`.

Then in Home Assistant: finish onboarding, add the **MQTT** integration (broker `127.0.0.1`, user
`homeassistant`). QuartzLED appears automatically through MQTT discovery.

## Public HTTPS (Google Home) via Tailscale Funnel

```bash
tailscale funnel --bg 8123          # https://<machine>.<tailnet>.ts.net
tailscale funnel --https=443 off    # disable
```

Home Assistant must trust the local proxy. **Since HA 2026.9 the `http:` YAML block is migrated once
into `.storage/http` and ignored afterwards** — set it in the UI (Settings → System → Network) or, with
HA stopped, in `.storage/http` → `data.stable`:

```json
"use_x_forwarded_for": true,
"trusted_proxies": ["127.0.0.1/32", "::1/128"]
```

Symptom when missing: `400: Bad Request` and *"A request from a reverse proxy was received from
127.0.0.1, but your HTTP integration is not set-up for reverse proxies"* in the HA log.

## QuartzLED web UI over Tailscale (tailnet only)

The ESP32 cannot run Tailscale; the ProBook proxies it. Reachable from your Tailscale devices only:

```bash
tailscale serve --bg --https=8443 http://<board-ip>:80   # https://<machine>.<tailnet>.ts.net:8443
tailscale serve --https=8443 off
```

Give the board a DHCP reservation for <board-ip> in the router so the proxy target never moves.

## MiniBeyaz (announcements + wake survey)

Container next to Mosquitto/HA. Speaks hard-to-notice QuartzLED mode changes in Turkish (Piper, fully local)
through the desktop session's PipeWire, and serves a morning wake survey that tunes the pre-wake light length.

```bash
bash ~/quartzled-server/minibeyaz/install.sh              # downloads the Piper voice, builds, starts
tailscale serve --bg --https=8444 http://127.0.0.1:8790   # survey, tailnet only
```

Survey answers live only in `minibeyaz/data/minibeyaz.db`. Per-person consent for possible future anonymous
sharing is recorded (default off); nothing leaves the machine.

## Gotcha

`docker exec` / `docker run` need `-i` to receive a heredoc on stdin; without it the command silently
gets empty input.
