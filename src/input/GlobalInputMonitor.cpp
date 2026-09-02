#include "GlobalInputMonitor.h"

#include <QtCore/QByteArray>
#include <QtCore/QMetaObject>
#include <QtCore/QDebug>
#include <QtCore/QThread>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <cstddef>
#include <chrono>
#include <cstring>

namespace CatClicker {

namespace {

class PosixGlobalInputBackend final : public GlobalInputBackend {
public:
    QList<EvdevDeviceInfo> discoverDevices() override
    {
        EvdevDeviceInspector inspector;
        return inspector.devices();
    }

    int openDevice(const QString &path, int flags, int *errorCode) override
    {
        const int fd = ::open(path.toLocal8Bit().constData(), flags);
        if (fd < 0 && errorCode) {
            *errorCode = errno;
        }
        return fd;
    }

    ssize_t readBytes(int fd, void *buffer, size_t maxBytes, int *errorCode) override
    {
        const ssize_t result = ::read(fd, buffer, maxBytes);
        if (result < 0 && errorCode) {
            *errorCode = errno;
        }
        return result;
    }

    int pollEvents(struct pollfd *fds, std::size_t count, int timeoutMs, int *errorCode) override
    {
        const int result = ::poll(fds, static_cast<nfds_t>(count), timeoutMs);
        if (result < 0 && errorCode) {
            *errorCode = errno;
        }
        return result;
    }

    bool createWakePipe(int pipeFds[2], int *errorCode) override
    {
#ifdef O_CLOEXEC
        if (::pipe2(pipeFds, O_CLOEXEC | O_NONBLOCK) == 0) {
            return true;
        }
#endif
        if (::pipe(pipeFds) != 0) {
            if (errorCode) {
                *errorCode = errno;
            }
            return false;
        }

        for (int i = 0; i < 2; ++i) {
            ::fcntl(pipeFds[i], F_SETFL, ::fcntl(pipeFds[i], F_GETFL, 0) | O_NONBLOCK);
            ::fcntl(pipeFds[i], F_SETFD, FD_CLOEXEC);
        }
        return true;
    }

    void closeFd(int fd) override
    {
        if (fd >= 0) {
            ::close(fd);
        }
    }

    bool writeWakeByte(int fd) override
    {
        if (fd < 0) {
            return false;
        }

        const char byte = '\n';
        return ::write(fd, &byte, 1) >= 0 || errno == EAGAIN;
    }

    qint64 monotonicTimeUs() const override
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    }
};

bool isPointerCategory(const QString &category)
{
    return category == QStringLiteral("mouse") || category == QStringLiteral("touchpad");
}

bool isMouseButtonCode(uint32_t code)
{
    return code >= BTN_MOUSE && code <= BTN_TASK;
}

bool isRecognizedKeyValue(qint32 value)
{
    return value == 0 || value == 1 || value == 2;
}

bool traceRecordingCursorEnabled()
{
    static const bool enabled = qEnvironmentVariableIntValue("CATCLICKER_TRACE_RECORDING_CURSOR") == 1;
    return enabled;
}

QString currentThreadId()
{
    return QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
}

}

std::unique_ptr<GlobalInputBackend> createPosixGlobalInputBackend()
{
    return std::make_unique<PosixGlobalInputBackend>();
}

GlobalInputMonitor::GlobalInputMonitor(std::unique_ptr<GlobalInputBackend> backend, QObject *parent)
    : QObject(parent)
    , m_backend(std::move(backend))
{
    qRegisterMetaType<GlobalInputEvent>();
    qRegisterMetaType<GlobalShortcutManager::ShortcutAction>();
}

GlobalInputMonitor::~GlobalInputMonitor()
{
    stopMonitoring();
}

void GlobalInputMonitor::setShortcutBindings(const GlobalShortcutManager::ShortcutBinding &record,
                                             const GlobalShortcutManager::ShortcutBinding &play,
                                             const GlobalShortcutManager::ShortcutBinding &stop)
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    m_recordBinding = record;
    m_playBinding = play;
    m_stopBinding = stop;
    rebuildRelevantShortcutKeysLocked();
}

