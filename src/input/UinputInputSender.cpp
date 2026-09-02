#include "UinputInputSender.h"

#include "VirtualDeviceIdentity.h"

#include <QtCore/QDebug>
#include <QtCore/QThread>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <thread>
#include <unistd.h>

namespace CatClicker {

namespace {

bool traceUinputEventsEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("CATCLICKER_TRACE_UINPUT_EVENTS");
    return enabled;
}

QString describeUinputEvent(quint16 type, quint16 code, qint32 value)
{
    if (type == EV_SYN && code == SYN_REPORT) {
        return QStringLiteral("[uinput] EV_SYN SYN_REPORT %1").arg(value);
    }

    if (type == EV_KEY) {
        QString codeName;
        switch (code) {
        case BTN_LEFT:
            codeName = QStringLiteral("BTN_LEFT");
            break;
        case BTN_RIGHT:
            codeName = QStringLiteral("BTN_RIGHT");
            break;
        case BTN_MIDDLE:
            codeName = QStringLiteral("BTN_MIDDLE");
            break;
        case BTN_SIDE:
            codeName = QStringLiteral("BTN_SIDE");
            break;
        case BTN_EXTRA:
            codeName = QStringLiteral("BTN_EXTRA");
            break;
        default:
            codeName = QStringLiteral("code=%1").arg(code);
            break;
        }

        QString stateName;
        switch (value) {
        case 1:
            stateName = QStringLiteral("DOWN");
            break;
        case 0:
            stateName = QStringLiteral("UP");
            break;
        default:
            stateName = QStringLiteral("value=%1").arg(value);
            break;
        }

        return QStringLiteral("[uinput] EV_KEY %1 %2").arg(codeName, stateName);
    }

    return QStringLiteral("[uinput] type=%1 code=%2 value=%3").arg(type).arg(code).arg(value);
}

bool configureAbsAxis(UinputIo *io, int fd, unsigned short code, int maxValue)
{
#ifdef UI_ABS_SETUP
    uinput_abs_setup setup{};
    setup.code = code;
    setup.absinfo.minimum = 0;
    setup.absinfo.maximum = std::max(0, maxValue);
    setup.absinfo.value = 0;
    setup.absinfo.flat = 0;
    setup.absinfo.fuzz = 0;
    setup.absinfo.resolution = 1;
    return io->ioctlPtr(fd, UI_ABS_SETUP, &setup) == 0;
#else
    Q_UNUSED(io)
    Q_UNUSED(fd)
    Q_UNUSED(code)
    Q_UNUSED(maxValue)
    return false;
#endif
}

void fillSetup(uinput_setup *setup, const char *name, uint16_t vendorId, uint16_t productId)
{
    std::memset(setup, 0, sizeof(*setup));
    std::snprintf(setup->name, UINPUT_MAX_NAME_SIZE, "%s", name);
    setup->id.bustype = BUS_USB;
    setup->id.vendor = vendorId;
    setup->id.product = productId;
    setup->id.version = 1;
}

}

UinputInputSender::UinputInputSender(std::unique_ptr<UinputIo> io, QObject *parent)
    : InputSenderBackend(parent)
    , m_io(std::move(io))
{
}

UinputInputSender::~UinputInputSender()
{
    QMutexLocker locker(&m_mutex);
    destroyDevicesLocked();
}

QString UinputInputSender::backendId() const
{
    return QStringLiteral("uinput");
}

QString UinputInputSender::backendName() const
{
    return QStringLiteral("uinput absolute pointer");
}

bool UinputInputSender::initialize(const MacroDisplayInfo &display)
{
    QMutexLocker locker(&m_mutex);

    m_displayCount = QGuiApplication::screens().size();
    m_hasMappingWarning = m_displayCount > 1;

    const AvailabilityProbe probe = availabilityProbe();
    if (!probe.deviceNodeExists) {
        return setStatusLocked(QStringLiteral("/dev/uinput does not exist."));
    }

    if (!probe.openable) {
        if (probe.openErrorCode == EACCES) {
            return setStatusLocked(QStringLiteral("/dev/uinput exists but is not writable by the current user."));
        }
        return setStatusLocked(QStringLiteral("Failed to open /dev/uinput: %1").arg(probe.openErrorText));
    }

    if (!m_keyboard.ready && !createKeyboardDevice()) {
        return false;
    }

    if (!ensurePointerReadyForDisplayLocked(display)) {
        return false;
    }

    m_statusText = QStringLiteral("uinput virtual keyboard and absolute pointer ready.");
    return true;
}

