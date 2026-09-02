#include "EvdevDeviceInspector.h"

#include "BuildConfig.h"
#include "VirtualDeviceIdentity.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>

#include <linux/input-event-codes.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace CatClicker {

namespace {

QString readTrimmedFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    return QString::fromUtf8(file.readAll()).trimmed();
}

quint64 parseHexWord(const QString &text)
{
    bool ok = false;
    return text.trimmed().toULongLong(&ok, 16);
}

QList<quint64> parseCapabilityWords(const QString &text)
{
    QList<quint64> words;
    const QStringList parts = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    words.reserve(parts.size());
    for (const QString &part : parts) {
        words.push_back(parseHexWord(part));
    }
    return words;
}

bool capabilityBitSet(const QList<quint64> &words, int bit)
{
    if (bit < 0) {
        return false;
    }

    const int wordIndexFromEnd = bit / 64;
    const int bitIndex = bit % 64;
    const int storageIndex = words.size() - 1 - wordIndexFromEnd;
    if (storageIndex < 0 || storageIndex >= words.size()) {
        return false;
    }

    return (words.at(storageIndex) & (1ULL << bitIndex)) != 0;
}

quint16 readHexIdFile(const QString &path)
{
    bool ok = false;
    const quint16 value = readTrimmedFile(path).toUShort(&ok, 16);
    return ok ? value : 0;
}

QString categorizeDevice(const EvdevDeviceInfo &device)
{
    if (device.isCatClickerVirtualDevice) {
        return QStringLiteral("catclicker-virtual");
    }

    if (device.hasRelativePointer && device.hasMouseButtons) {
        return QStringLiteral("mouse");
    }

    if (device.hasAbsoluteAxes && device.hasTouchpadFinger) {
        return QStringLiteral("touchpad");
    }

    if (device.hasKeyboardKeys) {
        return QStringLiteral("keyboard");
    }

    return QStringLiteral("other");
}

QString displayNameForDevice(const EvdevDeviceInfo &device)
{
    if (!device.displayName.isEmpty()) {
        return device.displayName;
    }

    if (!device.sysfsName.isEmpty()) {
        return device.sysfsName;
    }

    return device.eventPath;
}

void testOpen(EvdevDeviceInfo *device)
{
    if (!device) {
        return;
    }

    errno = 0;
    const int fd = ::open(device->eventPath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0) {
        device->openable = true;
        device->readable = true;
        ::close(fd);
        return;
    }

    device->openable = false;
    device->readable = false;
    device->openErrorCode = errno;
    device->openErrorText = QString::fromLocal8Bit(std::strerror(errno));
    if (errno == EACCES) {
        device->permissionError = device->openErrorText;
    }
}

}

EvdevDeviceInspector::EvdevDeviceInspector()
{
    inspect();
}

bool EvdevDeviceInspector::buildHasLibevdev() const
{
    return CATCLICKER_HAS_LIBEVDEV;
}

bool EvdevDeviceInspector::hasAnyDeviceNodes() const
{
    return !m_devices.isEmpty();
}

bool EvdevDeviceInspector::hasAnyReadablePhysicalInputDevices() const
{
    for (const EvdevDeviceInfo &device : m_devices) {
        if (device.isPhysicalInputCandidate && !device.isCatClickerVirtualDevice && device.readable) {
            if (!device.openable) {
                continue;
            }
            return true;
        }
    }

    return false;
}

bool EvdevDeviceInspector::hasUnreadablePhysicalInputDevices() const
{
    for (const EvdevDeviceInfo &device : m_devices) {
        if (device.isPhysicalInputCandidate && !device.isCatClickerVirtualDevice && !device.readable) {
            return true;
        }
    }

    return false;
}

QList<EvdevDeviceInfo> EvdevDeviceInspector::devices() const
{
    return m_devices;
}

QString EvdevDeviceInspector::summary() const
{
    if (m_devices.isEmpty()) {
        return QStringLiteral("No /dev/input/event* nodes were found.");
    }

    if (hasAnyReadablePhysicalInputDevices()) {
        return QStringLiteral("Physical input devices are present and readable for global evdev hotkeys and recording.");
    }

    if (hasUnreadablePhysicalInputDevices()) {
        return QStringLiteral("Global recording unavailable: physical input devices are not readable by this user.");
    }

    return QStringLiteral("Physical input nodes were found, but no readable keyboard/mouse candidates were identified.");
}