bool GlobalInputMonitor::startMonitoring()
{
    bool shouldEmitAvailabilityChanged = false;
    bool started = false;
    {
        std::lock_guard<std::mutex> locker(m_stateMutex);
        if (m_running.load()) {
            return !m_openDevices.isEmpty();
        }

        refreshDevicesLocked();

        if (m_openDevices.isEmpty()) {
            shouldEmitAvailabilityChanged = true;
        } else {
            int pipeError = 0;
            if (!m_backend->createWakePipe(m_wakePipe, &pipeError)) {
                m_listenerStatusText = QStringLiteral("Global input unavailable: failed to create wake pipe (%1).")
                                           .arg(QString::fromLocal8Bit(std::strerror(pipeError)));
                rebuildDiagnosticStateLocked();
            } else {
                m_running.store(true);
                m_worker = std::thread([this]() {
                    workerMain();
                });
                started = true;
            }
            shouldEmitAvailabilityChanged = true;
        }
    }

    if (!shouldEmitAvailabilityChanged) {
        return started;
    }
    emit availabilityChanged();
    return started;
}

void GlobalInputMonitor::stopMonitoring()
{
    const bool wasRunning = m_running.exchange(false);
    if (wasRunning && m_backend) {
        m_backend->writeWakeByte(m_wakePipe[1]);
    }
    joinWorker();

    {
        std::lock_guard<std::mutex> locker(m_stateMutex);
        for (OpenDevice &device : m_openDevices) {
            m_backend->closeFd(device.fd);
            device.fd = -1;
        }
        m_openDevices.clear();
        m_pendingShortcutKeyEvents.clear();
        m_suppressedChords.clear();
        m_pressedKeys.clear();
        if (m_wakePipe[0] >= 0) {
            m_backend->closeFd(m_wakePipe[0]);
            m_wakePipe[0] = -1;
        }
        if (m_wakePipe[1] >= 0) {
            m_backend->closeFd(m_wakePipe[1]);
            m_wakePipe[1] = -1;
        }
        rebuildDiagnosticStateLocked();
    }
    emit availabilityChanged();
}

bool GlobalInputMonitor::isListenerActive() const
{
    return m_running.load();
}

bool GlobalInputMonitor::hasOpenablePhysicalDevices() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    return !m_openDevices.isEmpty();
}

bool GlobalInputMonitor::hasPermissionProblem() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    for (const EvdevDeviceInfo &device : m_devices) {
        if (device.isPhysicalInputCandidate && !device.isCatClickerVirtualDevice && device.openErrorCode == EACCES) {
            return true;
        }
    }
    return false;
}

bool GlobalInputMonitor::globalHotkeysActive() const
{
    return isListenerActive() && hasOpenablePhysicalDevices();
}

QString GlobalInputMonitor::listenerStatusText() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    return m_listenerStatusText;
}

QString GlobalInputMonitor::hotkeyStatusText() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    return m_hotkeyStatusText;
}

QString GlobalInputMonitor::recordingBackendText() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    return m_recordingBackendText;
}

int GlobalInputMonitor::keyboardNodeCount() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    return m_keyboardNodeCount;
}

int GlobalInputMonitor::pointerNodeCount() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    return m_pointerNodeCount;
}

int GlobalInputMonitor::openFailureCount() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    return m_openFailureCount;
}

qint64 GlobalInputMonitor::currentTimeUs() const
{
    return m_backend ? m_backend->monotonicTimeUs() : 0;
}

RelativeMotionHealth GlobalInputMonitor::relativeMotionHealthSnapshot() const
{
    std::lock_guard<std::mutex> locker(m_relativeMotionMutex);
    return {m_relativeAcceptedCount,
            m_relativeTraceDeliveryCount,
            m_relativeMotionDeliveryPosted,
            m_hasPendingRelativeMotion,
            m_lastRelativeAcceptedMonotonicUs,
            m_lastRelativeDeliveredMonotonicUs};
}

InputDeviceLifecycleHealth GlobalInputMonitor::inputDeviceLifecycleHealthSnapshot() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    InputDeviceLifecycleHealth health = m_lifecycleHealth;
    health.activeInputDevices = static_cast<quint64>(m_openDevices.size());
    for (const OpenDevice &device : m_openDevices) {
        health.activePointerDevices += device.pointer ? 1 : 0;
        health.activeKeyboardDevices += device.keyboard ? 1 : 0;
    }
    return health;
}

