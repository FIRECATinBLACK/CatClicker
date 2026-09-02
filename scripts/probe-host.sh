#!/usr/bin/env bash
set -euo pipefail

section() {
  printf '\n== %s ==\n' "$1"
}

decode_cursor_modes() {
  local raw="$1"
  local value
  value="$(printf '%s\n' "$raw" | grep -Eo '[0-9]+' | head -n 1 || true)"
  if [[ -z "$value" ]]; then
    echo "  Hidden: unknown"
    echo "  Embedded: unknown"
    echo "  Metadata: unknown"
    return
  fi

  (( value & 1 )) && echo "  Hidden: yes" || echo "  Hidden: no"
  (( value & 2 )) && echo "  Embedded: yes" || echo "  Embedded: no"
  (( value & 4 )) && echo "  Metadata: yes" || echo "  Metadata: no"
}

cmd_version() {
  local name="$1"
  shift
  if command -v "$1" >/dev/null 2>&1; then
    "$@"
  else
    echo "${name}: not found"
  fi
}

pkg_version() {
  local pkg="$1"
  if command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists "$pkg"; then
      printf '%s: %s\n' "$pkg" "$(pkg-config --modversion "$pkg")"
    else
      printf '%s: not found\n' "$pkg"
    fi
  else
    echo "pkg-config: not found"
  fi
}

portal_property() {
  local iface="$1"
  local prop="$2"
  if command -v gdbus >/dev/null 2>&1; then
    gdbus call --session \
      --dest org.freedesktop.portal.Desktop \
      --object-path /org/freedesktop/portal/desktop \
      --method org.freedesktop.DBus.Properties.Get \
      "$iface" "$prop" 2>/dev/null || echo "unavailable"
  elif command -v busctl >/dev/null 2>&1; then
    busctl --user get-property org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop "$iface" "$prop" 2>/dev/null || echo "unavailable"
  else
    echo "gdbus/busctl not found"
  fi
}

section "OS and Session"
uname -a || true
if [[ -r /etc/os-release ]]; then
  cat /etc/os-release
else
  echo "/etc/os-release: unreadable"
fi
printf 'XDG_SESSION_TYPE=%s\n' "${XDG_SESSION_TYPE:-}"
printf 'XDG_CURRENT_DESKTOP=%s\n' "${XDG_CURRENT_DESKTOP:-}"
printf 'WAYLAND_DISPLAY=%s\n' "${WAYLAND_DISPLAY:-}"
printf 'DISPLAY=%s\n' "${DISPLAY:-}"

section "Build Toolchain"
cmd_version "cmake" cmake --version
cmd_version "g++" g++ --version
if command -v qtpaths6 >/dev/null 2>&1; then
  qtpaths6 --qt-version
elif command -v qmake6 >/dev/null 2>&1; then
  qmake6 -query QT_VERSION
else
  echo "Qt version probe: qtpaths6/qmake6 not found"
fi

section "Qt Detection"
if command -v qtpaths6 >/dev/null 2>&1; then
  echo "qtpaths6 --qt-version:"
  qtpaths6 --qt-version
  echo "qtpaths6 --query QT_INSTALL_PLUGINS:"
  qtpaths6 --query QT_INSTALL_PLUGINS
elif command -v qmake6 >/dev/null 2>&1; then
  echo "qmake6 -query QT_VERSION:"
  qmake6 -query QT_VERSION
  echo "qmake6 -query QT_INSTALL_PLUGINS:"
  qmake6 -query QT_INSTALL_PLUGINS
else
  echo "Qt detection: qtpaths6/qmake6 not found"
fi

section "pkg-config Libraries"
for pkg in libpipewire-0.3 libspa-0.2 libei-1.0 libevdev; do
  pkg_version "$pkg"
done

section "Portal Interfaces"
if command -v gdbus >/dev/null 2>&1; then
  gdbus introspect --session \
    --dest org.freedesktop.portal.Desktop \
    --object-path /org/freedesktop/portal/desktop 2>/dev/null | \
    grep -E 'org.freedesktop.portal.(ScreenCast|RemoteDesktop|GlobalShortcuts)|ConnectToEIS' || echo "portal introspection unavailable"
elif command -v busctl >/dev/null 2>&1; then
  busctl --user introspect org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop 2>/dev/null | \
    grep -E 'org.freedesktop.portal.(ScreenCast|RemoteDesktop|GlobalShortcuts)|ConnectToEIS' || echo "portal introspection unavailable"
else
  echo "gdbus/busctl not found"
fi

section "ScreenCast Properties"
echo "version:"
portal_property org.freedesktop.portal.ScreenCast version
echo "AvailableSourceTypes:"
portal_property org.freedesktop.portal.ScreenCast AvailableSourceTypes
echo "AvailableCursorModes:"
cursor_modes_output="$(portal_property org.freedesktop.portal.ScreenCast AvailableCursorModes)"
echo "$cursor_modes_output"
echo "Decoded cursor modes:"
decode_cursor_modes "$cursor_modes_output"

section "RemoteDesktop Properties"
echo "version:"
portal_property org.freedesktop.portal.RemoteDesktop version

section "GlobalShortcuts Properties"
echo "version:"
portal_property org.freedesktop.portal.GlobalShortcuts version

section "uinput Access"
if [[ -e /dev/uinput ]]; then
  ls -l /dev/uinput
  if [[ -r /dev/uinput && -w /dev/uinput ]]; then
    echo "/dev/uinput: readable+writable by current user"
  else
    echo "/dev/uinput: not readable+writable by current user"
  fi
  if command -v getfacl >/dev/null 2>&1; then
    getfacl /dev/uinput || true
  fi
else
  echo "/dev/uinput: missing"
fi

section "Physical Input Nodes"
if command -v udevadm >/dev/null 2>&1; then
  while IFS= read -r node; do
    [[ -e "$node" ]] || continue
    echo "-- $node"
    ls -l "$node"
    [[ -r "$node" ]] && echo "readable by current user: yes" || echo "readable by current user: no"
    udevadm info --query=property --name="$node" 2>/dev/null | grep -E '^(ID_INPUT|NAME|DEVNAME)=' || true
  done < <(find /dev/input -maxdepth 1 -name 'event*' 2>/dev/null | sort)
else
  find /dev/input -maxdepth 1 -name 'event*' 2>/dev/null | sort || true
fi

section "Screens"
if command -v wayland-info >/dev/null 2>&1; then
  echo "wl_output count:"
  wayland-info 2>/dev/null | grep -c '^interface: .*wl_output' || true
  wayland-info 2>/dev/null | grep -E 'xdg_output|logical_|name = ' | head -n 120 || true
else
  echo "wayland-info: not found"
fi

section "Wayland Capture Protocols"
if command -v wayland-info >/dev/null 2>&1; then
  wayland-info 2>/dev/null | grep -E 'ext_image_copy_capture_manager_v1|ext_image_copy_capture_cursor_session_v1|ext_output_image_capture_source_manager_v1|ext_image_capture_source_v1|zcosmic_workspace_image_capture_source_manager_v1' || \
    echo "No matching image-copy/image-capture protocols found"
else
  echo "wayland-info: not found"
fi
