#include "PortalController.h"

#include <QtCore/QProcessEnvironment>
#include <QtCore/QRegularExpression>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusVariant>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace CatClicker {

namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

}

PortalController::PortalController(QObject *parent)
    : QObject(parent)
{
    probePortals();
    probeDisplay();
}

PortalCapabilities PortalController::capabilities() const
{
    return m_capabilities;
}

MacroDisplayInfo PortalController::currentDisplayInfo() const
{
    return m_displayInfo;
}

QStringList PortalController::diagnosticLines() const
{
    const ScreenCastCursorModeSupport cursorModes = decodeCursorModes(m_capabilities.availableCursorModes);
    return {
        QStringLiteral("RemoteDesktop portal: %1").arg(m_capabilities.remoteDesktopInterfaceAvailable ? QStringLiteral("available") : QStringLiteral("unavailable")),
        QStringLiteral("RemoteDesktop version: %1").arg(m_capabilities.remoteDesktopVersion),
        QStringLiteral("ScreenCast portal: %1").arg(m_capabilities.screenCastInterfaceAvailable ? QStringLiteral("available") : QStringLiteral("unavailable")),
        QStringLiteral("ScreenCast version: %1").arg(m_capabilities.screenCastVersion),
        QStringLiteral("ScreenCast AvailableSourceTypes: 0x%1").arg(QString::number(m_capabilities.availableSourceTypes, 16)),
        QStringLiteral("ScreenCast AvailableCursorModes: 0x%1").arg(QString::number(m_capabilities.availableCursorModes, 16)),
        QStringLiteral("ScreenCast cursor modes:"),
        QStringLiteral("  Hidden: %1").arg(cursorModes.hidden ? QStringLiteral("yes") : QStringLiteral("no")),
        QStringLiteral("  Embedded: %1").arg(cursorModes.embedded ? QStringLiteral("yes") : QStringLiteral("no")),
        QStringLiteral("  Metadata: %1").arg(cursorModes.metadata ? QStringLiteral("yes") : QStringLiteral("no")),
        QStringLiteral("GlobalShortcuts portal: %1").arg(m_capabilities.globalShortcutsInterfaceAvailable ? QStringLiteral("available") : QStringLiteral("unavailable")),
        QStringLiteral("GlobalShortcuts version: %1").arg(m_capabilities.globalShortcutsVersion),
        QStringLiteral("ConnectToEIS method: %1").arg(m_capabilities.remoteDesktopConnectToEiAvailable ? QStringLiteral("present") : QStringLiteral("absent")),
    };
}

ScreenCastCursorModeSupport PortalController::decodeCursorModes(uint bitmask)
{
    return {
        .hidden = (bitmask & 0x1U) != 0,
        .embedded = (bitmask & 0x2U) != 0,
        .metadata = (bitmask & 0x4U) != 0,
    };
}