QStringList GlobalInputMonitor::diagnosticLines() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    return m_diagnosticLines;
}

QList<EvdevDeviceInfo> GlobalInputMonitor::devices() const
{
    std::lock_guard<std::mutex> locker(m_stateMutex);
    return m_devices;
}

void GlobalInputMonitor::setCaptureForwardingEnabled(bool enabled)
{
    m_captureForwardingEnabled.store(enabled);
    {
        std::lock_guard<std::mutex> locker(m_relativeMotionMutex);
        ++m_captureForwardingGeneration;
        m_hasPendingRelativeMotion = false;
        m_relativeMotionDeliveryPosted = false;
    }
}

void GlobalInputMonitor::refreshDevicesLocked()
{
    m_devices = m_backend->discoverDevices();
    m_openDevices.clear();
    m_keyboardNodeCount = 0;
    m_pointerNodeCount = 0;
    m_openFailureCount = 0;
    m_missingPointerDevices = 0;
    m_missingKeyboardDevices = 0;

    for (EvdevDeviceInfo &device : m_devices) {
        if (!device.isPhysicalInputCandidate || device.isCatClickerVirtualDevice) {
            continue;
        }

        int errorCode = 0;
        const int fd = m_backend->openDevice(device.eventPath, O_RDONLY | O_NONBLOCK | O_CLOEXEC, &errorCode);
        if (fd < 0) {
            device.openable = false;
            device.readable = false;
            device.openErrorCode = errorCode;
            device.openErrorText = QString::fromLocal8Bit(std::strerror(errorCode));
            if (errorCode == EACCES) {
                device.permissionError = device.openErrorText;
            }
            ++m_openFailureCount;
            continue;
        }

        device.openable = true;
        device.readable = true;
        device.openErrorCode = 0;
        device.openErrorText.clear();
        device.permissionError.clear();
        const bool pointer = isPointerCategory(device.category) || device.hasRelativePointer
            || device.hasMouseButtons || device.hasWheel;
        const bool keyboard = device.category == QStringLiteral("keyboard") || device.hasKeyboardKeys;
        OpenDevice openDevice{device.eventPath, device.category, fd, pointer, keyboard};
        m_openDevices.push_back(openDevice);
        if (keyboard) {
            ++m_keyboardNodeCount;
        }
        if (pointer) {
            ++m_pointerNodeCount;
        }
    }

    rebuildDiagnosticStateLocked();
}

bool GlobalInputMonitor::recoverMissingDevices()
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> locker(m_stateMutex);
        if (m_missingPointerDevices <= 0 && m_missingKeyboardDevices <= 0) {
            return false;
        }

        ++m_lifecycleHealth.rescans;
        QList<EvdevDeviceInfo> discovered = m_backend->discoverDevices();
        QSet<QString> openPaths;
        for (const OpenDevice &openDevice : std::as_const(m_openDevices)) {
            openPaths.insert(openDevice.path);
        }

        for (EvdevDeviceInfo &device : discovered) {
            if (!device.isPhysicalInputCandidate || device.isCatClickerVirtualDevice
                || openPaths.contains(device.eventPath)) {
                continue;
            }
            const bool pointer = isPointerCategory(device.category) || device.hasRelativePointer
                || device.hasMouseButtons || device.hasWheel;
            const bool keyboard = device.category == QStringLiteral("keyboard") || device.hasKeyboardKeys;
            if ((!pointer || m_missingPointerDevices <= 0)
                && (!keyboard || m_missingKeyboardDevices <= 0)) {
                continue;
            }

            int errorCode = 0;
            const int fd = m_backend->openDevice(device.eventPath, O_RDONLY | O_NONBLOCK | O_CLOEXEC, &errorCode);
            if (fd < 0) {
                continue;
            }
            m_openDevices.push_back({device.eventPath, device.category, fd, pointer, keyboard});
            openPaths.insert(device.eventPath);
            ++m_lifecycleHealth.devicesReopened;
            if (pointer) {
                ++m_lifecycleHealth.pointerDevicesReopened;
                m_missingPointerDevices = std::max(0, m_missingPointerDevices - 1);
            }
            if (keyboard) {
                m_missingKeyboardDevices = std::max(0, m_missingKeyboardDevices - 1);
            }
            changed = true;
        }
        m_devices = std::move(discovered);
        m_keyboardNodeCount = 0;
        m_pointerNodeCount = 0;
        for (const OpenDevice &device : std::as_const(m_openDevices)) {
            m_keyboardNodeCount += device.keyboard ? 1 : 0;
            m_pointerNodeCount += device.pointer ? 1 : 0;
        }
        rebuildDiagnosticStateLocked();
    }
    if (changed) {
        emit availabilityChanged();
    }
    return changed;
}

