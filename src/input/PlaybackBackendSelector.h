#pragma once

#include "InputSenderBackend.h"
#include "PortalController.h"

namespace CatClicker {

struct PlaybackBackendSelection {
    InputSenderBackend *backend = nullptr;
    QString backendName;
    QString reason;
};

class PlaybackBackendSelector {
public:
    static PlaybackBackendSelection select(const PortalCapabilities &capabilities,
                                           const QString &desktopName,
                                           InputSenderBackend *preferredEiBackend,
                                           InputSenderBackend *uinputBackend);
};

}
