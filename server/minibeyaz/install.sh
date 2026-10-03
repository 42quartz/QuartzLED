#!/usr/bin/env bash
# Builds and starts MiniBeyaz as a container next to Mosquitto + Home Assistant (no sudo; needs the
# docker group). Audio goes to your desktop session's PipeWire through its PulseAudio socket.
#   bash ~/quartzled-server/minibeyaz/install.sh
set -euo pipefail
export LC_ALL=C
DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER="$(dirname "$DIR")"
VOICE="${PIPER_VOICE:-tr_TR-dfki-medium}"
VOICE_EN="${PIPER_VOICE_EN:-en_US-lessac-medium}"   # same 22050 Hz rate: segments are concatenated

fetch_voice() {  # tr_TR-dfki-medium -> tr/tr_TR/dfki/medium/
  local v="$1" locale="${1%%-*}" name quality
  name="$(echo "$v" | cut -d- -f2)"; quality="$(echo "$v" | cut -d- -f3)"
  local url="https://huggingface.co/rhasspy/piper-voices/resolve/main/${locale%%_*}/$locale/$name/$quality"
  for ext in onnx onnx.json; do
    [[ -s "$DIR/models/$v.$ext" ]] || curl -fsSL -o "$DIR/models/$v.$ext" "$url/$v.$ext"
  done
}

echo "==> Piper voices $VOICE + $VOICE_EN"
mkdir -p "$DIR/models" "$DIR/data"
fetch_voice "$VOICE"
fetch_voice "$VOICE_EN"

echo "==> Environment"
if [[ ! -f "$DIR/minibeyaz.env" ]]; then
  # shellcheck disable=SC1091
  source "$SERVER/secrets.env"
  umask 077
  cat > "$DIR/minibeyaz.env" <<EOF
MQTT_HOST=127.0.0.1
MQTT_PORT=1883
MQTT_USER=minibeyaz
MQTT_PASS=$MQTT_MINIBEYAZ_PASS
PIPER_MODEL=/models/$VOICE.onnx
PIPER_MODEL_EN=/models/$VOICE_EN.onnx
DATA_DIR=/data
HTTP_PORT=8790
VOLUME=0.8
PULSE_SERVER=unix:/run/pulse/native
EOF
fi

grep -q '^PIPER_MODEL_EN=' "$DIR/minibeyaz.env" || echo "PIPER_MODEL_EN=/models/$VOICE_EN.onnx" >> "$DIR/minibeyaz.env"

echo "==> Container"
cd "$SERVER"
docker-compose up -d --build minibeyaz
sleep 3
docker logs --tail 5 minibeyaz
