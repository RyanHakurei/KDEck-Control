#!/usr/bin/env bash
# Remove kdeck-control. Your config and add-ons are kept unless you pass --purge.
set -euo pipefail
DATA=${XDG_DATA_HOME:-$HOME/.local/share}
CONF=${XDG_CONFIG_HOME:-$HOME/.config}
PURGE=0
[ "${1:-}" = "--purge" ] && PURGE=1

if systemctl --user show-environment >/dev/null 2>&1; then
    systemctl --user disable --now kdeck-control.service 2>/dev/null || true
    systemctl --user disable --now galleon-deck.service 2>/dev/null || true
    rm -f "$CONF/systemd/user/kdeck-control.service" "$CONF/systemd/user/galleon-deck.service"
    systemctl --user daemon-reload
fi
pid="${XDG_RUNTIME_DIR:-/tmp}/kdeck-control.pid"
[ -f "$pid" ] && kill "$(cat "$pid")" 2>/dev/null || true
oldpid="${XDG_RUNTIME_DIR:-/tmp}/galleon-deck.pid"
[ -f "$oldpid" ] && kill "$(cat "$oldpid")" 2>/dev/null || true

rm -f "$HOME/.local/bin/kdeck-control" "$HOME/.local/bin/kdeck-control-config" "$HOME/.local/bin/kdeck-addon"
rm -f "$HOME/.local/bin/galleon-deck" "$HOME/.local/bin/galleon-deck-config" "$HOME/.local/bin/galleon-addon"
rm -rf "$DATA/kdeck-control" "$DATA/galleon-deck"
rm -f "$DATA/applications/org.kde.kdeckcontrol.desktop" \
    "$DATA/applications/io.github.galleondeck.Config.desktop" \
    "$DATA/applications/org.kde.galleondeck.desktop" \
    "$CONF/autostart/kdeck-control-autostart.desktop" \
    "$CONF/autostart/galleon-deck-autostart.desktop"
# --purge removes the new config only. The previous config directory is left in place.
[ "$PURGE" = 1 ] && rm -rf "$CONF/kdeck-control" "$DATA/kdeck-control-addons" "${XDG_STATE_HOME:-$HOME/.local/state}/kdeck-control"

echo "Removing the udev rule and uinput autoload needs root:"
sudo rm -f /etc/udev/rules.d/71-kdeck-control.rules /etc/udev/rules.d/71-galleon-deck.rules \
    /etc/udev/rules.d/71-galleon-deck-hidraw.rules \
    /etc/modules-load.d/kdeck-control.conf /etc/modules-load.d/galleon-deck.conf
sudo udevadm control --reload
echo "Done.$([ "$PURGE" = 0 ] && echo " Your config is still in $CONF/kdeck-control.")"
