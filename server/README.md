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

## Gotcha

`docker exec` / `docker run` need `-i` to receive a heredoc on stdin; without it the command silently
gets empty input.
