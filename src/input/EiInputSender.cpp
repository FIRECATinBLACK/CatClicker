#include "EiInputSender.h"

#include "BuildConfig.h"

namespace CatClicker {

EiInputSender::EiInputSender(QObject *parent)
    : InputSenderBackend(parent)
{
}

void EiInputSender::setPortalCapabilities(const PortalCapabilities &capabilities)
{
    m_portalCapabilities = capabilities;
}

QString EiInputSender::backendId() const
{
    return QStringLiteral("libei");
}

QString EiInputSender::backendName() const
{
    return QStringLiteral("RemoteDesktop + libei");
}

bool EiInputSender::initialize(const MacroDisplayInfo &)
{
    m_statusText = QStringLiteral("libei playback is not implemented in this COSMIC-focused milestone.");
    return false;
}

bool EiInputSender::isAvailable() const
{
    return CATCLICKER_HAS_LIBEI
        && m_portalCapabilities.remoteDesktopInterfaceAvailable
        && m_portalCapabilities.remoteDesktopConnectToEiAvailable
        && m_portalCapabilities.remoteDesktopUsableSession;
}

QString EiInputSender::statusText() const
{
    if (!CATCLICKER_HAS_LIBEI) {
        return QStringLiteral("libei library not detected at build time.");
    }

    if (!m_portalCapabilities.remoteDesktopInterfaceAvailable) {
        return QStringLiteral("RemoteDesktop portal interface is unavailable on this desktop.");
    }

    if (!m_portalCapabilities.remoteDesktopConnectToEiAvailable) {
        return QStringLiteral("RemoteDesktop portal does not advertise ConnectToEIS().");
    }

    if (!m_portalCapabilities.remoteDesktopUsableSession) {
        return QStringLiteral("A usable RemoteDesktop/libei session has not been established.");
    }

    return m_statusText;
}

QStringList EiInputSender::diagnosticLines() const
{
    return {
        QStringLiteral("libei library: %1").arg(CATCLICKER_HAS_LIBEI ? QStringLiteral("installed at build time") : QStringLiteral("missing at build time")),
        QStringLiteral("libei usable session: %1").arg(m_portalCapabilities.remoteDesktopUsableSession ? QStringLiteral("yes") : QStringLiteral("no")),
        QStringLiteral("Virtual held keys: 0"),
        QStringLiteral("Virtual held buttons: 0"),
    };
}

int EiInputSender::virtualHeldKeyCount() const
{
    return 0;
}

int EiInputSender::virtualHeldButtonCount() const
{
    return 0;
}

bool EiInputSender::sendKey(uint32_t, bool)
{
    return false;
}

bool EiInputSender::movePointerAbsolute(double, double)
{
    return false;
}

bool EiInputSender::sendButton(int, bool)
{
    return false;
}

bool EiInputSender::sendScroll(double, double)
{
    return false;
}

void EiInputSender::releaseEverything()
{
}

}