bool UinputInputSender::isAvailable() const
{
    return availabilityProbe().openable;
}

UinputInputSender::AvailabilityProbe UinputInputSender::availabilityProbe() const
{
    AvailabilityProbe probe;
    probe.deviceNodeExists = m_io->exists(m_uinputPath);
    if (!probe.deviceNodeExists) {
        return probe;
    }

    int errorCode = 0;
    const int fd = m_io->openDevice(m_uinputPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC, &errorCode);
    if (fd >= 0) {
        probe.openable = true;
        m_io->closeDevice(fd);
        return probe;
    }

    probe.openErrorCode = errorCode;
    probe.openErrorText = QString::fromLocal8Bit(std::strerror(errorCode));
    return probe;
}

QString UinputInputSender::statusText() const
{
    QMutexLocker locker(&m_mutex);
    return m_statusText;
}

QStringList UinputInputSender::diagnosticLines() const
{
    QMutexLocker locker(&m_mutex);

    QStringList lines;
    const AvailabilityProbe probe = availabilityProbe();
    lines << QStringLiteral("/dev/uinput: %1")
                 .arg(!probe.deviceNodeExists
                          ? QStringLiteral("missing")
                          : (probe.openable
                                 ? QStringLiteral("openable")
                                 : QStringLiteral("not-openable")));
    if (probe.deviceNodeExists && !probe.openable && !probe.openErrorText.isEmpty()) {
        lines << QStringLiteral("/dev/uinput open error: %1 (%2)").arg(probe.openErrorText).arg(probe.openErrorCode);
    }
    lines << QStringLiteral("uinput virtual keyboard: %1").arg(m_keyboard.ready ? QStringLiteral("ready") : QStringLiteral("failed"));
    lines << QStringLiteral("uinput absolute pointer: %1").arg(m_pointer.ready ? QStringLiteral("ready") : QStringLiteral("failed"));
    lines << QStringLiteral("uinput device settle: keyboard=%1 pointer=%2")
                 .arg(m_keyboardSettled ? QStringLiteral("done") : QStringLiteral("pending"))
                 .arg(m_pointerSettled ? QStringLiteral("done") : QStringLiteral("pending"));
    lines << QStringLiteral("ABS pointer dimensions: %1 x %2").arg(m_display.logicalWidth).arg(m_display.logicalHeight);
    lines << QStringLiteral("Displays detected: %1").arg(m_displayCount);
    lines << QStringLiteral("Output mapping: %1")
                 .arg(m_hasMappingWarning ? QStringLiteral("current COSMIC limitation") : QStringLiteral("single-display safe path"));
    lines << QStringLiteral("Playback safety: %1")
                 .arg(m_hasMappingWarning ? QStringLiteral("warning") : QStringLiteral("normal"));
    lines << QStringLiteral("Virtual held keys: %1").arg(m_backendHeldKeys.size());
    lines << QStringLiteral("Virtual held buttons: %1").arg(m_backendHeldButtons.size());
    return lines;
}

int UinputInputSender::virtualHeldKeyCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_backendHeldKeys.size();
}

int UinputInputSender::virtualHeldButtonCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_backendHeldButtons.size();
}

bool UinputInputSender::sendKey(uint32_t keyCode, bool pressed)
{
    QMutexLocker locker(&m_mutex);
    return sendKeyLocked(keyCode, pressed, true);
}

bool UinputInputSender::movePointerAbsolute(double x, double y)
{
    QMutexLocker locker(&m_mutex);
    if (!m_pointer.ready) {
        return setStatusLocked(QStringLiteral("Virtual pointer is not ready."));
    }

    const int maxX = std::max(0, m_display.logicalWidth - 1);
    const int maxY = std::max(0, m_display.logicalHeight - 1);
    const int clampedX = clampAxis(x, maxX);
    const int clampedY = clampAxis(y, maxY);

    if (m_hasLastPointerPosition
        && clampedX == m_lastPointerX && clampedY == m_lastPointerY) {
        int primeX = clampedX;
        int primeY = clampedY;
        if (maxX > 0) {
            primeX = clampedX < maxX ? clampedX + 1 : clampedX - 1;
        } else if (maxY > 0) {
            primeY = clampedY < maxY ? clampedY + 1 : clampedY - 1;
        }
        if ((primeX != clampedX || primeY != clampedY)
            && !emitAbsolutePositionLocked(primeX, primeY)) {
            m_hasLastPointerPosition = false;
            return false;
        }
    }

    if (!emitAbsolutePositionLocked(clampedX, clampedY)) {
        m_hasLastPointerPosition = false;
        return false;
    }
    m_lastPointerX = clampedX;
    m_lastPointerY = clampedY;
    m_hasLastPointerPosition = true;
    return true;
}