void GlobalInputMonitor::rebuildDiagnosticStateLocked()
{
    bool permissionProblem = false;
    for (const EvdevDeviceInfo &device : m_devices) {
        if (device.isPhysicalInputCandidate && !device.isCatClickerVirtualDevice && device.openErrorCode == EACCES) {
            permissionProblem = true;
            break;
        }
    }

    m_diagnosticLines.clear();
    m_diagnosticLines << QStringLiteral("Global input listener: %1")
                             .arg(m_openDevices.isEmpty() ? QStringLiteral("unavailable") : QStringLiteral("active"));
    m_diagnosticLines << QStringLiteral("Physical keyboard nodes: %1").arg(m_keyboardNodeCount);
    m_diagnosticLines << QStringLiteral("Physical pointer nodes: %1").arg(m_pointerNodeCount);
    m_diagnosticLines << QStringLiteral("Open failures: %1").arg(m_openFailureCount);

    if (m_openDevices.isEmpty()) {
        if (permissionProblem) {
            m_listenerStatusText = QStringLiteral("Global input unavailable: CatClicker needs permission to read physical input devices. Run the setup helper, then log out/in.");
        } else {
            m_listenerStatusText = QStringLiteral("Global input unavailable");
        }
        m_hotkeyStatusText = QStringLiteral("Application-only fallback");
        m_recordingBackendText = QStringLiteral("Qt focused fallback");
    } else {
        m_listenerStatusText = QStringLiteral("Global input listener active");
        m_hotkeyStatusText = QStringLiteral("Evdev global hotkeys active");
        m_recordingBackendText = QStringLiteral("Evdev global capture available");
    }
}

void GlobalInputMonitor::rebuildRelevantShortcutKeysLocked()
{
    m_shortcutRelevantKeys = m_recordBinding.relevantKeyCodes();
    m_shortcutRelevantKeys.unite(m_playBinding.relevantKeyCodes());
    m_shortcutRelevantKeys.unite(m_stopBinding.relevantKeyCodes());
}