void PortalController::probePortals()
{
    QDBusInterface dbus(QStringLiteral("org.freedesktop.DBus"),
                        QStringLiteral("/org/freedesktop/DBus"),
                        QStringLiteral("org.freedesktop.DBus"),
                        QDBusConnection::sessionBus());
    const QDBusReply<QStringList> namesReply = dbus.call(QStringLiteral("ListNames"));
    if (!namesReply.isValid()) {
        m_capabilities.warnings.push_back(QStringLiteral("Failed to list D-Bus names on the session bus."));
        return;
    }

    m_capabilities.desktopPortalServiceAvailable = namesReply.value().contains(QString::fromLatin1(kPortalService));
    if (!m_capabilities.desktopPortalServiceAvailable) {
        m_capabilities.warnings.push_back(QStringLiteral("org.freedesktop.portal.Desktop is not present on the session bus."));
        return;
    }

    const QString xml = introspectionXml();
    if (xml.isEmpty()) {
        m_capabilities.warnings.push_back(QStringLiteral("Failed to introspect org.freedesktop.portal.Desktop."));
        return;
    }

    const QString screenCastInterface = QStringLiteral("org.freedesktop.portal.ScreenCast");
    const QString remoteDesktopInterface = QStringLiteral("org.freedesktop.portal.RemoteDesktop");
    const QString globalShortcutsInterface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");

    m_capabilities.screenCastInterfaceAvailable = hasInterfaceInXml(xml, screenCastInterface);
    m_capabilities.remoteDesktopInterfaceAvailable = hasInterfaceInXml(xml, remoteDesktopInterface);
    m_capabilities.globalShortcutsInterfaceAvailable = hasInterfaceInXml(xml, globalShortcutsInterface);
    m_capabilities.remoteDesktopConnectToEiAvailable = hasMethodInXml(xml, remoteDesktopInterface, QStringLiteral("ConnectToEIS"));

    bool ok = false;
    if (m_capabilities.screenCastInterfaceAvailable) {
        m_capabilities.screenCastVersion = readUintProperty(screenCastInterface, QStringLiteral("version"), &ok);
        if (!ok) {
            m_capabilities.warnings.push_back(QStringLiteral("ScreenCast version property could not be read."));
        }

        m_capabilities.availableSourceTypes = readUintProperty(screenCastInterface, QStringLiteral("AvailableSourceTypes"), &ok);
        if (!ok) {
            m_capabilities.warnings.push_back(QStringLiteral("ScreenCast AvailableSourceTypes could not be read."));
        }

        m_capabilities.availableCursorModes = readUintProperty(screenCastInterface, QStringLiteral("AvailableCursorModes"), &ok);
        if (!ok) {
            m_capabilities.warnings.push_back(QStringLiteral("ScreenCast AvailableCursorModes could not be read."));
        }
    }

    if (m_capabilities.remoteDesktopInterfaceAvailable) {
        m_capabilities.remoteDesktopVersion = readUintProperty(remoteDesktopInterface, QStringLiteral("version"), &ok);
        if (!ok) {
            m_capabilities.warnings.push_back(QStringLiteral("RemoteDesktop version property could not be read."));
        }
    }

    if (m_capabilities.globalShortcutsInterfaceAvailable) {
        m_capabilities.globalShortcutsVersion = readUintProperty(globalShortcutsInterface, QStringLiteral("version"), &ok);
        if (!ok) {
            m_capabilities.warnings.push_back(QStringLiteral("GlobalShortcuts version property could not be read."));
        }
    }
}

void PortalController::probeDisplay()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        m_displayInfo.displayId = QStringLiteral("No screen");
        return;
    }

    const QRect geometry = screen->geometry();
    m_displayInfo.displayId = screen->name();
    m_displayInfo.logicalWidth = geometry.width();
    m_displayInfo.logicalHeight = geometry.height();
    m_displayInfo.streamWidth = geometry.width();
    m_displayInfo.streamHeight = geometry.height();
    m_displayInfo.scale = screen->devicePixelRatio();
    m_displayInfo.offsetX = geometry.x();
    m_displayInfo.offsetY = geometry.y();
}

QString PortalController::introspectionXml() const
{
    QDBusInterface introspectable(QString::fromLatin1(kPortalService),
                                  QString::fromLatin1(kPortalPath),
                                  QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                  QDBusConnection::sessionBus());
    const QDBusReply<QString> reply = introspectable.call(QStringLiteral("Introspect"));
    return reply.isValid() ? reply.value() : QString();
}

bool PortalController::hasInterfaceInXml(const QString &xml, const QString &interfaceName) const
{
    return xml.contains(QStringLiteral("<interface name=\"%1\">").arg(interfaceName));
}

bool PortalController::hasMethodInXml(const QString &xml, const QString &interfaceName, const QString &methodName) const
{
    const QRegularExpression re(QStringLiteral("<interface name=\"%1\">([\\s\\S]*?)</interface>").arg(QRegularExpression::escape(interfaceName)));
    const QRegularExpressionMatch match = re.match(xml);
    if (!match.hasMatch()) {
        return false;
    }

    return match.captured(1).contains(QStringLiteral("<method name=\"%1\">").arg(methodName));
}

uint PortalController::readUintProperty(const QString &interfaceName, const QString &propertyName, bool *ok) const
{
    QDBusInterface props(QString::fromLatin1(kPortalService),
                         QString::fromLatin1(kPortalPath),
                         QString::fromLatin1(kPropertiesInterface),
                         QDBusConnection::sessionBus());
    const QDBusReply<QDBusVariant> reply = props.call(QStringLiteral("Get"), interfaceName, propertyName);
    if (!reply.isValid()) {
        if (ok) {
            *ok = false;
        }
        return 0;
    }

    bool converted = false;
    const uint value = reply.value().variant().toUInt(&converted);
    if (ok) {
        *ok = converted;
    }
    return converted ? value : 0;
}

}