bool UinputInputSender::sendButton(int button, bool pressed)
{
    QMutexLocker locker(&m_mutex);
    return sendButtonLocked(button, pressed, true);
}

bool UinputInputSender::sendScroll(double dx, double dy)
{
    QMutexLocker locker(&m_mutex);
    if (!m_pointer.ready) {
        return setStatusLocked(QStringLiteral("Virtual pointer is not ready."));
    }

    const int wheelX = static_cast<int>(std::lround(dx));
    const int wheelY = static_cast<int>(std::lround(dy));
    const int hiResX = static_cast<int>(std::lround(dx * 120.0));
    const int hiResY = static_cast<int>(std::lround(dy * 120.0));

    if (wheelX != 0 && !emitEventLocked(m_pointer.fd, EV_REL, REL_HWHEEL, wheelX)) {
        return false;
    }
    if (wheelY != 0 && !emitEventLocked(m_pointer.fd, EV_REL, REL_WHEEL, wheelY)) {
        return false;
    }
#ifdef REL_HWHEEL_HI_RES
    if (hiResX != 0 && !emitEventLocked(m_pointer.fd, EV_REL, REL_HWHEEL_HI_RES, hiResX)) {
        return false;
    }
#endif
#ifdef REL_WHEEL_HI_RES
    if (hiResY != 0 && !emitEventLocked(m_pointer.fd, EV_REL, REL_WHEEL_HI_RES, hiResY)) {
        return false;
    }
#endif
    return emitSynLocked(m_pointer.fd);
}

void UinputInputSender::releaseEverything()
{
    QMutexLocker locker(&m_mutex);

    const QList<uint32_t> heldKeys = m_backendHeldKeys.values();
    const QList<int> heldButtons = m_backendHeldButtons.values();

    for (uint32_t keyCode : heldKeys) {
        sendKeyLocked(keyCode, false, false);
    }

    for (int button : heldButtons) {
        sendButtonLocked(button, false, false);
    }
}

bool UinputInputSender::createKeyboardDevice()
{
    m_keyboard.fd = m_io->openDevice(m_uinputPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_keyboard.fd < 0) {
        return setStatusLocked(QStringLiteral("Failed to open /dev/uinput for the virtual keyboard."));
    }

    if (m_io->ioctlInt(m_keyboard.fd, UI_SET_EVBIT, EV_KEY) != 0) {
        destroyDeviceLocked(m_keyboard);
        return setStatusLocked(QStringLiteral("Failed to enable EV_KEY on the virtual keyboard."));
    }

    for (int keyCode = 1; keyCode <= KEY_MAX; ++keyCode) {
        if (m_io->ioctlInt(m_keyboard.fd, UI_SET_KEYBIT, static_cast<unsigned long>(keyCode)) != 0) {
            destroyDeviceLocked(m_keyboard);
            return setStatusLocked(QStringLiteral("Failed to enable keyboard keycode %1.").arg(keyCode));
        }
    }

    uinput_setup setup{};
    fillSetup(&setup, VirtualDeviceIdentity::KeyboardName, VirtualDeviceIdentity::VendorId, VirtualDeviceIdentity::KeyboardProductId);

    if (m_io->ioctlPtr(m_keyboard.fd, UI_DEV_SETUP, &setup) != 0
        || m_io->ioctlInt(m_keyboard.fd, UI_DEV_CREATE, 0) != 0) {
        destroyDeviceLocked(m_keyboard);
        return setStatusLocked(QStringLiteral("Failed to create the CatClicker virtual keyboard."));
    }

    m_keyboard.ready = true;
    return settleDeviceLocked(m_keyboard, &m_keyboardSettled, QStringLiteral("keyboard"));
}