void GlobalInputMonitor::joinWorker()
{
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void GlobalInputMonitor::workerMain()
{
    struct DeviceBuffer {
        QString path;
        QByteArray pendingBytes;
        bool droppingUntilSync = false;
    };

    QHash<int, DeviceBuffer> buffersByFd;
    while (m_running.load()) {
        recoverMissingDevices();
        QList<OpenDevice> devices;
        {
            std::lock_guard<std::mutex> locker(m_stateMutex);
            devices = m_openDevices;
        }

        std::vector<pollfd> pollFds;
        pollFds.reserve(static_cast<std::size_t>(devices.size()) + 1);
        pollFds.push_back({m_wakePipe[0], POLLIN, 0});
        for (const OpenDevice &device : devices) {
            pollFds.push_back({device.fd, POLLIN, 0});
        }

        int pollError = 0;
        const int ready = m_backend->pollEvents(pollFds.data(), pollFds.size(), 500, &pollError);
        if (ready < 0) {
            std::lock_guard<std::mutex> locker(m_stateMutex);
            if (pollError == EINTR) ++m_lifecycleHealth.pollEintr;
            else ++m_lifecycleHealth.pollOtherError;
            continue;
        }
        if (ready == 0) {
            continue;
        }

        if (pollFds.front().revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
            char buffer[32];
            int drainError = 0;
            while (m_backend->readBytes(m_wakePipe[0], buffer, sizeof(buffer), &drainError) > 0) {
            }
        }

        for (int i = 1; i < static_cast<int>(pollFds.size()); ++i) {
            const pollfd &pollFd = pollFds[static_cast<std::size_t>(i)];
            const OpenDevice &device = devices.at(i - 1);
            if (pollFd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                {
                    std::lock_guard<std::mutex> locker(m_stateMutex);
                    m_lifecycleHealth.devicePollErr += (pollFd.revents & POLLERR) ? 1 : 0;
                    m_lifecycleHealth.devicePollHup += (pollFd.revents & POLLHUP) ? 1 : 0;
                    m_lifecycleHealth.devicePollNval += (pollFd.revents & POLLNVAL) ? 1 : 0;
                }
                retireOpenDevice(pollFd.fd);
                buffersByFd.remove(pollFd.fd);
                continue;
            }

            if (!(pollFd.revents & POLLIN)) {
                continue;
            }
            {
                std::lock_guard<std::mutex> locker(m_stateMutex);
                m_lifecycleHealth.pointerPollReadable += device.pointer ? 1 : 0;
                m_lifecycleHealth.keyboardPollReadable += device.keyboard ? 1 : 0;
            }

            std::byte readBuffer[sizeof(input_event) * 16];
            int readError = 0;
            const ssize_t bytesRead = m_backend->readBytes(pollFd.fd,
                                                           readBuffer,
                                                           sizeof(readBuffer),
                                                           &readError);
            if (bytesRead == 0) {
                {
                    std::lock_guard<std::mutex> locker(m_stateMutex);
                    ++m_lifecycleHealth.deviceReadZero;
                }
                retireOpenDevice(pollFd.fd);
                buffersByFd.remove(pollFd.fd);
                continue;
            }
            if (bytesRead < 0) {
                {
                    std::lock_guard<std::mutex> locker(m_stateMutex);
                    if (readError == EAGAIN) ++m_lifecycleHealth.deviceReadEagain;
                    else if (readError == EINTR) ++m_lifecycleHealth.deviceReadEintr;
                    else if (readError == ENODEV) ++m_lifecycleHealth.deviceReadEnodev;
                    else if (readError == EIO) ++m_lifecycleHealth.deviceReadEio;
                    else ++m_lifecycleHealth.deviceReadOtherError;
                }
                if (readError == EAGAIN || readError == EINTR) {
                    continue;
                }
                retireOpenDevice(pollFd.fd);
                buffersByFd.remove(pollFd.fd);
                continue;
            }

            DeviceBuffer &bufferState = buffersByFd[pollFd.fd];
            if (bufferState.path.isEmpty()) {
                bufferState.path = devices.at(i - 1).path;
            }
            bufferState.pendingBytes.append(reinterpret_cast<const char *>(readBuffer),
                                            static_cast<qsizetype>(bytesRead));

            const qsizetype eventSize = static_cast<qsizetype>(sizeof(input_event));
            while (bufferState.pendingBytes.size() >= eventSize) {
                input_event event{};
                std::memcpy(&event, bufferState.pendingBytes.constData(), sizeof(event));
                bufferState.pendingBytes.remove(0, eventSize);
                {
                    std::lock_guard<std::mutex> locker(m_stateMutex);
                    m_lifecycleHealth.pointerEventsRead += device.pointer ? 1 : 0;
                    m_lifecycleHealth.pointerRelEventsRead += device.pointer && event.type == EV_REL ? 1 : 0;
                    m_lifecycleHealth.pointerButtonEventsRead += device.pointer && event.type == EV_KEY
                            && isMouseButtonCode(event.code) ? 1 : 0;
                    m_lifecycleHealth.keyboardEventsRead += device.keyboard && event.type == EV_KEY
                            && !isMouseButtonCode(event.code) ? 1 : 0;
                }
                if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                    bufferState.droppingUntilSync = true;
                    std::lock_guard<std::mutex> locker(m_stateMutex);
                    ++m_lifecycleHealth.synDropped;
                    continue;
                }
                if (bufferState.droppingUntilSync) {
                    if (event.type == EV_SYN && event.code == SYN_REPORT) {
                        bufferState.droppingUntilSync = false;
                        std::lock_guard<std::mutex> locker(m_stateMutex);
                        ++m_lifecycleHealth.syncRecoveries;
                    }
                    continue;
                }
                const qint64 timeUs = m_backend->monotonicTimeUs();
                processInputEvent(event, bufferState.path, timeUs);
            }
        }
    }
}

