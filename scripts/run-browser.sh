#!/bin/bash
# Runs PortZone in Docker and shows it in your web browser, without WSLg or any window system.
#   scripts/run-browser.sh    then open the printed address in Windows; Ctrl+C here stops it.
# Uses this repository's store/ and its drafts; ./sdmc is the SD card. Build first: scripts/build.sh desktop
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$root/sdmc"

docker image inspect portzone-browser >/dev/null 2>&1 ||
    docker build -t portzone-browser -f "$root/docker/browser.Dockerfile" "$root/docker"

echo "PortZone: open http://localhost:6080/vnc.html?autoconnect=1&resize=scale in your browser."
echo "Keys: arrows move, Enter is A, Escape is B, Y is Y. Ctrl+C here stops it."
docker run --rm -it -p 6080:6080 -v "$root":/work portzone-browser dbus-run-session -- bash -c '
    Xvfb :99 -screen 0 1280x720x24 >/dev/null 2>&1 &
    sleep 1
    export DISPLAY=:99
    x11vnc -display :99 -forever -shared -nopw -quiet >/dev/null 2>&1 &
    websockify --web /usr/share/novnc 6080 localhost:5900 >/dev/null 2>&1 &
    cd /work/build && ./portzone --index /work/store/ports.json --drafts /work/store/drafts --sd /work/sdmc'