bool UinputInputSender::createPointerDevice()
{
    m_pointer.fd = m_io->openDevice(m_uinputPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_pointer.fd < 0) {
        return setStatusLocked(QStringLiteral("Failed to open /dev/uinput for the virtual pointer."));
    }

    if (m_io->ioctlInt(m_pointer.fd, UI_SET_PROPBIT, INPUT_PROP_POINTER) != 0
        || m_io->ioctlInt(m_pointer.fd, UI_SET_EVBIT, EV_ABS) != 0
        || m_io->ioctlInt(m_pointer.fd, UI_SET_EVBIT, EV_KEY) != 0
        || m_io->ioctlInt(m_pointer.fd, UI_SET_EVBIT, EV_REL) != 0
        || m_io->ioctlInt(m_pointer.fd, UI_SET_ABSBIT, ABS_X) != 0
        || m_io->ioctlInt(m_pointer.fd, UI_SET_ABSBIT, ABS_Y) != 0) {
        destroyDeviceLocked(m_pointer);
        return setStatusLocked(QStringLiteral("Failed to enable pointer capabilities on the virtual pointer."));
    }

    const int buttonCodes[] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA};
    for (int buttonCode : buttonCodes) {
        if (m_io->ioctlInt(m_pointer.fd, UI_SET_KEYBIT, static_cast<unsigned long>(buttonCode)) != 0) {
            destroyDeviceLocked(m_pointer);
            return setStatusLocked(QStringLiteral("Failed to enable pointer button code %1.").arg(buttonCode));
        }
    }

    const int relCodes[] = {REL_WHEEL, REL_HWHEEL
#ifdef REL_WHEEL_HI_RES
                            ,
                            REL_WHEEL_HI_RES
#endif
#ifdef REL_HWHEEL_HI_RES
                            ,
                            REL_HWHEEL_HI_RES
#endif
    };
    for (int relCode : relCodes) {
        if (m_io->ioctlInt(m_pointer.fd, UI_SET_RELBIT, static_cast<unsigned long>(relCode)) != 0) {
            destroyDeviceLocked(m_pointer);
            return setStatusLocked(QStringLiteral("Failed to enable pointer scroll code %1.").arg(relCode));
        }
    }

    if (!configureAbsAxis(m_io.get(), m_pointer.fd, ABS_X, std::max(0, m_display.logicalWidth - 1))
        || !configureAbsAxis(m_io.get(), m_pointer.fd, ABS_Y, std::max(0, m_display.logicalHeight - 1))) {
        destroyDeviceLocked(m_pointer);
        return setStatusLocked(QStringLiteral("Failed to configure ABS_X/ABS_Y for the virtual pointer."));
    }

    uinput_setup setup{};
    fillSetup(&setup, VirtualDeviceIdentity::PointerName, VirtualDeviceIdentity::VendorId, VirtualDeviceIdentity::PointerProductId);

    if (m_io->ioctlPtr(m_pointer.fd, UI_DEV_SETUP, &setup) != 0
        || m_io->ioctlInt(m_pointer.fd, UI_DEV_CREATE, 0) != 0) {
        destroyDeviceLocked(m_pointer);
        return setStatusLocked(QStringLiteral("Failed to create the CatClicker virtual pointer."));
    }

    m_pointer.ready = true;
    // UI_ABS_SETUP initializes both axes to zero in the virtual device.
    m_lastPointerX = 0;
    m_lastPointerY = 0;
    m_hasLastPointerPosition = true;
    return settleDeviceLocked(m_pointer, &m_pointerSettled, QStringLiteral("pointer"));
}

void UinputInputSender::destroyDevicesLocked()
{
    destroyDeviceLocked(m_keyboard);
    destroyDeviceLocked(m_pointer);
}

void UinputInputSender::destroyDeviceLocked(DeviceState &device)
{
    if (device.fd >= 0) {
        m_io->ioctlInt(device.fd, UI_DEV_DESTROY, 0);
        m_io->closeDevice(device.fd);
    }

    device.fd = -1;
    device.ready = false;
    if (&device == &m_pointer) {
        m_hasLastPointerPosition = false;
    }
}

bool UinputInputSender::ensurePointerReadyForDisplayLocked(const MacroDisplayInfo &display)
{
    const bool needsRecreate = !m_pointer.ready
        || display.logicalWidth != m_display.logicalWidth
        || display.logicalHeight != m_display.logicalHeight;

    if (!needsRecreate) {
        return true;
    }

    destroyDeviceLocked(m_pointer);
    m_display = display;
    return createPointerDevice();
}

bool UinputInputSender::settleDeviceLocked(DeviceState &device, bool *settledFlag, const QString &deviceRole)
{
    if (!device.ready) {
        return false;
    }

    if (*settledFlag) {
        return true;
    }

    // uinput devices can miss the first event if userspace has not finished discovering them yet.
    // This bounded one-time settle happens only after device creation, never before every playback.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    *settledFlag = true;
    m_statusText = QStringLiteral("uinput virtual %1 created and settled.").arg(deviceRole);
    return true;
}

