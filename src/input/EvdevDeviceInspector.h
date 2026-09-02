#pragma once

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

namespace CatClicker {

struct EvdevDeviceInfo {
    QString eventPath;
    QString sysfsName;
    QString displayName;
    QString category;
    QString permissionError;
    QString openErrorText;
    bool readable = false;
    bool openable = false;
    bool isPhysicalInputCandidate = false;
    bool isCatClickerVirtualDevice = false;
    bool hasKeyboardKeys = false;
    bool hasMouseButtons = false;
    bool hasRelativePointer = false;
    bool hasWheel = false;
    bool hasAbsoluteAxes = false;
    bool hasTouchpadFinger = false;
    quint16 vendorId = 0;
    quint16 productId = 0;
    int openErrorCode = 0;
};

class EvdevDeviceInspector {
public:
    EvdevDeviceInspector();

    bool buildHasLibevdev() const;
    bool hasAnyDeviceNodes() const;
    bool hasAnyReadablePhysicalInputDevices() const;
    bool hasUnreadablePhysicalInputDevices() const;
    QList<EvdevDeviceInfo> devices() const;
    QString summary() const;
    QStringList diagnosticLines() const;

private:
    QList<EvdevDeviceInfo> m_devices;

    void inspect();
};

}