void GlobalInputMonitor::retireOpenDevice(int fd)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> locker(m_stateMutex);
        for (int i = 0; i < m_openDevices.size(); ++i) {
            if (m_openDevices.at(i).fd != fd) {
                continue;
            }

            const QString path = m_openDevices.at(i).path;
            const bool pointer = m_openDevices.at(i).pointer;
            const bool keyboard = m_openDevices.at(i).keyboard;
            m_backend->closeFd(fd);
            m_openDevices.removeAt(i);
            ++m_lifecycleHealth.devicesRemoved;
            if (pointer) {
                ++m_lifecycleHealth.pointerDevicesRemoved;
                ++m_missingPointerDevices;
            }
            if (keyboard) {
                ++m_missingKeyboardDevices;
            }
            for (EvdevDeviceInfo &device : m_devices) {
                if (device.eventPath == path) {
                    device.openable = false;
                    device.readable = false;
                    break;
                }
            }
            rebuildDiagnosticStateLocked();
            changed = true;
            break;
        }
    }

    if (changed) {
        emit availabilityChanged();
    }
}

void GlobalInputMonitor::publishShortcutAction(GlobalShortcutManager::ShortcutAction action)
{
    QMetaObject::invokeMethod(this, [this, action]() {
        emit globalShortcutTriggered(action);
    }, Qt::QueuedConnection);
}

void GlobalInputMonitor::publishRecordableEvent(const GlobalInputEvent &event)
{
    if (!m_captureForwardingEnabled.load()) {
        return;
    }

    if (event.type == GlobalInputEventType::RelativeMotion) {
        publishRelativeMotion(event);
        return;
    }

    quint64 generation = 0;
    {
        std::lock_guard<std::mutex> locker(m_relativeMotionMutex);
        generation = m_captureForwardingGeneration;
    }
    QMetaObject::invokeMethod(this, [this, event, generation]() {
        {
            std::lock_guard<std::mutex> locker(m_relativeMotionMutex);
            if (generation != m_captureForwardingGeneration
                || !m_captureForwardingEnabled.load()) {
                return;
            }
        }
        emit globalEventCaptured(event);
    }, Qt::QueuedConnection);
}

void GlobalInputMonitor::publishRelativeMotion(const GlobalInputEvent &event)
{
    bool postDelivery = false;
    quint64 generation = 0;
    {
        std::lock_guard<std::mutex> locker(m_relativeMotionMutex);
        generation = m_captureForwardingGeneration;
        m_pendingRelativeMotion = event;
        m_hasPendingRelativeMotion = true;
        ++m_relativeAcceptedCount;
        m_lastRelativeAcceptedMonotonicUs = event.timeUs;
        if (!m_relativeMotionDeliveryPosted) {
            m_relativeMotionDeliveryPosted = true;
            postDelivery = true;
        }
    }

    if (!postDelivery) {
        return;
    }

    const quint64 postNumber = m_relativeTracePostCount.fetch_add(1) + 1;
    if (traceRecordingCursorEnabled() && (postNumber == 1 || postNumber % 100 == 0)) {
        qInfo().noquote() << QStringLiteral("[global-input] thread=%1 REL delivery posted (subsequent REL events coalesce)")
                                 .arg(currentThreadId());
    }
    QMetaObject::invokeMethod(this, [this, generation]() {
        deliverPendingRelativeMotion(generation);
    }, Qt::QueuedConnection);
}

