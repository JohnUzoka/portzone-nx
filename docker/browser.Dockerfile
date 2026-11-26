# Runs the desktop build in a virtual display and shows it in a web browser (noVNC), for PCs where
# Linux windows cannot open (WSLg not working). Used by scripts/run-browser.sh.
FROM portzone-desktop-build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      xvfb xauth libgl1-mesa-dri libglx-mesa0 dbus dbus-x11 x11vnc novnc websockify \
    && rm -rf /var/lib/apt/lists/*
