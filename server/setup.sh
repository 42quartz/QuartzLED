#!/usr/bin/env bash
# One-time setup for the QuartzLED home server on Debian 13 (ProBook).
#   sudo bash ~/quartzled-server/setup.sh
# Installs Docker from Debian's repos, lets $SUDO_USER manage containers,
# generates MQTT passwords (secrets.env, mode 600) and starts Mosquitto + Home Assistant.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "Run with sudo: sudo bash $0" >&2
  exit 1
fi
USER_NAME="${SUDO_USER:?run via sudo from your normal account}"
DIR="$(cd "$(dirname "$0")" && pwd)"

echo "==> Installing Docker"
apt-get update -q
apt-get install -y -q docker.io docker-compose
systemctl enable --now docker

echo "==> Adding $USER_NAME to the docker group (root-equivalent; lets you manage containers without sudo)"
usermod -aG docker "$USER_NAME"

echo "==> MQTT credentials"
mkdir -p "$DIR/mosquitto/data" "$DIR/mosquitto/log" "$DIR/homeassistant"
if [[ ! -f "$DIR/secrets.env" ]]; then
  gen() { head -c 24 /dev/urandom | base64 | tr -d '/+=' | head -c 24; }
  umask 077
  cat > "$DIR/secrets.env" <<EOF
MQTT_QUARTZLED_PASS=$(gen)
MQTT_HOMEASSISTANT_PASS=$(gen)
MQTT_MINIBEYAZ_PASS=$(gen)
EOF
fi
# shellcheck disable=SC1091
source "$DIR/secrets.env"
: > "$DIR/mosquitto/config/passwd"
for u in quartzled homeassistant minibeyaz; do
  var="MQTT_${u^^}_PASS"
  docker run --rm -v "$DIR/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 \
    mosquitto_passwd -b /mosquitto/config/passwd "$u" "${!var}"
done
chmod 600 "$DIR/mosquitto/config/passwd"
# Mosquitto runs as uid 1883 inside the container.
chown -R 1883:1883 "$DIR/mosquitto"
chown -R "$USER_NAME:$USER_NAME" "$DIR/homeassistant" "$DIR/secrets.env"

echo "==> Starting containers"
cd "$DIR"
docker-compose up -d

IP=$(hostname -I | awk '{print $1}')
echo
echo "Done."
echo "  Home Assistant: http://$IP:8123  (first start takes 1-2 minutes)"
echo "  MQTT broker:    $IP:1883 (users: quartzled, homeassistant, minibeyaz; passwords in $DIR/secrets.env)"
echo "Log out and back in (or reboot) for the docker group to apply to $USER_NAME."