void GlobalInputMonitor::deliverPendingRelativeMotion(quint64 generation)
{
    GlobalInputEvent event;
    {
        std::lock_guard<std::mutex> locker(m_relativeMotionMutex);
        if (generation != m_captureForwardingGeneration) {
            return;
        }
        if (!m_hasPendingRelativeMotion || !m_captureForwardingEnabled.load()) {
            m_hasPendingRelativeMotion = false;
            m_relativeMotionDeliveryPosted = false;
            return;
        }
        event = m_pendingRelativeMotion;
        m_hasPendingRelativeMotion = false;
    }

    {
        std::lock_guard<std::mutex> locker(m_relativeMotionMutex);
        ++m_relativeTraceDeliveryCount;
        m_lastRelativeDeliveredMonotonicUs = event.timeUs;
    }
    if (traceRecordingCursorEnabled()
        && (m_relativeTraceDeliveryCount == 1 || m_relativeTraceDeliveryCount % 100 == 0)) {
        qInfo().noquote() << QStringLiteral("[global-input] thread=%1 deliver %2")
                                 .arg(currentThreadId(),
                                      event.code == REL_X ? QStringLiteral("REL_X") : QStringLiteral("REL_Y"));
    }
    emit globalEventCaptured(event);

    bool postAgain = false;
    {
        std::lock_guard<std::mutex> locker(m_relativeMotionMutex);
        if (generation == m_captureForwardingGeneration
            && m_hasPendingRelativeMotion && m_captureForwardingEnabled.load()) {
            postAgain = true;
        } else {
            m_relativeMotionDeliveryPosted = false;
        }
    }
    if (postAgain) {
        QMetaObject::invokeMethod(this, [this, generation]() {
            deliverPendingRelativeMotion(generation);
        }, Qt::QueuedConnection);
    }
}

void GlobalInputMonitor::processInputEvent(const input_event &event, const QString &devicePath, qint64 timeUs)
{
    switch (event.type) {
    case EV_KEY:
        if (!isRecognizedKeyValue(event.value)) {
            return;
        }
        processKeyLikeEvent(event.code,
                            event.value != 0,
                            event.value == 2,
                            isMouseButtonCode(event.code),
                            devicePath,
                            timeUs);
        break;
    case EV_REL:
        processRelativeEvent(event.code, event.value, devicePath, timeUs);
        break;
    default:
        break;
    }
}

void GlobalInputMonitor::processKeyLikeEvent(uint32_t code, bool pressed, bool autoRepeat, bool mouseButton, const QString &devicePath, qint64 timeUs)
{
    GlobalShortcutManager::ShortcutAction action = GlobalShortcutManager::ShortcutAction::None;
    bool suppressed = false;
    QList<GlobalInputEvent> flushableEvents;
    GlobalInputEvent outgoing;
    bool shouldEmitOutgoing = false;

    {
        std::lock_guard<std::mutex> locker(m_stateMutex);

        if (mouseButton) {
            outgoing.type = GlobalInputEventType::MouseButton;
            outgoing.code = code;
            outgoing.pressed = pressed;
            outgoing.timeUs = timeUs;
            outgoing.devicePath = devicePath;
            shouldEmitOutgoing = true;
        } else {
            if (autoRepeat || (pressed && m_pressedKeys.contains(code))) {
                return;
            }

            GlobalInputEvent keyEvent;
            keyEvent.type = GlobalInputEventType::Key;
            keyEvent.code = code;
            keyEvent.pressed = pressed;
            keyEvent.autoRepeat = autoRepeat;
            keyEvent.timeUs = timeUs;
            keyEvent.devicePath = devicePath;

            if (pressed) {
                m_pressedKeys.insert(code);
                if (m_shortcutRelevantKeys.contains(code)) {
                    m_pendingShortcutKeyEvents.push_back(keyEvent);
                    action = matchShortcutActionLocked(code);
                    if (action != GlobalShortcutManager::ShortcutAction::None) {
                        const QSet<uint32_t> chordKeys = pressedChordKeysForActionLocked(action);
                        for (int i = m_pendingShortcutKeyEvents.size() - 1; i >= 0; --i) {
                            if (chordKeys.contains(m_pendingShortcutKeyEvents.at(i).code)) {
                                m_pendingShortcutKeyEvents.removeAt(i);
                            }
                        }
                        m_suppressedChords.push_back({chordKeys});
                        suppressed = true;
                    }
                } else {
                    outgoing = keyEvent;
                    shouldEmitOutgoing = true;
                }
            } else {
                const bool wasSuppressed = isSuppressedKeyLocked(code);

                if (!wasSuppressed) {
                    if (m_shortcutRelevantKeys.contains(code)) {
                        m_pendingShortcutKeyEvents.push_back(keyEvent);
                    } else {
                        outgoing = keyEvent;
                        shouldEmitOutgoing = true;
                    }
                }

                m_pressedKeys.remove(code);

                if (wasSuppressed) {
                    removeReleasedSuppressionLocked(code);
                }

                flushableEvents = takeFlushablePendingKeyEventsLocked();
            }
        }
    }

    if (action != GlobalShortcutManager::ShortcutAction::None) {
        publishShortcutAction(action);
    }

    if (shouldEmitOutgoing && !suppressed) {
        publishRecordableEvent(outgoing);
    }

    for (const GlobalInputEvent &event : flushableEvents) {
        publishRecordableEvent(event);
    }
}

