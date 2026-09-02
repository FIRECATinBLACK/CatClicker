#include "PlaybackBackendSelector.h"

namespace CatClicker {

PlaybackBackendSelection PlaybackBackendSelector::select(const PortalCapabilities &capabilities,
                                                         const QString &desktopName,
                                                         InputSenderBackend *preferredEiBackend,
                                                         InputSenderBackend *uinputBackend)
{
    if (preferredEiBackend && preferredEiBackend->isAvailable()) {
        return {
            .backend = preferredEiBackend,
            .backendName = preferredEiBackend->backendName(),
            .reason = QStringLiteral("RemoteDesktop portal and a usable libei session are available."),
        };
    }

    if (uinputBackend && uinputBackend->isAvailable()) {
        const QString desktop = desktopName.isEmpty() ? QStringLiteral("this desktop") : desktopName;
        return {
            .backend = uinputBackend,
            .backendName = uinputBackend->backendName(),
            .reason = capabilities.remoteDesktopInterfaceAvailable
                ? QStringLiteral("RemoteDesktop/libei is not currently usable; using uinput fallback.")
                : QStringLiteral("RemoteDesktop portal unavailable on %1; using uinput fallback.").arg(desktop),
        };
    }

    return {
        .backend = nullptr,
        .backendName = QString(),
        .reason = QStringLiteral("No playback backend is currently usable. libei is unavailable and /dev/uinput is not accessible."),
    };
}

}