bool UinputInputSender::emitEventLocked(int fd, quint16 type, quint16 code, qint32 value)
{
    input_event event{};
    event.type = type;
    event.code = code;
    event.value = value;

    if (traceUinputEventsEnabled()) {
        QString description = describeUinputEvent(type, code, value);
        description.replace(QStringLiteral("[uinput] "),
                            QStringLiteral("[uinput] thread=%1 ")
                                .arg(QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16)));
        qInfo().noquote() << description;
    }

    if (m_io->writeData(fd, &event, sizeof(event)) != static_cast<qint64>(sizeof(event))) {
        return setStatusLocked(QStringLiteral("Failed to write a uinput event to /dev/uinput."));
    }

    return true;
}

bool UinputInputSender::emitSynLocked(int fd)
{
    return emitEventLocked(fd, EV_SYN, SYN_REPORT, 0);
}

bool UinputInputSender::emitWithSynLocked(int fd, quint16 type, quint16 code, qint32 value)
{
    if (!emitEventLocked(fd, type, code, value)) {
        return false;
    }

    return emitSynLocked(fd);
}

bool UinputInputSender::emitAbsolutePositionLocked(int x, int y)
{
    return emitEventLocked(m_pointer.fd, EV_ABS, ABS_X, x)
        && emitEventLocked(m_pointer.fd, EV_ABS, ABS_Y, y)
        && emitSynLocked(m_pointer.fd);
}

bool UinputInputSender::sendKeyLocked(uint32_t keyCode, bool pressed, bool trackState)
{
    if (!m_keyboard.ready) {
        return setStatusLocked(QStringLiteral("Virtual keyboard is not ready."));
    }

    const bool ok = emitWithSynLocked(m_keyboard.fd, EV_KEY, static_cast<quint16>(keyCode), pressed ? 1 : 0);
    if (ok) {
        if (trackState) {
            rememberKeyStateLocked(keyCode, pressed);
        } else if (!pressed) {
            m_backendHeldKeys.remove(keyCode);
        }
        return true;
    }

    if (!pressed) {
        // Release may have partially succeeded. Err on the side of treating the key as still held
        // so later cleanup attempts will try again.
        m_backendHeldKeys.insert(keyCode);
    }
    return false;
}

bool UinputInputSender::sendButtonLocked(int button, bool pressed, bool trackState)
{
    if (!m_pointer.ready) {
        return setStatusLocked(QStringLiteral("Virtual pointer is not ready."));
    }

    const int mapped = mapButtonCode(button);
    if (mapped == 0) {
        return setStatusLocked(QStringLiteral("Unsupported mouse button code %1.").arg(button));
    }

    const bool ok = emitWithSynLocked(m_pointer.fd, EV_KEY, static_cast<quint16>(mapped), pressed ? 1 : 0);
    if (ok) {
        if (trackState) {
            rememberButtonStateLocked(mapped, pressed);
        } else if (!pressed) {
            m_backendHeldButtons.remove(mapped);
        }
        return true;
    }

    if (!pressed) {
        m_backendHeldButtons.insert(mapped);
    }
    return false;
}

void UinputInputSender::rememberKeyStateLocked(uint32_t keyCode, bool pressed)
{
    if (pressed) {
        m_backendHeldKeys.insert(keyCode);
    } else {
        m_backendHeldKeys.remove(keyCode);
    }
}

void UinputInputSender::rememberButtonStateLocked(int button, bool pressed)
{
    if (pressed) {
        m_backendHeldButtons.insert(button);
    } else {
        m_backendHeldButtons.remove(button);
    }
}

bool UinputInputSender::setStatusLocked(const QString &status)
{
    m_statusText = status;
    return false;
}

int UinputInputSender::mapButtonCode(int button) const
{
    switch (button) {
    case BTN_LEFT:
    case 1:
        return BTN_LEFT;
    case BTN_RIGHT:
    case 2:
        return BTN_RIGHT;
    case BTN_MIDDLE:
    case 3:
        return BTN_MIDDLE;
    case BTN_SIDE:
    case 8:
        return BTN_SIDE;
    case BTN_EXTRA:
    case 9:
        return BTN_EXTRA;
    default:
        return 0;
    }
}

int UinputInputSender::clampAxis(double value, int maxValue) const
{
    const double rounded = std::round(value);
    const double clamped = std::clamp(rounded, 0.0, static_cast<double>(maxValue));
    if (clamped > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(clamped);
}

}
