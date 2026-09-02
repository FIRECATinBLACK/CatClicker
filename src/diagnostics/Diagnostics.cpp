#include "Diagnostics.h"

#include "BuildConfig.h"

#include <QtCore/QProcessEnvironment>

namespace CatClicker {

Diagnostics::Diagnostics(QObject *parent)
    : QObject(parent)
{
}

QString Diagnostics::generateReport(const PortalCapabilities &capabilities,
                                    const MacroDisplayInfo &display,
                                    bool pipeWireDetected,
                                    bool qtFocusedCaptureDetected,
                                    const QString &cursorTrackerStatus,
                                    const EvdevDeviceInspector &evdevInspector,
                                    const GlobalInputMonitor &globalInputMonitor,
                                    const QString &selectedPlaybackBackend,
                                    const QString &playbackReason,
                                    const InputSenderBackend *backend) const
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const ScreenCastCursorModeSupport cursorModes = PortalController::decodeCursorModes(capabilities.availableCursorModes);

    QStringList lines;
    lines << QStringLiteral("Session type: %1").arg(env.value(QStringLiteral("XDG_SESSION_TYPE"), QStringLiteral("unknown")));
    lines << QStringLiteral("Desktop: %1").arg(env.value(QStringLiteral("XDG_CURRENT_DESKTOP"), QStringLiteral("unknown")));
    lines << QStringLiteral("Playback backend: %1").arg(selectedPlaybackBackend.isEmpty() ? QStringLiteral("none selected") : selectedPlaybackBackend);
    lines << QStringLiteral("Reason: %1").arg(playbackReason.isEmpty() ? QStringLiteral("not evaluated yet") : playbackReason);

    lines << QStringLiteral("RemoteDesktop portal: %1").arg(capabilities.remoteDesktopInterfaceAvailable ? QStringLiteral("available") : QStringLiteral("unavailable"));
    lines << QStringLiteral("RemoteDesktop version: %1").arg(capabilities.remoteDesktopVersion);
    lines << QStringLiteral("ScreenCast portal: %1").arg(capabilities.screenCastInterfaceAvailable ? QStringLiteral("available") : QStringLiteral("unavailable"));
    lines << QStringLiteral("ScreenCast version: %1").arg(capabilities.screenCastVersion);
    lines << QStringLiteral("ScreenCast AvailableSourceTypes: 0x%1").arg(QString::number(capabilities.availableSourceTypes, 16));
    lines << QStringLiteral("ScreenCast AvailableCursorModes: 0x%1").arg(QString::number(capabilities.availableCursorModes, 16));
    lines << QStringLiteral("ScreenCast cursor modes:");
    lines << QStringLiteral("  Hidden: %1").arg(cursorModes.hidden ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Embedded: %1").arg(cursorModes.embedded ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Metadata: %1").arg(cursorModes.metadata ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("GlobalShortcuts portal: %1").arg(capabilities.globalShortcutsInterfaceAvailable ? QStringLiteral("available") : QStringLiteral("unavailable"));
    lines << QStringLiteral("GlobalShortcuts version: %1").arg(capabilities.globalShortcutsVersion);
    lines << QStringLiteral("libei library: %1").arg(CATCLICKER_HAS_LIBEI ? QStringLiteral("available at build time") : QStringLiteral("unavailable at build time"));
    lines << QStringLiteral("libei usable session: %1").arg(capabilities.remoteDesktopUsableSession ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("PipeWire / SPA: %1").arg(pipeWireDetected ? QStringLiteral("available at build time") : QStringLiteral("unavailable at build time"));
    lines << QStringLiteral("ScreenCast metadata cursor recording: %1")
                 .arg((pipeWireDetected && cursorModes.metadata) ? QStringLiteral("possible on this compositor") : QStringLiteral("not available on this compositor"));
    lines << QStringLiteral("Focused Qt capture backend: %1").arg(qtFocusedCaptureDetected ? QStringLiteral("available") : QStringLiteral("unavailable"));
    lines << QStringLiteral("Cursor tracker: %1").arg(cursorTrackerStatus);
    lines << QStringLiteral("Global input listener: %1").arg(globalInputMonitor.listenerStatusText());
    lines << QStringLiteral("Global hotkeys: %1").arg(globalInputMonitor.hotkeyStatusText());
    lines << QStringLiteral("Recording backend mode: %1").arg(globalInputMonitor.recordingBackendText());
    lines << QStringLiteral("Selected monitor: %1").arg(display.displayId);
    lines << QStringLiteral("Monitor logical geometry: %1x%2 @ (%3, %4)")
                 .arg(display.logicalWidth)
                 .arg(display.logicalHeight)
                 .arg(display.offsetX)
                 .arg(display.offsetY);
    lines << QStringLiteral("Monitor scale: %1").arg(display.scale, 0, 'f', 2);

    if (backend) {
        lines << QStringLiteral("Playback backend status: %1").arg(backend->statusText());
        lines.append(backend->diagnosticLines());
    }

    lines.append(globalInputMonitor.diagnosticLines());
    lines.append(evdevInspector.diagnosticLines());

    for (const QString &warning : capabilities.warnings) {
        lines << QStringLiteral("Warning: %1").arg(warning);
    }

    return lines.join(QLatin1Char('\n'));
}

}
