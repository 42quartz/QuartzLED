#!/usr/bin/env bash
# Builds and starts MiniBeyaz as a container next to Mosquitto + Home Assistant (no sudo; needs the
# docker group). Audio goes to your desktop session's PipeWire through its PulseAudio socket.
#   bash ~/quartzled-server/minibeyaz/install.sh
set -euo pipefail
export LC_ALL=C
DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER="$(dirname "$DIR")"
VOICE="${PIPER_VOICE:-tr_TR-dfki-medium}"
VOICE_URL="https://huggingface.co/rhasspy/piper-voices/resolve/main/tr/tr_TR/$(echo "$VOICE" | cut -d- -f2)/$(echo "$VOICE" | cut -d- -f3)"

echo "==> Piper voice $VOICE"
mkdir -p "$DIR/models" "$DIR/data"
for ext in onnx onnx.json; do
  [[ -s "$DIR/models/$VOICE.$ext" ]] || curl -fsSL -o "$DIR/models/$VOICE.$ext" "$VOICE_URL/$VOICE.$ext"
done

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
DATA_DIR=/data
HTTP_PORT=8790
VOLUME=0.8
PULSE_SERVER=unix:/run/pulse/native
EOF
fi

echo "==> Container"
cd "$SERVER"
docker-compose up -d --build minibeyaz
sleep 3
docker logs --tail 5 minibeyaz