void GlobalInputMonitor::processRelativeEvent(quint16 code, qint32 value, const QString &devicePath, qint64 timeUs)
{
    GlobalInputEvent event;
    event.timeUs = timeUs;
    event.devicePath = devicePath;
    event.code = code;

    switch (code) {
    case REL_WHEEL:
        event.type = GlobalInputEventType::Scroll;
        event.deltaY = static_cast<double>(value);
        publishRecordableEvent(event);
        break;
    case REL_HWHEEL:
        event.type = GlobalInputEventType::Scroll;
        event.deltaX = static_cast<double>(value);
        publishRecordableEvent(event);
        break;
    case REL_X:
        event.type = GlobalInputEventType::RelativeMotion;
        event.deltaX = static_cast<double>(value);
        publishRecordableEvent(event);
        break;
    case REL_Y:
        event.type = GlobalInputEventType::RelativeMotion;
        event.deltaY = static_cast<double>(value);
        publishRecordableEvent(event);
        break;
    default:
        break;
    }
}

GlobalShortcutManager::ShortcutAction GlobalInputMonitor::matchShortcutActionLocked(uint32_t keyCode) const
{
    if (m_recordBinding.matchesPress(keyCode, m_pressedKeys)) {
        return GlobalShortcutManager::ShortcutAction::Record;
    }
    if (m_playBinding.matchesPress(keyCode, m_pressedKeys)) {
        return GlobalShortcutManager::ShortcutAction::Play;
    }
    if (m_stopBinding.matchesPress(keyCode, m_pressedKeys)) {
        return GlobalShortcutManager::ShortcutAction::Stop;
    }
    return GlobalShortcutManager::ShortcutAction::None;
}

QSet<uint32_t> GlobalInputMonitor::pressedChordKeysForActionLocked(GlobalShortcutManager::ShortcutAction action) const
{
    switch (action) {
    case GlobalShortcutManager::ShortcutAction::Record:
        return m_recordBinding.pressedChordKeys(m_pressedKeys);
    case GlobalShortcutManager::ShortcutAction::Play:
        return m_playBinding.pressedChordKeys(m_pressedKeys);
    case GlobalShortcutManager::ShortcutAction::Stop:
        return m_stopBinding.pressedChordKeys(m_pressedKeys);
    case GlobalShortcutManager::ShortcutAction::None:
        break;
    }
    return {};
}

bool GlobalInputMonitor::isSuppressedKeyLocked(uint32_t keyCode) const
{
    for (const SuppressedChord &chord : m_suppressedChords) {
        if (chord.activeKeys.contains(keyCode)) {
            return true;
        }
    }
    return false;
}

void GlobalInputMonitor::removeReleasedSuppressionLocked(uint32_t keyCode)
{
    for (int i = m_suppressedChords.size() - 1; i >= 0; --i) {
        m_suppressedChords[i].activeKeys.remove(keyCode);
        if (m_suppressedChords[i].activeKeys.isEmpty()) {
            m_suppressedChords.removeAt(i);
        }
    }
}

QList<GlobalInputEvent> GlobalInputMonitor::takeFlushablePendingKeyEventsLocked()
{
    QList<GlobalInputEvent> flushed;
    QList<GlobalInputEvent> stillPending;
    for (const GlobalInputEvent &event : std::as_const(m_pendingShortcutKeyEvents)) {
        if (m_pressedKeys.contains(event.code) || isSuppressedKeyLocked(event.code)) {
            stillPending.push_back(event);
        } else {
            flushed.push_back(event);
        }
    }
    m_pendingShortcutKeyEvents = stillPending;
    return flushed;
}

}
