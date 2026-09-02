#!/usr/bin/env bash
set -euo pipefail

sudo apt update
sudo apt install \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  qt6-base-dev \
  qt6-declarative-dev \
  qml6-module-qtqml \
  qml6-module-qtqml-models \
  qml6-module-qtqml-workerscript \
  qml6-module-qtquick \
  qml6-module-qtquick-window \
  qml6-module-qtquick-layouts \
  qml6-module-qtquick-controls \
  qml6-module-qtquick-templates \
  qml6-module-qtquick-dialogs \
  libpipewire-0.3-dev \
  libspa-0.2-dev \
  libei-dev \
  libevdev-dev