QStringList EvdevDeviceInspector::diagnosticLines() const
{
    QStringList lines;
    lines << QStringLiteral("libevdev build support: %1").arg(buildHasLibevdev() ? QStringLiteral("available") : QStringLiteral("unavailable"));
    lines << QStringLiteral("evdev device discovery summary: %1").arg(summary());

    for (const EvdevDeviceInfo &device : m_devices) {
        const QStringList flags = {
            device.readable ? QStringLiteral("readable") : QStringLiteral("not-readable"),
            device.openable ? QStringLiteral("openable") : QStringLiteral("not-openable"),
            device.isPhysicalInputCandidate ? QStringLiteral("physical-candidate") : QStringLiteral("non-candidate"),
            device.isCatClickerVirtualDevice ? QStringLiteral("catclicker-virtual") : QStringLiteral("not-catclicker-virtual"),
            device.hasKeyboardKeys ? QStringLiteral("keys") : QStringLiteral("no-keys"),
            device.hasMouseButtons ? QStringLiteral("mouse-buttons") : QStringLiteral("no-mouse-buttons"),
            device.hasRelativePointer ? QStringLiteral("rel-pointer") : QStringLiteral("no-rel-pointer"),
            device.hasWheel ? QStringLiteral("wheel") : QStringLiteral("no-wheel"),
            device.hasAbsoluteAxes ? QStringLiteral("abs-axes") : QStringLiteral("no-abs-axes"),
            device.hasTouchpadFinger ? QStringLiteral("touchpad-finger") : QStringLiteral("no-touchpad-finger")
        };

        lines << QStringLiteral("evdev %1: %2 [%3]")
                     .arg(device.eventPath,
                          displayNameForDevice(device),
                          device.category);
        lines << QStringLiteral("  flags: %1").arg(flags.join(QStringLiteral(", ")));
        lines << QStringLiteral("  vendor/product: 0x%1 / 0x%2")
                     .arg(QString::number(device.vendorId, 16),
                          QString::number(device.productId, 16));
        if (!device.permissionError.isEmpty()) {
            lines << QStringLiteral("  access: %1").arg(device.permissionError);
        } else if (!device.openErrorText.isEmpty()) {
            lines << QStringLiteral("  open: %1 (%2)").arg(device.openErrorText).arg(device.openErrorCode);
        }
    }

    return lines;
}

void EvdevDeviceInspector::inspect()
{
    m_devices.clear();

    QDir inputDir(QStringLiteral("/dev/input"));
    const QFileInfoList entries = inputDir.entryInfoList({QStringLiteral("event*")}, QDir::System | QDir::Files, QDir::Name);
    const QRegularExpression suffixRe(QStringLiteral("event\\d+$"));

    for (const QFileInfo &entry : entries) {
        EvdevDeviceInfo device;
        device.eventPath = entry.absoluteFilePath();
        device.readable = entry.isReadable();

        const QString eventName = entry.fileName();
        if (!suffixRe.match(eventName).hasMatch()) {
            m_devices.push_back(device);
            continue;
        }

        const QString sysBase = QStringLiteral("/sys/class/input/%1/device").arg(eventName);
        device.sysfsName = readTrimmedFile(sysBase + QStringLiteral("/name"));
        device.vendorId = readHexIdFile(sysBase + QStringLiteral("/id/vendor"));
        device.productId = readHexIdFile(sysBase + QStringLiteral("/id/product"));
        device.displayName = device.sysfsName;
        device.isCatClickerVirtualDevice =
            VirtualDeviceIdentity::isCatClickerVirtualDevice(device.sysfsName, device.vendorId, device.productId)
            || VirtualDeviceIdentity::isCatClickerVirtualDeviceName(device.sysfsName);

        const QList<quint64> evBits = parseCapabilityWords(readTrimmedFile(sysBase + QStringLiteral("/capabilities/ev")));
        const QList<quint64> keyBits = parseCapabilityWords(readTrimmedFile(sysBase + QStringLiteral("/capabilities/key")));
        const QList<quint64> relBits = parseCapabilityWords(readTrimmedFile(sysBase + QStringLiteral("/capabilities/rel")));
        const QList<quint64> absBits = parseCapabilityWords(readTrimmedFile(sysBase + QStringLiteral("/capabilities/abs")));

        const bool supportsKeys = capabilityBitSet(evBits, EV_KEY);
        const bool supportsRel = capabilityBitSet(evBits, EV_REL);
        const bool supportsAbs = capabilityBitSet(evBits, EV_ABS);

        device.hasKeyboardKeys = supportsKeys
                                 && capabilityBitSet(keyBits, KEY_A)
                                 && capabilityBitSet(keyBits, KEY_SPACE);
        device.hasMouseButtons = supportsKeys && capabilityBitSet(keyBits, BTN_MOUSE);
        device.hasRelativePointer = supportsRel
                                    && capabilityBitSet(relBits, REL_X)
                                    && capabilityBitSet(relBits, REL_Y);
        device.hasWheel = supportsRel && capabilityBitSet(relBits, REL_WHEEL);
        device.hasAbsoluteAxes = supportsAbs
                                 && capabilityBitSet(absBits, ABS_X)
                                 && capabilityBitSet(absBits, ABS_Y);
        device.hasTouchpadFinger = supportsKeys && capabilityBitSet(keyBits, BTN_TOOL_FINGER);
        device.category = categorizeDevice(device);
        device.isPhysicalInputCandidate =
            device.category == QStringLiteral("keyboard")
            || device.category == QStringLiteral("mouse")
            || device.category == QStringLiteral("touchpad");

        testOpen(&device);

        m_devices.push_back(device);
    }
}

}
