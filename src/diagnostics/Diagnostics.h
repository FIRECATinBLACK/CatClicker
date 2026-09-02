#pragma once

#include "../input/EvdevDeviceInspector.h"
#include "../input/GlobalInputMonitor.h"
#include "../input/InputSenderBackend.h"
#include "../input/PortalController.h"

#include <QtCore/QObject>

namespace CatClicker {

class Diagnostics : public QObject {
    Q_OBJECT

public:
    explicit Diagnostics(QObject *parent = nullptr);

    QString generateReport(const PortalCapabilities &capabilities,
                           const MacroDisplayInfo &display,
                           bool pipeWireDetected,
                           bool qtFocusedCaptureDetected,
                           const QString &cursorTrackerStatus,
                           const EvdevDeviceInspector &evdevInspector,
                           const GlobalInputMonitor &globalInputMonitor,
                           const QString &selectedPlaybackBackend,
                           const QString &playbackReason,
                           const InputSenderBackend *backend) const;
};

}
