#pragma once

#include "../macro/Macro.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace CatClicker {

struct PortalCapabilities {
    bool desktopPortalServiceAvailable = false;
    bool screenCastInterfaceAvailable = false;
    bool remoteDesktopInterfaceAvailable = false;
    bool globalShortcutsInterfaceAvailable = false;
    bool remoteDesktopConnectToEiAvailable = false;
    bool remoteDesktopUsableSession = false;
    uint screenCastVersion = 0;
    uint remoteDesktopVersion = 0;
    uint globalShortcutsVersion = 0;
    uint availableSourceTypes = 0;
    uint availableCursorModes = 0;
    QStringList warnings;
};

struct ScreenCastCursorModeSupport {
    bool hidden = false;
    bool embedded = false;
    bool metadata = false;
};

class PortalController : public QObject {
    Q_OBJECT

public:
    explicit PortalController(QObject *parent = nullptr);

    PortalCapabilities capabilities() const;
    MacroDisplayInfo currentDisplayInfo() const;
    QStringList diagnosticLines() const;
    static ScreenCastCursorModeSupport decodeCursorModes(uint bitmask);

private:
    PortalCapabilities m_capabilities;
    MacroDisplayInfo m_displayInfo;

    void probePortals();
    void probeDisplay();
    QString introspectionXml() const;
    bool hasInterfaceInXml(const QString &xml, const QString &interfaceName) const;
    bool hasMethodInXml(const QString &xml, const QString &interfaceName, const QString &methodName) const;
    uint readUintProperty(const QString &interfaceName, const QString &propertyName, bool *ok) const;
};

}
