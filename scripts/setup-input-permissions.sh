#!/usr/bin/env bash
set -euo pipefail

RULE_PATH="/etc/udev/rules.d/70-catclicker-uaccess.rules"

cat <<'EOF'
CatClicker needs active-session access to:
  - /dev/uinput for playback injection
  - keyboard, mouse, and touchpad event nodes for future recording/hotkeys

This setup installs udev rules that add TAG+="uaccess" to those devices so
systemd-logind applies ACLs to the currently active local session.

This is narrower than permanently adding your user to a broad device-access group.
CatClicker itself should still run as your normal user account, not as root.
EOF

sudo tee "${RULE_PATH}" >/dev/null <<'EOF'
SUBSYSTEM=="misc", KERNEL=="uinput", TAG+="uaccess"
SUBSYSTEM=="input", KERNEL=="event*", ENV{ID_INPUT_KEYBOARD}=="1", TAG+="uaccess"
SUBSYSTEM=="input", KERNEL=="event*", ENV{ID_INPUT_MOUSE}=="1", TAG+="uaccess"
SUBSYSTEM=="input", KERNEL=="event*", ENV{ID_INPUT_TOUCHPAD}=="1", TAG+="uaccess"
EOF

echo "Installed ${RULE_PATH}"
echo "Reloading udev rules..."
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=input

cat <<'EOF'
If your system already uses systemd-logind seat ACLs, the new rules should take effect
for the active local session after you log out and back in.

If /dev/uinput or your physical event nodes are still inaccessible afterward, inspect:
  getfacl /dev/uinput
  getfacl /dev/input/event*

If your host does not honor uaccess ACLs for these devices, use a narrower manual fallback
only after reviewing the security tradeoff. CatClicker itself should still not be run as root.
EOF
