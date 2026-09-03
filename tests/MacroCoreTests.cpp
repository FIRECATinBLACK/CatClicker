#include "../src/input/InputSenderBackend.h"
#include "../src/input/FileChooserPortal.h"
#include "../src/input/PlaybackBackendSelector.h"
#include "../src/input/PortalController.h"
#include "../src/input/EvdevCaptureBackend.h"
#include "../src/input/CosmicCursorPositionProvider.h"
#include "../src/input/GlobalInputEvent.h"
#include "../src/input/GlobalInputMonitor.h"
#include "../src/input/QtFocusedCaptureBackend.h"
#include "../src/app/PlaybackLoopController.h"
#include "../src/app/SingleInstanceCoordinator.h"
#include "../src/diagnostics/Diagnostics.h"
#include "../src/input/UinputInputSender.h"
#include "../src/input/UinputIo.h"
#include "../src/input/VirtualDeviceIdentity.h"
#include "../src/hotkeys/GlobalShortcutManager.h"
#include "../src/macro/Macro.h"
#include "../src/macro/MacroPlayer.h"
#include "../src/macro/MacroRecorder.h"
#include "../src/persistence/MacroSerializer.h"
#include "../src/persistence/Settings.h"
#include "BuildConfig.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QHash>
#include <QtCore/QTemporaryDir>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeySequence>
#include <QtGui/QMouseEvent>
#include <QtGui/QPointingDevice>
#include <QtGui/QWheelEvent>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <linux/input.h>
#include <poll.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>

using namespace CatClicker;

class MockInputSenderBackend final : public InputSenderBackend {
    Q_OBJECT

public:
    struct Call {
        QString type;
        int a = 0;
        int b = 0;
        double x = 0.0;
        double y = 0.0;

        bool operator==(const Call &) const = default;
    };

    bool available = true;
    bool initializeOk = true;
    int failOnCallIndex = -1;
    QStringList lines;
    QVector<Call> calls;
    int sendCount = 0;

    explicit MockInputSenderBackend(QObject *parent = nullptr)
        : InputSenderBackend(parent)
    {
    }

    QString backendId() const override { return QStringLiteral("mock"); }
    QString backendName() const override { return QStringLiteral("Mock Backend"); }
    bool initialize(const MacroDisplayInfo &) override { return initializeOk; }
    bool isAvailable() const override { return available; }
    QString statusText() const override { return QStringLiteral("mock"); }
    QStringList diagnosticLines() const override { return lines; }
    int virtualHeldKeyCount() const override { return 0; }
    int virtualHeldButtonCount() const override { return 0; }

    bool sendKey(uint32_t keyCode, bool pressed) override
    {
        calls.push_back({QStringLiteral("key"), static_cast<int>(keyCode), pressed ? 1 : 0});
        return failOnCallIndex < 0 || sendCount++ != failOnCallIndex;
    }

    bool movePointerAbsolute(double x, double y) override
    {
        calls.push_back({QStringLiteral("move"), 0, 0, x, y});
        return failOnCallIndex < 0 || sendCount++ != failOnCallIndex;
    }

    bool sendButton(int button, bool pressed) override
    {
        calls.push_back({QStringLiteral("button"), button, pressed ? 1 : 0});
        return failOnCallIndex < 0 || sendCount++ != failOnCallIndex;
    }

    bool sendScroll(double dx, double dy) override
    {
        calls.push_back({QStringLiteral("scroll"), 0, 0, dx, dy});
        return failOnCallIndex < 0 || sendCount++ != failOnCallIndex;
    }

    void releaseEverything() override
    {
        calls.push_back({QStringLiteral("releaseEverything")});
    }
};

class MockUinputIo final : public UinputIo {
public:
    struct EventPattern {
        quint16 type = 0;
        quint16 code = 0;
        qint32 value = 0;
    };

    bool pathExists = true;
    bool pathAccessible = true;
    int nextFd = 10;
    int failMatchingEventWrites = 0;
    QVector<input_event> writtenEvents;
    EventPattern failingEvent;

    bool exists(const QString &) const override
    {
        return pathExists;
    }

    bool canAccess(const QString &, int) const override
    {
        return pathAccessible;
    }

    int openDevice(const QString &, int, int *errorCode) override
    {
        if (errorCode) {
            *errorCode = 0;
        }
        return nextFd++;
    }

    int ioctlInt(int, unsigned long, unsigned long) override
    {
        return 0;
    }

    int ioctlPtr(int, unsigned long, void *) override
    {
        return 0;
    }

    qint64 writeData(int, const void *data, std::size_t size) override
    {
        if (size == sizeof(input_event)) {
            const auto *event = static_cast<const input_event *>(data);
            writtenEvents.push_back(*event);
            if (failMatchingEventWrites > 0
                && event->type == failingEvent.type
                && event->code == failingEvent.code
                && event->value == failingEvent.value) {
                --failMatchingEventWrites;
                return -1;
            }
        }

        return static_cast<qint64>(size);
    }

    int closeDevice(int) override
    {
        return 0;
    }
};

class FakeGlobalInputBackend final : public GlobalInputBackend {
public:
    QList<EvdevDeviceInfo> discoveredDevices;
    QHash<QString, int> openErrorsByPath;
    QList<QString> openedPaths;
    QList<int> closedFds;

    QList<EvdevDeviceInfo> discoverDevices() override
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        return discoveredDevices;
    }

    int openDevice(const QString &path, int, int *errorCode) override
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        openedPaths.push_back(path);
        if (openErrorsByPath.contains(path)) {
            if (errorCode) {
                *errorCode = openErrorsByPath.value(path);
            }
            return -1;
        }

        const int fd = m_nextFd++;
        m_pathToFd.insert(path, fd);
        m_fdToPath.insert(fd, path);
        auto pending = m_pendingBytesByPath.find(path);
        if (pending != m_pendingBytesByPath.end()) {
            m_bytesByFd[fd].append(pending.value());
            m_pendingBytesByPath.erase(pending);
        }
        m_cv.notify_all();
        return fd;
    }

    ssize_t readBytes(int fd, void *buffer, size_t maxBytes, int *errorCode) override
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        if (m_forcedReadErrors.contains(fd)) {
            if (errorCode) {
                *errorCode = m_forcedReadErrors.take(fd);
            }
            return -1;
        }
        QByteArray *source = byteQueueForFdLocked(fd);
        if (!source) {
            if (errorCode) {
                *errorCode = EBADF;
            }
            return -1;
        }
        if (source->isEmpty()) {
            if (errorCode) {
                *errorCode = EAGAIN;
            }
            return -1;
        }

        const qsizetype chunkSize = std::min<qsizetype>(source->size(), static_cast<qsizetype>(maxBytes));
        std::memcpy(buffer, source->constData(), static_cast<size_t>(chunkSize));
        source->remove(0, chunkSize);
        return chunkSize;
    }

    int pollEvents(struct pollfd *fds, std::size_t count, int timeoutMs, int *errorCode) override
    {
        std::unique_lock<std::mutex> locker(m_mutex);
        const auto hasReadyFd = [&]() {
            for (std::size_t i = 0; i < count; ++i) {
                if (hasReadableBytesLocked(fds[i].fd) || m_forcedRevents.value(fds[i].fd) != 0) {
                    return true;
                }
            }
            return false;
        };

        if (!hasReadyFd()) {
            m_cv.wait_for(locker, std::chrono::milliseconds(timeoutMs), hasReadyFd);
        }

        int readyCount = 0;
        for (std::size_t i = 0; i < count; ++i) {
            fds[i].revents = 0;
            if (m_closedFdSet.contains(fds[i].fd)) {
                fds[i].revents |= POLLNVAL;
            } else if (hasReadableBytesLocked(fds[i].fd)) {
                fds[i].revents |= POLLIN;
            }
            fds[i].revents |= m_forcedRevents.take(fds[i].fd);
            if (fds[i].revents != 0) {
                ++readyCount;
            }
        }

        if (readyCount == 0 && errorCode) {
            *errorCode = 0;
        }
        return readyCount;
    }

    bool createWakePipe(int pipeFds[2], int *) override
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        m_wakeReadFd = m_nextFd++;
        m_wakeWriteFd = m_nextFd++;
        pipeFds[0] = m_wakeReadFd;
        pipeFds[1] = m_wakeWriteFd;
        return true;
    }

    void closeFd(int fd) override
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        closedFds.push_back(fd);
        m_closedFdSet.insert(fd);
        m_bytesByFd.remove(fd);
        if (m_wakeReadFd == fd) {
            m_wakeReadFd = -1;
        }
        if (m_wakeWriteFd == fd) {
            m_wakeWriteFd = -1;
        }
        m_cv.notify_all();
    }

    bool writeWakeByte(int fd) override
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        if (fd != m_wakeWriteFd || m_wakeReadFd < 0) {
            return false;
        }
        m_wakeBytes.append('\n');
        m_cv.notify_all();
        return true;
    }

    qint64 monotonicTimeUs() const override
    {
        return m_timeUs.fetch_add(1000) + 1000;
    }

    void queueInputEvent(const QString &path, const input_event &event)
    {
        queueBytes(path, QByteArray(reinterpret_cast<const char *>(&event), static_cast<qsizetype>(sizeof(event))));
    }

    void queuePartialInputEvent(const QString &path, const input_event &event, qsizetype firstChunkSize)
    {
        const QByteArray bytes(reinterpret_cast<const char *>(&event), static_cast<qsizetype>(sizeof(event)));
        queueBytes(path, bytes.left(firstChunkSize));
        queueBytes(path, bytes.mid(firstChunkSize));
    }

    void queuePollCondition(const QString &path, short condition)
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        const int fd = m_pathToFd.value(path, -1);
        if (fd >= 0) {
            m_forcedRevents[fd] |= condition;
        }
        m_cv.notify_all();
    }

    void queueReadError(const QString &path, int errorCode)
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        const int fd = m_pathToFd.value(path, -1);
        if (fd >= 0) {
            m_forcedReadErrors.insert(fd, errorCode);
            m_forcedRevents[fd] |= POLLIN;
        }
        m_cv.notify_all();
    }

    void setDiscoveredDevices(const QList<EvdevDeviceInfo> &devices)
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        discoveredDevices = devices;
        m_cv.notify_all();
    }

private:
    QByteArray *byteQueueForFdLocked(int fd)
    {
        if (fd == m_wakeReadFd) {
            return &m_wakeBytes;
        }
        auto it = m_bytesByFd.find(fd);
        return it == m_bytesByFd.end() ? nullptr : &it.value();
    }

    bool hasReadableBytesLocked(int fd) const
    {
        if (fd == m_wakeReadFd) {
            return !m_wakeBytes.isEmpty();
        }
        auto it = m_bytesByFd.find(fd);
        return it != m_bytesByFd.end() && !it.value().isEmpty();
    }

    void queueBytes(const QString &path, const QByteArray &bytes)
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        if (const int fd = m_pathToFd.value(path, -1); fd >= 0) {
            m_bytesByFd[fd].append(bytes);
        } else {
            m_pendingBytesByPath[path].append(bytes);
        }
        m_cv.notify_all();
    }

    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    mutable std::atomic<qint64> m_timeUs = 0;
    int m_nextFd = 100;
    int m_wakeReadFd = -1;
    int m_wakeWriteFd = -1;
    QHash<QString, int> m_pathToFd;
    QHash<int, QString> m_fdToPath;
    QHash<QString, QByteArray> m_pendingBytesByPath;
    QHash<int, QByteArray> m_bytesByFd;
    QByteArray m_wakeBytes;
    QSet<int> m_closedFdSet;
    QHash<int, short> m_forcedRevents;
    QHash<int, int> m_forcedReadErrors;
};

class FakeCursorPositionProvider final : public CursorPositionProvider {
public:
    bool hasPosition = false;
    QPointF position;

    CursorSnapshot cursorSnapshot() const override
    {
        return {hasPosition, position};
    }
};

class ThreadSafeFakeCursorPositionProvider final : public CursorPositionProvider {
public:
    void setPosition(const QPointF &position)
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        m_snapshot = {true, position};
    }

    CursorSnapshot cursorSnapshot() const override
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        return m_snapshot;
    }

private:
    mutable std::mutex m_mutex;
    CursorSnapshot m_snapshot;
};

class MacroCoreTests : public QObject {
    Q_OBJECT

private slots:
    void serializationRoundTrip();
    void chronologicalOrdering();
    void timestampScaling();
    void smoothingScheduleInterpolatesOnlyAdjacentMoves();
    void smoothingDoesNotCrossMouseSemantics();
    void unanchoredMousePlaybackIsDropped();
    void anchoredMousePlaybackUsesExactCoordinates();
    void repeatedPlaybackReachesFinalEventWithAndWithoutSmoothing();
    void longMouseHoldPreservesTimingAndAnchors();
    void dragPlaybackPreservesOrderingAndAnchors();
    void heldKeyCleanup();
    void heldButtonCleanup();
    void corruptedMacroRejected();
    void hostileMacroInputRemainsDataOrIsRejected();
    void monitorCompatibility();
    void buttonAnchorEnforcement();
    void completionReleases();
    void errorReleases();
    void cancellationReleases();
    void stopWakeupIsImmediate();
    void stopReportsUserCancellation();
    void keycodePassThrough();
    void playbackEventOrdering();
    void backendSelection();
    void cursorModeDecoding();
    void cosmicCursorStateRequiresEnterAndPosition();
    void cosmicCursorStateLeaveAndStopInvalidate();
    void cosmicCursorMappingScaleOne();
    void cosmicCursorMappingScaled();
    void cosmicCursorMappingLogicalOrigin();
    void cosmicCursorMappingRejectsTransform();
    void cosmicCursorSnapshotIsCoherentAcrossThreads();
    void cosmicCursorHealthTracksCallbacksAndPublications();
    void cosmicProviderLeaveInvalidatesPublishedPosition();
    void cosmicProviderRefreshRequiresNewGenerationPosition();
    void cosmicProviderAllowsOnlyOneOutstandingRefresh();
    void cosmicProviderSelectionPolicy();
    void workerShutdownIsIdempotent();
    void virtualDeviceIdentity();
    void shortcutNormalizationRejectsModifierOnly();
    void stopShortcutAddsModifierSupersets();
    void uinputHeldKeyBookkeeping();
    void uinputHeldButtonBookkeeping();
    void uinputReleaseEverythingClearsHeldCounts();
    void uinputFailedReleaseRetainsHeldCounts();
    void uinputRetryAfterFailedReleaseClearsHeldCounts();
    void uinputMouseCompletionEventSequence();
    void uinputMouseStopEventSequence();
    void uinputRepeatedAbsoluteTargetIsForcedSafely();
    void qtFocusedCaptureBackendRecordsFocusedInput();
    void qtFocusedCaptureBackendSuppressesTriggerShortcut();
    void qtFocusedCaptureBackendSynthesizesReleaseOnStop();
    void globalInputMonitorExcludesCatClickerVirtualDevices();
    void globalInputMonitorOpensPhysicalDevices();
    void globalInputMonitorReportsPermissionErrors();
    void globalInputMonitorPublishesKeyEvents();
    void globalInputMonitorPublishesMouseButtons();
    void globalInputMonitorPublishesVerticalWheel();
    void globalInputMonitorPublishesHorizontalWheel();
    void globalInputMonitorPublishesRelativeX();
    void globalInputMonitorPublishesRelativeY();
    void globalInputMonitorCoalescesRelativeFloodWithoutLosingButton();
    void globalInputMonitorMixedInputRemainsLive();
    void globalInputMonitorReopensPointerAfterPollLoss();
    void globalInputMonitorKeepsPointerAfterRecoverableReadError();
    void globalInputMonitorSynDroppedSuppressesUntilReport();
    void evdevCaptureBackendTranslatesMouseButtons();
    void evdevCaptureBackendSerializesVerticalWheel();
    void evdevCaptureBackendSerializesHorizontalWheel();
    void evdevCaptureBackendUsesAbsoluteCursorPositionForMotion();
    void evdevCaptureBackendUsesAbsoluteCursorPositionForRelativeY();
    void evdevCaptureBackendSamplesLiveProviderChanges();
    void evdevCaptureBackendSamplesFourDistinctClickAnchors();
    void cursorUpdatesProgressDuringContinuousRelativeFlood();
    void recordingLifecycleSamplesProviderAfterCaptureStarts();
    void recordingDoesNotUsePositionInvalidatedByLeaveOrRefresh();
    void cosmicSamplerCoalescesRelAndDeliversGenerations();
    void cosmicSamplerDefersMouseButNotKeyboard();
    void cosmicSamplerStationaryClickAndStopAreSafe();
    void cosmicSamplerSupportsThreeRecordingsOnSameBackend();
    void cosmicSamplerIgnoresLateCompletionAndRecoversNextRecording();
    void cosmicSamplerDoesNotStallMidRecording();
    void cosmicSamplerSuppressesStaleCoordinatesAndRecovers();
    void cosmicSamplerHardRefreshRecoversWithGenerationIsolation();
    void cosmicSamplerHardRefreshNeverFabricatesAnchors();
    void cosmicSamplerHardRefreshIsRateLimited();
    void cosmicSamplerLongActiveSessionKeepsProgressing();
    void evdevCaptureBackendDeduplicatesResolvedCursorPosition();
    void evdevCaptureBackendUsesAbsoluteCursorAnchorForClicks();
    void evdevCaptureBackendLeavesClickUnanchoredWithoutPosition();
    void evdevCaptureBackendUsesAbsoluteCursorAnchorForScroll();
    void evdevCaptureBackendIgnoresRelativeMotion();
    void globalShortcutExactMatch();
    void globalShortcutAcceptsLeftRightModifiers();
    void globalShortcutIgnoresAutorepeat();
    void globalShortcutSuppressesStartRecordChord();
    void globalShortcutSuppressesStopChord();
    void globalShortcutLeavesUnrelatedNearbyInput();
    void evdevCaptureBackendRecordsABCD();
    void globalInputMonitorStopWakeup();
    void loopDisabledStopsAfterOneCompletion();
    void loopEnabledRestartsAfterSuccess();
    void stopDuringLoopPreventsNextIteration();
    void playbackErrorStopsLoop();
    void disablingLoopDuringActiveIterationStopsAfterCurrentPass();
    void fileChooserResponseSuccess();
    void fileChooserResponseCancelled();
    void fileChooserResponseMalformed();
    void interfaceModeDefaultsRegularAndPersists();
    void secondInstanceNotifiesPrimaryAndExits();
    void versionIsExposedInSafeDiagnostics();
};

static QList<QPair<quint16, qint32>> keyEventPairsForCode(const QVector<input_event> &events, quint16 code)
{
    QList<QPair<quint16, qint32>> pairs;
    for (const input_event &event : events) {
        if ((event.type == EV_KEY && event.code == code) || (event.type == EV_SYN && event.code == SYN_REPORT)) {
            pairs.append({event.type, event.value});
        }
    }
    return pairs;
}

static EvdevDeviceInfo makeDevice(const QString &path,
                                  const QString &name,
                                  const QString &category,
                                  bool physicalCandidate,
                                  bool catClickerVirtual = false)
{
    EvdevDeviceInfo device;
    device.eventPath = path;
    device.displayName = name;
    device.sysfsName = name;
    device.category = category;
    device.isPhysicalInputCandidate = physicalCandidate;
    device.isCatClickerVirtualDevice = catClickerVirtual;
    device.hasKeyboardKeys = category == QStringLiteral("keyboard");
    device.hasMouseButtons = category == QStringLiteral("mouse");
    device.hasRelativePointer = category == QStringLiteral("mouse") || category == QStringLiteral("touchpad");
    device.hasWheel = category == QStringLiteral("mouse") || category == QStringLiteral("touchpad");
    device.vendorId = catClickerVirtual ? VirtualDeviceIdentity::VendorId : 0x1234;
    device.productId = catClickerVirtual ? VirtualDeviceIdentity::KeyboardProductId : 0x5678;
    return device;
}

static input_event makeInputEvent(quint16 type, quint16 code, qint32 value)
{
    input_event event{};
    event.type = type;
    event.code = code;
    event.value = value;
    return event;
}

void MacroCoreTests::serializationRoundTrip()
{
    Macro macro;
    macro.name = QStringLiteral("Roundtrip Example");
    macro.durationUs = 830000;
    macro.createdAtUtc = QDateTime::fromString(QStringLiteral("2026-09-01T12:34:56Z"), Qt::ISODate);
    macro.display.displayId = QStringLiteral("Primary Display");
    macro.display.streamWidth = 1920;
    macro.display.streamHeight = 1080;
    macro.display.scale = 1.25;
    macro.display.logicalWidth = 2560;
    macro.display.logicalHeight = 1440;
    macro.display.offsetX = 10;
    macro.display.offsetY = 20;
    macro.events = {
        MacroEvent::mouseMove(0, 640.5, 360.25),
        MacroEvent::keyEvent(250000, 42, true),
        MacroEvent::keyEvent(300000, 42, false),
        MacroEvent::scroll(500000, 0.0, -1.0, 0.0, 0.0, false),
        MacroEvent::mouseButton(750000, 272, true, 800.0, 500.0, true),
        MacroEvent::mouseButton(830000, 272, false, 800.0, 500.0, true),
    };

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = tempDir.filePath(QStringLiteral("roundtrip.catmacro"));

    QString error;
    QVERIFY(MacroSerializer::saveToFile(path, macro, &error));

    Macro parsed;
    QVERIFY(MacroSerializer::loadFromFile(path, &parsed, &error));
    QCOMPARE(parsed.name, macro.name);
    QCOMPARE(parsed.durationUs, macro.durationUs);
    QCOMPARE(parsed.createdAtUtc, macro.createdAtUtc);
    QCOMPARE(parsed.display.displayId, macro.display.displayId);
    QCOMPARE(parsed.display.streamWidth, macro.display.streamWidth);
    QCOMPARE(parsed.display.streamHeight, macro.display.streamHeight);
    QCOMPARE(parsed.display.scale, macro.display.scale);
    QCOMPARE(parsed.display.logicalWidth, macro.display.logicalWidth);
    QCOMPARE(parsed.display.logicalHeight, macro.display.logicalHeight);
    QCOMPARE(parsed.display.offsetX, macro.display.offsetX);
    QCOMPARE(parsed.display.offsetY, macro.display.offsetY);
    QCOMPARE(parsed.events.size(), macro.events.size());
    for (int i = 0; i < macro.events.size(); ++i) {
        const MacroEvent &expected = macro.events.at(i);
        const MacroEvent &actual = parsed.events.at(i);
        QCOMPARE(actual.type, expected.type);
        QCOMPARE(actual.timeUs, expected.timeUs);
        QCOMPARE(actual.keyCode, expected.keyCode);
        QCOMPARE(actual.pressed, expected.pressed);
        QCOMPARE(actual.button, expected.button);
        QCOMPARE(actual.x, expected.x);
        QCOMPARE(actual.y, expected.y);
        QCOMPARE(actual.hasCursorAnchor, expected.hasCursorAnchor);
        QCOMPARE(actual.anchorX, expected.anchorX);
        QCOMPARE(actual.anchorY, expected.anchorY);
        QCOMPARE(actual.deltaX, expected.deltaX);
        QCOMPARE(actual.deltaY, expected.deltaY);
    }
}

void MacroCoreTests::chronologicalOrdering()
{
    Macro macro;
    macro.events = {
        MacroEvent::keyEvent(500, 30, true),
        MacroEvent::keyEvent(100, 31, true),
    };
    macro.sortChronologically();
    QCOMPARE(macro.events.first().timeUs, 100);
    QCOMPARE(macro.events.last().timeUs, 500);
}

void MacroCoreTests::timestampScaling()
{
    QCOMPARE(MacroPlayer::scaledTimestampUs(1000000, 2.0), 500000);
    QCOMPARE(MacroPlayer::scaledTimestampUs(1000000, 0.5), 2000000);
}

void MacroCoreTests::smoothingScheduleInterpolatesOnlyAdjacentMoves()
{
    Macro macro;
    macro.events = {
        MacroEvent::mouseMove(0, 0.0, 0.0),
        MacroEvent::mouseMove(33332, 40.0, 20.0),
    };
    MacroPlayer player;
    const QVector<MacroPlayer::ScheduledEvent> unsmoothed = player.buildSchedule(macro, 1.0, false);
    const QVector<MacroPlayer::ScheduledEvent> smoothed = player.buildSchedule(macro, 1.0, true);

    QCOMPARE(unsmoothed.size(), 2);
    QCOMPARE(smoothed.size(), 5);
    QCOMPARE(smoothed.first().event.x, 0.0);
    QCOMPARE(smoothed.last().event.x, 40.0);
    QCOMPARE(smoothed.last().event.y, 20.0);
    QVERIFY(std::abs(smoothed.at(1).event.x - 10.0) < 0.01);
    QVERIFY(std::abs(smoothed.at(1).event.y - 5.0) < 0.01);
    QCOMPARE(smoothed.at(1).dueUs, qint64(8333));
    QCOMPARE(macro.events.size(), 2); // Scheduling never rewrites the macro.
}

void MacroCoreTests::smoothingDoesNotCrossMouseSemantics()
{
    Macro macro;
    macro.events = {
        MacroEvent::mouseMove(0, 0.0, 0.0),
        MacroEvent::mouseButton(10000, BTN_LEFT, true, 20.0, 20.0, true),
        MacroEvent::mouseMove(30000, 60.0, 60.0),
        MacroEvent::scroll(40000, 0.0, -1.0, 60.0, 60.0, true),
        MacroEvent::mouseMove(60000, 100.0, 100.0),
    };
    MacroPlayer player;
    const QVector<MacroPlayer::ScheduledEvent> schedule = player.buildSchedule(macro, 1.0, true);
    QCOMPARE(schedule.size(), macro.events.size());
    for (qsizetype index = 0; index < schedule.size(); ++index) {
        QCOMPARE(schedule.at(index).event.type, macro.events.at(index).type);
    }
}

void MacroCoreTests::unanchoredMousePlaybackIsDropped()
{
    Macro macro;
    macro.events = {
        MacroEvent::keyEvent(0, KEY_A, true),
        MacroEvent::mouseButton(1000, BTN_LEFT, true, 0.0, 0.0, false),
        MacroEvent::scroll(2000, 0.0, -1.0, 0.0, 0.0, false),
        MacroEvent::keyEvent(3000, KEY_A, false),
    };
    MacroPlayer player;
    const QVector<MacroPlayer::ScheduledEvent> schedule = player.buildSchedule(macro, 1.0, true);
    QCOMPARE(schedule.size(), 2);
    QCOMPARE(schedule.at(0).event.type, MacroEventType::Key);
    QCOMPARE(schedule.at(1).event.type, MacroEventType::Key);

    MockInputSenderBackend backend;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &backend, true));
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1000));
    QVERIFY(std::none_of(backend.calls.cbegin(), backend.calls.cend(), [](const MockInputSenderBackend::Call &call) {
        return call.type == QStringLiteral("button") || call.type == QStringLiteral("scroll")
            || call.type == QStringLiteral("move");
    }));
}

void MacroCoreTests::anchoredMousePlaybackUsesExactCoordinates()
{
    Macro macro;
    macro.events = {
        MacroEvent::mouseButton(0, BTN_LEFT, true, 125.0, 225.0, true),
        MacroEvent::mouseButton(20000, BTN_LEFT, false, 126.0, 226.0, true),
        MacroEvent::scroll(30000, 0.0, -1.0, 700.0, 500.0, true),
    };
    MockInputSenderBackend backend;
    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &backend, true));
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1000));

    QCOMPARE(backend.calls.at(0), (MockInputSenderBackend::Call{QStringLiteral("move"), 0, 0, 125.0, 225.0}));
    QCOMPARE(backend.calls.at(1).type, QStringLiteral("button"));
    QCOMPARE(backend.calls.at(2), (MockInputSenderBackend::Call{QStringLiteral("move"), 0, 0, 126.0, 226.0}));
    QCOMPARE(backend.calls.at(3).type, QStringLiteral("button"));
    QCOMPARE(backend.calls.at(4), (MockInputSenderBackend::Call{QStringLiteral("move"), 0, 0, 700.0, 500.0}));
    QCOMPARE(backend.calls.at(5).type, QStringLiteral("scroll"));
}

void MacroCoreTests::repeatedPlaybackReachesFinalEventWithAndWithoutSmoothing()
{
    Macro macro;
    macro.events = {
        MacroEvent::mouseMove(0, 10.0, 20.0),
        MacroEvent::mouseMove(10000, 30.0, 40.0),
        MacroEvent::mouseButton(20000, BTN_LEFT, true, 50.0, 60.0, true),
        MacroEvent::mouseButton(30000, BTN_LEFT, false, 50.0, 60.0, true),
    };
    for (bool smoothing : {false, true}) {
        MacroPlayer player;
        MockInputSenderBackend backend;
        QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
        for (int run = 0; run < 3; ++run) {
            backend.calls.clear();
            QVERIFY(player.startPlayback(macro, 1.0, &backend, smoothing));
            QTRY_COMPARE(finishedSpy.count(), run + 1);
            const int releaseIndex = static_cast<int>(std::find_if(
                backend.calls.cbegin(), backend.calls.cend(), [](const MockInputSenderBackend::Call &call) {
                    return call.type == QStringLiteral("button") && call.a == BTN_LEFT && call.b == 0;
                }) - backend.calls.cbegin());
            QVERIFY(releaseIndex < backend.calls.size());
            QCOMPARE(backend.calls.at(releaseIndex - 1),
                     (MockInputSenderBackend::Call{QStringLiteral("move"), 0, 0, 50.0, 60.0}));
        }
    }
}

void MacroCoreTests::longMouseHoldPreservesTimingAndAnchors()
{
    Macro macro;
    macro.events = {
        MacroEvent::mouseButton(0, BTN_LEFT, true, 400.0, 300.0, true),
        MacroEvent::mouseButton(2000000, BTN_LEFT, false, 400.0, 300.0, true),
    };
    MacroPlayer player;
    const QVector<MacroPlayer::ScheduledEvent> schedule = player.buildSchedule(macro, 1.0, true);
    QCOMPARE(schedule.size(), 2);
    QCOMPARE(schedule.at(1).dueUs - schedule.at(0).dueUs, qint64(2000000));

    MockInputSenderBackend backend;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(player.startPlayback(macro, 100.0, &backend, true));
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1000));
    QVERIFY(timer.elapsed() >= 18);
    QCOMPARE(backend.calls.at(0),
             (MockInputSenderBackend::Call{QStringLiteral("move"), 0, 0, 400.0, 300.0}));
    QCOMPARE(backend.calls.at(1),
             (MockInputSenderBackend::Call{QStringLiteral("button"), BTN_LEFT, 1, 0.0, 0.0}));
    QCOMPARE(backend.calls.at(2),
             (MockInputSenderBackend::Call{QStringLiteral("move"), 0, 0, 400.0, 300.0}));
    QCOMPARE(backend.calls.at(3),
             (MockInputSenderBackend::Call{QStringLiteral("button"), BTN_LEFT, 0, 0.0, 0.0}));
}

void MacroCoreTests::dragPlaybackPreservesOrderingAndAnchors()
{
    Macro macro;
    macro.events = {
        MacroEvent::mouseButton(0, BTN_LEFT, true, 100.0, 100.0, true),
        MacroEvent::mouseMove(20000, 300.0, 250.0),
        MacroEvent::mouseButton(40000, BTN_LEFT, false, 500.0, 400.0, true),
    };
    MockInputSenderBackend backend;
    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &backend, true));
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1000));
    QCOMPARE(backend.calls.at(0).x, 100.0);
    QCOMPARE(backend.calls.at(1).type, QStringLiteral("button"));
    QVERIFY(backend.calls.at(1).b == 1);
    QCOMPARE(backend.calls.at(2),
             (MockInputSenderBackend::Call{QStringLiteral("move"), 0, 0, 300.0, 250.0}));
    QCOMPARE(backend.calls.at(3),
             (MockInputSenderBackend::Call{QStringLiteral("move"), 0, 0, 500.0, 400.0}));
    QCOMPARE(backend.calls.at(4).type, QStringLiteral("button"));
    QVERIFY(backend.calls.at(4).b == 0);
}

void MacroCoreTests::heldKeyCleanup()
{
    MacroPlayer player;
    player.markHeld(MacroEvent::keyEvent(0, 42, true));
    const QVector<MacroEvent> releases = player.buildEmergencyReleaseEvents();
    QCOMPARE(releases.size(), 1);
    QCOMPARE(releases.first().type, MacroEventType::Key);
    QCOMPARE(releases.first().keyCode, 42U);
    QVERIFY(!releases.first().pressed);
}

void MacroCoreTests::heldButtonCleanup()
{
    MacroPlayer player;
    player.markHeld(MacroEvent::mouseButton(0, 272, true, 0.0, 0.0, false));
    const QVector<MacroEvent> releases = player.buildEmergencyReleaseEvents();
    QVERIFY(releases.isEmpty());
}

void MacroCoreTests::corruptedMacroRejected()
{
    Macro macro;
    QString error;
    QVERIFY(!MacroSerializer::fromJson("{ broken", &macro, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!MacroSerializer::fromJson(R"({
        "format": "CatClicker Macro",
        "version": 1.5,
        "name": "Bad",
        "duration_us": "1000",
        "created_at_utc": "2026-09-01T12:34:56Z",
        "display": {
            "display_id": "Primary",
            "width": 1920,
            "height": 1080,
            "scale": 1.0,
            "logical_width": 1920,
            "logical_height": 1080,
            "offset_x": 0,
            "offset_y": 0
        },
        "events": []
    })", &macro, &error));
    QVERIFY(error.contains(QStringLiteral("version")));

    QVERIFY(MacroSerializer::fromJson(R"({
        "format": "CatClicker Macro",
        "version": 1,
        "name": "Good",
        "duration_us": "1000",
        "created_at_utc": "2026-09-01T12:34:56Z",
        "display": {
            "display_id": "Primary",
            "width": 1920,
            "height": 1080,
            "scale": 1.0,
            "logical_width": 1920,
            "logical_height": 1080,
            "offset_x": 0,
            "offset_y": 0
        },
        "events": []
    })", &macro, &error));

    QVERIFY(!MacroSerializer::fromJson(R"({
        "format": "CatClicker Macro",
        "version": 1,
        "name": "Bad",
        "duration_us": "1000",
        "created_at_utc": "2026-09-01T12:34:56Z",
        "display": {
            "display_id": "Primary",
            "width": 1920,
            "height": 1080,
            "scale": 1.0,
            "logical_width": 1920,
            "logical_height": 1080,
            "offset_x": 0,
            "offset_y": 0
        },
        "events": [
            { "type": "mouse_move", "time_us": "0", "x": 10.0 }
        ]
    })", &macro, &error));
    QVERIFY(error.contains(QStringLiteral("Field 'y'")));

    QVERIFY(!MacroSerializer::fromJson(R"({
        "format": "CatClicker Macro",
        "version": 1,
        "name": "Bad",
        "duration_us": "1000",
        "created_at_utc": "2026-09-01T12:34:56Z",
        "display": {
            "display_id": "Primary",
            "width": 1920,
            "height": 1080,
            "scale": 1.0,
            "logical_width": 1920.5,
            "logical_height": 1080,
            "offset_x": 0,
            "offset_y": 0
        },
        "events": []
    })", &macro, &error));
    QVERIFY(error.contains(QStringLiteral("logical_width")));

    QVERIFY(!MacroSerializer::fromJson(R"({
        "format": "CatClicker Macro",
        "version": 1,
        "name": "Bad",
        "duration_us": 9223372036854775808,
        "created_at_utc": "2026-09-01T12:34:56Z",
        "display": {
            "display_id": "Primary",
            "width": 1920,
            "height": 1080,
            "scale": 1.0,
            "logical_width": 1920,
            "logical_height": 1080,
            "offset_x": 0,
            "offset_y": 0
        },
        "events": []
    })", &macro, &error));
    QVERIFY(error.contains(QStringLiteral("duration_us")));

    QVERIFY(!MacroSerializer::fromJson(R"({
        "format": "CatClicker Macro",
        "version": 1,
        "name": "Edge",
        "duration_us": -9223372036854775808,
        "created_at_utc": "2026-09-01T12:34:56Z",
        "display": {
            "display_id": "Primary",
            "width": 1920,
            "height": 1080,
            "scale": 1.0,
            "logical_width": 1920,
            "logical_height": 1080,
            "offset_x": 0,
            "offset_y": 0
        },
        "events": []
    })", &macro, &error));
    QVERIFY(error.contains(QStringLiteral("duration")));
}

void MacroCoreTests::hostileMacroInputRemainsDataOrIsRejected()
{
    const QString hostileName = QStringLiteral("$(touch /tmp/nope); 'quoted'\nUnicode: 猫");
    Macro source;
    source.name = hostileName;
    source.durationUs = 10;
    source.events = {MacroEvent::keyEvent(10, 30, true)};

    Macro parsed;
    QString error;
    QVERIFY(MacroSerializer::fromJson(MacroSerializer::toJson(source), &parsed, &error));
    QCOMPARE(parsed.name, hostileName);

    QByteArray oversized(MacroSerializer::MaximumFileSize + 1, 'x');
    QVERIFY(!MacroSerializer::fromJson(oversized, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("64 MiB")));

    QByteArray unknown = MacroSerializer::toJson(source);
    unknown.replace("\"key\"", "\"shell_command\"");
    QVERIFY(!MacroSerializer::fromJson(unknown, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("Unknown event type")));

    QByteArray nonChronological = MacroSerializer::toJson(source);
    nonChronological.replace("\"time_us\": \"10\"", "\"time_us\": \"11\"");
    QVERIFY(!MacroSerializer::fromJson(nonChronological, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("chronological")));

    QByteArray invalidNumeric = MacroSerializer::toJson(source);
    invalidNumeric.replace("\"x\": 0", "\"x\": 1e999");
    invalidNumeric.replace("\"key\"", "\"mouse_move\"");
    QVERIFY(!MacroSerializer::fromJson(invalidNumeric, &parsed, &error));
}

void MacroCoreTests::monitorCompatibility()
{
    Macro macro;
    macro.display.logicalWidth = 2560;
    macro.display.logicalHeight = 1440;

    MacroDisplayInfo currentDisplay;
    currentDisplay.logicalWidth = 1920;
    currentDisplay.logicalHeight = 1080;

    QString reason;
    QVERIFY(!macro.isCompatibleWith(currentDisplay, &reason));
    QVERIFY(reason.contains(QStringLiteral("2560x1440")));
}

void MacroCoreTests::buttonAnchorEnforcement()
{
    Macro macro;
    macro.events = {
        MacroEvent::mouseMove(0, 100.0, 100.0),
        MacroEvent::mouseButton(1000, 272, true, 150.0, 160.0, true),
    };

    MockInputSenderBackend backend;
    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &backend));
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1000));

    QCOMPARE(backend.calls.at(0).type, QStringLiteral("move"));
    QCOMPARE(backend.calls.at(1).type, QStringLiteral("move"));
    QCOMPARE(backend.calls.at(1).x, 150.0);
    QCOMPARE(backend.calls.at(1).y, 160.0);
    QCOMPARE(backend.calls.at(2).type, QStringLiteral("button"));
}

void MacroCoreTests::completionReleases()
{
    Macro macro;
    macro.events = {
        MacroEvent::keyEvent(0, 17, true),
        MacroEvent::mouseButton(1000, 272, true, 0.0, 0.0, false),
    };

    MockInputSenderBackend backend;
    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &backend));
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1000));

    QVERIFY(backend.calls.contains({QStringLiteral("key"), 17, 0, 0.0, 0.0}));
    QVERIFY(!backend.calls.contains({QStringLiteral("button"), 272, 1, 0.0, 0.0}));
    QCOMPARE(backend.calls.last().type, QStringLiteral("releaseEverything"));
    QVERIFY(player.buildEmergencyReleaseEvents().isEmpty());
}

void MacroCoreTests::errorReleases()
{
    Macro macro;
    macro.events = {
        MacroEvent::keyEvent(0, 17, true),
        MacroEvent::mouseMove(1000, 10.0, 20.0),
    };

    MockInputSenderBackend backend;
    backend.failOnCallIndex = 1;
    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &backend));
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1000));

    QVERIFY(backend.calls.last().type == QStringLiteral("releaseEverything"));
    QVERIFY(backend.calls.contains({QStringLiteral("key"), 17, 0, 0.0, 0.0}));
    QVERIFY(player.buildEmergencyReleaseEvents().isEmpty());
}

void MacroCoreTests::cancellationReleases()
{
    Macro macro;
    macro.events = {
        MacroEvent::keyEvent(0, 17, true),
        MacroEvent::mouseMove(50000, 10.0, 20.0),
        MacroEvent::mouseButton(1000000, 272, true, 0.0, 0.0, false),
    };

    MockInputSenderBackend backend;
    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &backend));
    QTimer::singleShot(20, &player, &MacroPlayer::stopPlayback);
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1500));
    QVERIFY(backend.calls.contains({QStringLiteral("key"), 17, 0, 0.0, 0.0}));
    QCOMPARE(backend.calls.last().type, QStringLiteral("releaseEverything"));
    QVERIFY(player.buildEmergencyReleaseEvents().isEmpty());
}

void MacroCoreTests::stopWakeupIsImmediate()
{
    Macro macro;
    macro.events = {
        MacroEvent::keyEvent(5000000, 17, true),
    };

    MockInputSenderBackend backend;
    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QElapsedTimer timer;

    QVERIFY(player.startPlayback(macro, 1.0, &backend));
    timer.start();
    QTimer::singleShot(20, &player, &MacroPlayer::stopPlayback);
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(500));

    QVERIFY2(timer.elapsed() < 500, "stopPlayback() did not wake the scheduler promptly.");
    QVERIFY(backend.calls.isEmpty() || backend.calls.last().type == QStringLiteral("releaseEverything"));
}

void MacroCoreTests::stopReportsUserCancellation()
{
    Macro macro;
    macro.events = {
        MacroEvent::keyEvent(0, 42, true),
        MacroEvent::mouseButton(1000, 272, true, 300.0, 400.0, true),
        MacroEvent::mouseButton(5000000, 272, false, 300.0, 400.0, true),
    };

    MockInputSenderBackend backend;
    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);

    QVERIFY(player.startPlayback(macro, 1.0, &backend));
    QTimer::singleShot(20, &player, &MacroPlayer::stopPlayback);
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1500));

    QCOMPARE(finishedSpy.count(), 1);
    const QList<QVariant> args = finishedSpy.takeFirst();
    QCOMPARE(args.at(0).toBool(), false);
    QCOMPARE(args.at(1).toBool(), true);
    QCOMPARE(args.at(2).toString(), QStringLiteral("Playback stopped by user."));
    QVERIFY(backend.calls.contains({QStringLiteral("key"), 42, 0, 0.0, 0.0}));
    QVERIFY(backend.calls.contains({QStringLiteral("button"), 272, 1, 0.0, 0.0}));
    QCOMPARE(backend.calls.last().type, QStringLiteral("releaseEverything"));
    QVERIFY(player.buildEmergencyReleaseEvents().isEmpty());
}

void MacroCoreTests::keycodePassThrough()
{
    MockInputSenderBackend backend;
    QVERIFY(backend.sendKey(30, true));
    QCOMPARE(backend.calls.first().a, 30);
}

void MacroCoreTests::playbackEventOrdering()
{
    Macro macro;
    macro.events = {
        MacroEvent::keyEvent(0, 17, true),
        MacroEvent::mouseMove(1000, 1.0, 2.0),
        MacroEvent::scroll(2000, 0.0, -1.0, 1.0, 2.0, true),
    };

    MockInputSenderBackend backend;
    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &backend));
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1000));
    QCOMPARE(backend.calls.at(0).type, QStringLiteral("key"));
    QCOMPARE(backend.calls.at(1).type, QStringLiteral("move"));
    QCOMPARE(backend.calls.at(2).type, QStringLiteral("move"));
    QCOMPARE(backend.calls.at(2).x, 1.0);
    QCOMPARE(backend.calls.at(2).y, 2.0);
    QCOMPARE(backend.calls.at(3).type, QStringLiteral("scroll"));
}

void MacroCoreTests::backendSelection()
{
    PortalCapabilities capabilities;
    MockInputSenderBackend eiBackend;
    MockInputSenderBackend uinputBackend;
    eiBackend.available = false;
    uinputBackend.available = true;

    PlaybackBackendSelection selection = PlaybackBackendSelector::select(capabilities,
                                                                         QStringLiteral("COSMIC"),
                                                                         &eiBackend,
                                                                         &uinputBackend);
    QCOMPARE(selection.backend, static_cast<InputSenderBackend *>(&uinputBackend));
    QVERIFY(selection.reason.contains(QStringLiteral("uinput fallback")));
}

void MacroCoreTests::cursorModeDecoding()
{
    const ScreenCastCursorModeSupport support = PortalController::decodeCursorModes(3);
    QVERIFY(support.hidden);
    QVERIFY(support.embedded);
    QVERIFY(!support.metadata);
}

void MacroCoreTests::cosmicCursorStateRequiresEnterAndPosition()
{
    CosmicCursorState state;
    QVERIFY(!state.hasPosition());
    QVERIFY(!state.updatePosition(QPointF(10.0, 20.0)));
    state.entered();
    QVERIFY(!state.hasPosition());
    QVERIFY(state.updatePosition(QPointF(500.0, 300.0)));
    QVERIFY(state.hasPosition());
    QCOMPARE(state.position(), QPointF(500.0, 300.0));
}

void MacroCoreTests::cosmicCursorStateLeaveAndStopInvalidate()
{
    CosmicCursorState state;
    state.entered();
    QVERIFY(state.updatePosition(QPointF(50.0, 30.0)));
    state.left();
    QVERIFY(!state.hasPosition());
    state.entered();
    QVERIFY(state.updatePosition(QPointF(60.0, 40.0)));
    state.stopped();
    QVERIFY(!state.hasPosition());
}

void MacroCoreTests::cosmicCursorMappingScaleOne()
{
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    QPointF logical;
    QVERIFY(mapCosmicCursorPosition(mapping, QPointF(500.0, 300.0), &logical));
    QCOMPARE(logical, QPointF(500.0, 300.0));
}

void MacroCoreTests::cosmicCursorMappingScaled()
{
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 3840, 2160, 0};
    QPointF logical;
    QVERIFY(mapCosmicCursorPosition(mapping, QPointF(1000.0, 600.0), &logical));
    QCOMPARE(logical, QPointF(500.0, 300.0));
}

void MacroCoreTests::cosmicCursorMappingLogicalOrigin()
{
    const CosmicCursorOutputMapping mapping{1920, 0, 1920, 1080, 1920, 1080, 0};
    QPointF logical;
    QVERIFY(mapCosmicCursorPosition(mapping, QPointF(100.0, 200.0), &logical));
    QCOMPARE(logical, QPointF(2020.0, 200.0));
}

void MacroCoreTests::cosmicCursorMappingRejectsTransform()
{
    const CosmicCursorOutputMapping mapping{0, 0, 1080, 1920, 1920, 1080, 1};
    QPointF logical;
    QString error;
    QVERIFY(!mapCosmicCursorPosition(mapping, QPointF(100.0, 200.0), &logical, &error));
    QVERIFY(error.contains(QStringLiteral("transform")));
}

void MacroCoreTests::cosmicCursorSnapshotIsCoherentAcrossThreads()
{
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    provider.applyEnter();
    provider.applyPosition(QPointF(1.0, 2.0), mapping);

    std::atomic_bool finished = false;
    std::thread updater([&]() {
        for (int i = 2; i < 5000; ++i) {
            provider.applyPosition(QPointF(i, i * 2), mapping);
        }
        finished = true;
    });

    while (!finished.load()) {
        const CursorSnapshot snapshot = provider.cursorSnapshot();
        QVERIFY(snapshot.valid);
        QCOMPARE(snapshot.position.y(), snapshot.position.x() * 2.0);
    }
    updater.join();
}

void MacroCoreTests::cosmicCursorHealthTracksCallbacksAndPublications()
{
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    provider.applyEnter();
    provider.applyPosition(QPointF(100.0, 100.0), mapping);
    provider.applyPosition(QPointF(500.0, 400.0), mapping);

    const CursorProviderHealth health = provider.healthSnapshot();
    QVERIFY(!health.workerAlive);
    QCOMPARE(health.positionCallbackCount, quint64(2));
    QCOMPARE(health.snapshotPublishCount, quint64(2));
    QVERIFY(health.latestPositionCallbackMonotonicUs > 0);
    QVERIFY(health.latestPublished.valid);
    QCOMPARE(health.latestPublished.position, QPointF(500.0, 400.0));
}

void MacroCoreTests::cosmicProviderLeaveInvalidatesPublishedPosition()
{
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    provider.applyEnter();
    provider.applyPositionForGeneration(QPointF(100.0, 200.0), mapping, 1);
    QVERIFY(provider.cursorSnapshot().valid);
    provider.applyLeave();
    QVERIFY(!provider.cursorSnapshot().valid);
    const CursorProviderHealth health = provider.healthSnapshot();
    QCOMPARE(health.enterCount, quint64(1));
    QCOMPARE(health.leaveCount, quint64(1));
}

void MacroCoreTests::cosmicProviderRefreshRequiresNewGenerationPosition()
{
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    provider.applyEnter();
    provider.applyPositionForGeneration(QPointF(100.0, 100.0), mapping, 1);
    QVERIFY(provider.cursorSnapshot().valid);

    QVERIFY(provider.requestCursorSessionRefresh());
    QVERIFY(!provider.cursorSnapshot().valid);
    provider.applyPositionForGeneration(QPointF(200.0, 200.0), mapping, 1);
    QVERIFY(!provider.cursorSnapshot().valid);

    provider.applyCursorSessionRecreated(2);
    provider.applyEnterForGeneration(2);
    provider.applyLeaveForGeneration(1); // A retired listener cannot invalidate generation 2.
    provider.applyPositionForGeneration(QPointF(500.0, 400.0), mapping, 2);
    const CursorSnapshot fresh = provider.cursorSnapshot();
    QVERIFY(fresh.valid);
    QCOMPARE(fresh.position, QPointF(500.0, 400.0));
    const CursorProviderHealth health = provider.healthSnapshot();
    QCOMPARE(health.cursorSessionGeneration, quint64(2));
    QCOMPARE(health.cursorSessionRecreateCount, quint64(1));
    QCOMPARE(health.positionAfterRecreateCount, quint64(1));
    QVERIFY(!health.cursorSessionRefreshOutstanding);
}

void MacroCoreTests::cosmicProviderAllowsOnlyOneOutstandingRefresh()
{
    CosmicCursorPositionProvider provider;
    QVERIFY(provider.requestCursorSessionRefresh());
    for (int i = 0; i < 100; ++i) {
        QVERIFY(!provider.requestCursorSessionRefresh());
    }
    QCOMPARE(provider.healthSnapshot().cursorSessionGeneration, quint64(2));
    QVERIFY(provider.healthSnapshot().cursorSessionRefreshOutstanding);
}

void MacroCoreTests::cosmicProviderSelectionPolicy()
{
    QVERIFY(shouldUseDirectCosmicCursorProvider(
        QStringLiteral("COSMIC"), QStringLiteral("wayland"), QStringLiteral("wayland-1"), true));
    QVERIFY(shouldUseDirectCosmicCursorProvider(
        QStringLiteral("pop:COSMIC"), QStringLiteral("WAYLAND"), QStringLiteral("wayland-0"), true));
    QVERIFY(!shouldUseDirectCosmicCursorProvider(
        QStringLiteral("KDE"), QStringLiteral("wayland"), QStringLiteral("wayland-0"), true));
    QVERIFY(!shouldUseDirectCosmicCursorProvider(
        QStringLiteral("COSMIC"), QStringLiteral("x11"), QStringLiteral("wayland-0"), true));
    QVERIFY(!shouldUseDirectCosmicCursorProvider(
        QStringLiteral("COSMIC"), QStringLiteral("wayland"), QString(), true));
    QVERIFY(!shouldUseDirectCosmicCursorProvider(
        QStringLiteral("COSMIC"), QStringLiteral("wayland"), QStringLiteral("wayland-0"), false));
    QVERIFY(!shouldUseDirectCosmicCursorProvider(
        QStringLiteral("COSMIC"), QStringLiteral("wayland"), QStringLiteral("wayland-0"), true, true));
}

void MacroCoreTests::workerShutdownIsIdempotent()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    fakeBackend->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event-teardown"),
                   QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };
    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVERIFY(monitor.startMonitoring());
    monitor.stopMonitoring();
    monitor.stopMonitoring();

    Macro macro;
    macro.events = {MacroEvent::keyEvent(5000000, KEY_A, true)};
    MockInputSenderBackend sender;
    MacroPlayer player;
    QVERIFY(player.startPlayback(macro, 1.0, &sender));
    player.shutdown();
    player.shutdown();
    QVERIFY(!player.isPlaying());

    CosmicCursorPositionProvider provider;
    provider.stop();
    provider.stop();
    QVERIFY(!provider.cursorSnapshot().valid);
}

void MacroCoreTests::virtualDeviceIdentity()
{
    QVERIFY(VirtualDeviceIdentity::isCatClickerVirtualDeviceName(QStringLiteral("CatClicker Virtual Keyboard")));
    QVERIFY(VirtualDeviceIdentity::isCatClickerVirtualDevice(QStringLiteral("CatClicker Virtual Pointer"),
                                                             VirtualDeviceIdentity::VendorId,
                                                             VirtualDeviceIdentity::PointerProductId));
}

void MacroCoreTests::shortcutNormalizationRejectsModifierOnly()
{
    GlobalShortcutManager shortcuts;

    shortcuts.setStopShortcut(QStringLiteral("Shift"));
    QCOMPARE(shortcuts.stopShortcut(), QStringLiteral("Ctrl+Shift+F12"));

    shortcuts.setStopShortcut(QStringLiteral("shift+f10"));
    QCOMPARE(shortcuts.stopShortcut(), QStringLiteral("Shift+F10"));
}

void MacroCoreTests::stopShortcutAddsModifierSupersets()
{
    GlobalShortcutManager shortcuts;
    shortcuts.setStopShortcut(QStringLiteral("F10"));

    const QStringList sequences = shortcuts.stopShortcutSequences();
    QCOMPARE(sequences.first(), QStringLiteral("F10"));
    QVERIFY(sequences.contains(QStringLiteral("Shift+F10")));
    QVERIFY(sequences.contains(QStringLiteral("Ctrl+F10")));
    QVERIFY(sequences.contains(QStringLiteral("Alt+F10")));
    QVERIFY(sequences.contains(QStringLiteral("Meta+F10")));
    const QString allModifiersSequence =
        QKeySequence(
            QKeyCombination(
                Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier,
                Qt::Key_F10))
            .toString(QKeySequence::PortableText);
    QVERIFY(sequences.contains(allModifiersSequence));
    QStringList uniqueSequences = sequences;
    uniqueSequences.removeDuplicates();
    QCOMPARE(sequences.size(), uniqueSequences.size());
}

void MacroCoreTests::uinputHeldKeyBookkeeping()
{
    auto io = std::make_unique<MockUinputIo>();
    UinputInputSender sender(std::move(io));
    MacroDisplayInfo display;
    display.displayId = QStringLiteral("Test Display");
    display.logicalWidth = 1920;
    display.logicalHeight = 1080;

    QVERIFY(sender.initialize(display));
    QCOMPARE(sender.virtualHeldKeyCount(), 0);
    QVERIFY(sender.sendKey(42, true));
    QCOMPARE(sender.virtualHeldKeyCount(), 1);
    QVERIFY(sender.sendKey(42, false));
    QCOMPARE(sender.virtualHeldKeyCount(), 0);
}

void MacroCoreTests::uinputHeldButtonBookkeeping()
{
    auto io = std::make_unique<MockUinputIo>();
    UinputInputSender sender(std::move(io));
    MacroDisplayInfo display;
    display.displayId = QStringLiteral("Test Display");
    display.logicalWidth = 1920;
    display.logicalHeight = 1080;

    QVERIFY(sender.initialize(display));
    QCOMPARE(sender.virtualHeldButtonCount(), 0);
    QVERIFY(sender.sendButton(BTN_LEFT, true));
    QCOMPARE(sender.virtualHeldButtonCount(), 1);
    QVERIFY(sender.sendButton(BTN_LEFT, false));
    QCOMPARE(sender.virtualHeldButtonCount(), 0);
}

void MacroCoreTests::uinputReleaseEverythingClearsHeldCounts()
{
    auto io = std::make_unique<MockUinputIo>();
    UinputInputSender sender(std::move(io));
    MacroDisplayInfo display;
    display.displayId = QStringLiteral("Test Display");
    display.logicalWidth = 1920;
    display.logicalHeight = 1080;

    QVERIFY(sender.initialize(display));
    QVERIFY(sender.sendKey(42, true));
    QVERIFY(sender.sendButton(BTN_LEFT, true));
    QCOMPARE(sender.virtualHeldKeyCount(), 1);
    QCOMPARE(sender.virtualHeldButtonCount(), 1);

    sender.releaseEverything();

    QCOMPARE(sender.virtualHeldKeyCount(), 0);
    QCOMPARE(sender.virtualHeldButtonCount(), 0);
}

void MacroCoreTests::uinputFailedReleaseRetainsHeldCounts()
{
    auto io = std::make_unique<MockUinputIo>();
    MockUinputIo *ioPtr = io.get();
    UinputInputSender sender(std::move(io));
    MacroDisplayInfo display;
    display.displayId = QStringLiteral("Test Display");
    display.logicalWidth = 1920;
    display.logicalHeight = 1080;

    QVERIFY(sender.initialize(display));
    QVERIFY(sender.sendButton(BTN_LEFT, true));
    QCOMPARE(sender.virtualHeldButtonCount(), 1);

    ioPtr->failingEvent = {EV_KEY, BTN_LEFT, 0};
    ioPtr->failMatchingEventWrites = 1;

    QVERIFY(!sender.sendButton(BTN_LEFT, false));
    QCOMPARE(sender.virtualHeldButtonCount(), 1);
}

void MacroCoreTests::uinputRetryAfterFailedReleaseClearsHeldCounts()
{
    auto io = std::make_unique<MockUinputIo>();
    MockUinputIo *ioPtr = io.get();
    UinputInputSender sender(std::move(io));
    MacroDisplayInfo display;
    display.displayId = QStringLiteral("Test Display");
    display.logicalWidth = 1920;
    display.logicalHeight = 1080;

    QVERIFY(sender.initialize(display));
    QVERIFY(sender.sendButton(BTN_LEFT, true));
    QCOMPARE(sender.virtualHeldButtonCount(), 1);

    ioPtr->failingEvent = {EV_KEY, BTN_LEFT, 0};
    ioPtr->failMatchingEventWrites = 1;
    QVERIFY(!sender.sendButton(BTN_LEFT, false));
    QCOMPARE(sender.virtualHeldButtonCount(), 1);

    QVERIFY(sender.sendButton(BTN_LEFT, false));
    QCOMPARE(sender.virtualHeldButtonCount(), 0);
}

void MacroCoreTests::uinputMouseCompletionEventSequence()
{
    auto io = std::make_unique<MockUinputIo>();
    MockUinputIo *ioPtr = io.get();
    UinputInputSender sender(std::move(io));
    MacroDisplayInfo display;
    display.displayId = QStringLiteral("Test Display");
    display.logicalWidth = 1920;
    display.logicalHeight = 1080;

    QVERIFY(sender.initialize(display));

    Macro macro;
    macro.events = {
        MacroEvent::mouseButton(0, BTN_LEFT, true, 300.0, 400.0, true),
        MacroEvent::mouseButton(1000, BTN_LEFT, false, 300.0, 400.0, true),
    };

    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &sender));
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1000));

    QList<qint32> buttonValues;
    for (const input_event &event : ioPtr->writtenEvents) {
        if (event.type == EV_KEY && event.code == BTN_LEFT) buttonValues.push_back(event.value);
    }
    QCOMPARE(buttonValues, QList<qint32>({1, 0}));
}

void MacroCoreTests::uinputMouseStopEventSequence()
{
    auto io = std::make_unique<MockUinputIo>();
    MockUinputIo *ioPtr = io.get();
    UinputInputSender sender(std::move(io));
    MacroDisplayInfo display;
    display.displayId = QStringLiteral("Test Display");
    display.logicalWidth = 1920;
    display.logicalHeight = 1080;

    QVERIFY(sender.initialize(display));

    Macro macro;
    macro.events = {
        MacroEvent::mouseButton(0, BTN_LEFT, true, 300.0, 400.0, true),
        MacroEvent::mouseButton(5000000, BTN_LEFT, false, 300.0, 400.0, true),
    };

    MacroPlayer player;
    QSignalSpy finishedSpy(&player, &MacroPlayer::playbackFinished);
    QVERIFY(player.startPlayback(macro, 1.0, &sender));
    QTimer::singleShot(20, &player, &MacroPlayer::stopPlayback);
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(1500));

    QList<qint32> buttonValues;
    for (const input_event &event : ioPtr->writtenEvents) {
        if (event.type == EV_KEY && event.code == BTN_LEFT) buttonValues.push_back(event.value);
    }
    QCOMPARE(buttonValues, QList<qint32>({1, 0}));
}

void MacroCoreTests::uinputRepeatedAbsoluteTargetIsForcedSafely()
{
    auto io = std::make_unique<MockUinputIo>();
    MockUinputIo *ioPtr = io.get();
    UinputInputSender sender(std::move(io));
    MacroDisplayInfo display;
    display.displayId = QStringLiteral("Test Display");
    display.logicalWidth = 1920;
    display.logicalHeight = 1080;
    QVERIFY(sender.initialize(display));
    QVERIFY(sender.movePointerAbsolute(387.0, 503.0));

    ioPtr->writtenEvents.clear();
    QVERIFY(sender.movePointerAbsolute(387.0, 503.0));
    QCOMPARE(ioPtr->writtenEvents.size(), 6);
    QCOMPARE(ioPtr->writtenEvents.at(0).type, static_cast<quint16>(EV_ABS));
    QCOMPARE(ioPtr->writtenEvents.at(0).code, static_cast<quint16>(ABS_X));
    QCOMPARE(ioPtr->writtenEvents.at(0).value, 388);
    QCOMPARE(ioPtr->writtenEvents.at(1).code, static_cast<quint16>(ABS_Y));
    QCOMPARE(ioPtr->writtenEvents.at(1).value, 503);
    QCOMPARE(ioPtr->writtenEvents.at(2).type, static_cast<quint16>(EV_SYN));
    QCOMPARE(ioPtr->writtenEvents.at(3).code, static_cast<quint16>(ABS_X));
    QCOMPARE(ioPtr->writtenEvents.at(3).value, 387);
    QCOMPARE(ioPtr->writtenEvents.at(4).code, static_cast<quint16>(ABS_Y));
    QCOMPARE(ioPtr->writtenEvents.at(4).value, 503);
    QCOMPARE(ioPtr->writtenEvents.at(5).type, static_cast<quint16>(EV_SYN));
    for (const input_event &event : ioPtr->writtenEvents) {
        QVERIFY(event.type != EV_KEY);
        if (event.type == EV_ABS && event.code == ABS_X) {
            QVERIFY(event.value >= 0 && event.value < display.logicalWidth);
        }
        if (event.type == EV_ABS && event.code == ABS_Y) {
            QVERIFY(event.value >= 0 && event.value < display.logicalHeight);
        }
    }
}

void MacroCoreTests::qtFocusedCaptureBackendRecordsFocusedInput()
{
    QtFocusedCaptureBackend backend;
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &QtFocusedCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(backend.startCapture());

    QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &keyPress);

    QMouseEvent moveEvent(QEvent::MouseMove,
                          QPointF(20.0, 30.0),
                          QPointF(20.0, 30.0),
                          QPointF(20.0, 30.0),
                          Qt::NoButton,
                          Qt::NoButton,
                          Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &moveEvent);

    QWheelEvent wheelEvent(QPointF(30.0, 40.0),
                           QPointF(30.0, 40.0),
                           QPoint(),
                           QPoint(0, 120),
                           Qt::NoButton,
                           Qt::NoModifier,
                           Qt::NoScrollPhase,
                           false);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &wheelEvent);

    QKeyEvent keyRelease(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &keyRelease);

    backend.stopCapture();

    QCOMPARE(captured.size(), 4);
    QCOMPARE(captured.at(0).type, MacroEventType::Key);
    QCOMPARE(captured.at(0).keyCode, static_cast<uint32_t>(KEY_A));
    QVERIFY(captured.at(0).pressed);
    QCOMPARE(captured.at(1).type, MacroEventType::MouseMove);
    QCOMPARE(captured.at(2).type, MacroEventType::Scroll);
    QCOMPARE(captured.at(2).deltaY, 1.0);
    QCOMPARE(captured.at(3).type, MacroEventType::Key);
    QCOMPARE(captured.at(3).keyCode, static_cast<uint32_t>(KEY_A));
    QVERIFY(!captured.at(3).pressed);
    QVERIFY(captured.at(0).timeUs <= captured.at(1).timeUs);
    QVERIFY(captured.at(1).timeUs <= captured.at(2).timeUs);
    QVERIFY(captured.at(2).timeUs <= captured.at(3).timeUs);
}

void MacroCoreTests::qtFocusedCaptureBackendSuppressesTriggerShortcut()
{
    QtFocusedCaptureBackend backend;
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &QtFocusedCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    backend.suppressShortcutUntilRelease(QStringLiteral("Ctrl+Shift+F8"));
    QVERIFY(backend.startCapture());

    QKeyEvent ctrlRelease(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &ctrlRelease);
    QKeyEvent shiftRelease(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &shiftRelease);
    QKeyEvent f8Release(QEvent::KeyRelease, Qt::Key_F8, Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &f8Release);

    QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &keyPress);
    QKeyEvent keyRelease(QEvent::KeyRelease, Qt::Key_B, Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &keyRelease);

    backend.stopCapture();

    QCOMPARE(captured.size(), 2);
    QCOMPARE(captured.at(0).keyCode, static_cast<uint32_t>(KEY_B));
    QVERIFY(captured.at(0).pressed);
    QCOMPARE(captured.at(1).keyCode, static_cast<uint32_t>(KEY_B));
    QVERIFY(!captured.at(1).pressed);
}

void MacroCoreTests::qtFocusedCaptureBackendSynthesizesReleaseOnStop()
{
    QtFocusedCaptureBackend backend;
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &QtFocusedCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(backend.startCapture());

    QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_C, Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &keyPress);

    QMouseEvent mousePress(QEvent::MouseButtonPress,
                           QPointF(50.0, 60.0),
                           QPointF(50.0, 60.0),
                           QPointF(50.0, 60.0),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QCoreApplication::sendEvent(QGuiApplication::instance(), &mousePress);

    backend.stopCapture();

    QCOMPARE(captured.size(), 3);
    QCOMPARE(captured.at(0).type, MacroEventType::Key);
    QVERIFY(captured.at(0).pressed);
    QCOMPARE(captured.at(1).type, MacroEventType::MouseButton);
    QVERIFY(captured.at(1).pressed);
    QCOMPARE(captured.at(2).type, MacroEventType::Key);
    QVERIFY(!captured.at(2).pressed);
    QCOMPARE(captured.at(2).keyCode, static_cast<uint32_t>(KEY_C));
    QVERIFY(captured.at(1).timeUs <= captured.at(2).timeUs);
}

void MacroCoreTests::globalInputMonitorExcludesCatClickerVirtualDevices()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event10"),
                   QString::fromLatin1(VirtualDeviceIdentity::KeyboardName),
                   QStringLiteral("keyboard"),
                   true,
                   true),
        makeDevice(QStringLiteral("/dev/input/event11"),
                   QStringLiteral("USB Keyboard"),
                   QStringLiteral("keyboard"),
                   true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVERIFY(monitor.startMonitoring());

    QCOMPARE(backendPtr->openedPaths.size(), 1);
    QCOMPARE(backendPtr->openedPaths.first(), QStringLiteral("/dev/input/event11"));
    QCOMPARE(monitor.keyboardNodeCount(), 1);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorOpensPhysicalDevices()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event20"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true),
        makeDevice(QStringLiteral("/dev/input/event21"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVERIFY(monitor.startMonitoring());
    QVERIFY(monitor.isListenerActive());
    QVERIFY(monitor.hasOpenablePhysicalDevices());
    QCOMPARE(monitor.keyboardNodeCount(), 1);
    QCOMPARE(monitor.pointerNodeCount(), 1);
    QCOMPARE(backendPtr->openedPaths.size(), 2);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorReportsPermissionErrors()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event30"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };
    backendPtr->openErrorsByPath.insert(QStringLiteral("/dev/input/event30"), EACCES);

    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVERIFY(!monitor.startMonitoring());
    QVERIFY(monitor.hasPermissionProblem());
    QVERIFY(!monitor.globalHotkeysActive());
    QVERIFY(monitor.listenerStatusText().contains(QStringLiteral("permission"), Qt::CaseInsensitive));
}

void MacroCoreTests::globalInputMonitorPublishesKeyEvents()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event40"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVector<GlobalInputEvent> events;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured, [&](const GlobalInputEvent &event) {
        events.push_back(event);
    });

    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backendPtr->queuePartialInputEvent(QStringLiteral("/dev/input/event40"), makeInputEvent(EV_KEY, KEY_A, 1), 7);

    QTRY_COMPARE(events.size(), 1);
    QCOMPARE(events.first().type, GlobalInputEventType::Key);
    QCOMPARE(events.first().code, static_cast<uint32_t>(KEY_A));
    QVERIFY(events.first().pressed);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorPublishesMouseButtons()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event41"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVector<GlobalInputEvent> events;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured, [&](const GlobalInputEvent &event) {
        events.push_back(event);
    });

    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event41"), makeInputEvent(EV_KEY, BTN_LEFT, 1));

    QTRY_COMPARE(events.size(), 1);
    QCOMPARE(events.first().type, GlobalInputEventType::MouseButton);
    QCOMPARE(events.first().code, static_cast<uint32_t>(BTN_LEFT));
    QVERIFY(events.first().pressed);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorPublishesVerticalWheel()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event42"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVector<GlobalInputEvent> events;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured, [&](const GlobalInputEvent &event) {
        events.push_back(event);
    });

    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event42"), makeInputEvent(EV_REL, REL_WHEEL, -1));

    QTRY_COMPARE(events.size(), 1);
    QCOMPARE(events.first().type, GlobalInputEventType::Scroll);
    QCOMPARE(events.first().deltaY, -1.0);
    QCOMPARE(events.first().deltaX, 0.0);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorPublishesHorizontalWheel()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVector<GlobalInputEvent> events;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured, [&](const GlobalInputEvent &event) {
        events.push_back(event);
    });

    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43"), makeInputEvent(EV_REL, REL_HWHEEL, 1));

    QTRY_COMPARE(events.size(), 1);
    QCOMPARE(events.first().type, GlobalInputEventType::Scroll);
    QCOMPARE(events.first().deltaX, 1.0);
    QCOMPARE(events.first().deltaY, 0.0);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorPublishesRelativeX()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43x"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVector<GlobalInputEvent> events;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured,
                     [&](const GlobalInputEvent &event) { events.push_back(event); });
    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43x"), makeInputEvent(EV_REL, REL_X, 7));
    QTRY_COMPARE(events.size(), 1);
    QCOMPARE(events.first().type, GlobalInputEventType::RelativeMotion);
    QCOMPARE(events.first().code, static_cast<uint32_t>(REL_X));
    QCOMPARE(events.first().deltaX, 7.0);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorPublishesRelativeY()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43y"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVector<GlobalInputEvent> events;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured,
                     [&](const GlobalInputEvent &event) { events.push_back(event); });
    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43y"), makeInputEvent(EV_REL, REL_Y, -9));
    QTRY_COMPARE(events.size(), 1);
    QCOMPARE(events.first().type, GlobalInputEventType::RelativeMotion);
    QCOMPARE(events.first().code, static_cast<uint32_t>(REL_Y));
    QCOMPARE(events.first().deltaY, -9.0);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorCoalescesRelativeFloodWithoutLosingButton()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43flood");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    for (int i = 0; i < 1000; ++i) {
        backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, (i % 2) ? REL_X : REL_Y, 1));
    }
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));

    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVector<GlobalInputEvent> events;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured,
                     [&](const GlobalInputEvent &event) { events.push_back(event); });
    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    QTRY_VERIFY(std::any_of(events.cbegin(), events.cend(), [](const GlobalInputEvent &event) {
        return event.type == GlobalInputEventType::MouseButton;
    }));
    QTest::qWait(50);
    const int relativeCount = static_cast<int>(std::count_if(
        events.cbegin(), events.cend(), [](const GlobalInputEvent &event) {
            return event.type == GlobalInputEventType::RelativeMotion;
        }));
    const int buttonCount = static_cast<int>(std::count_if(
        events.cbegin(), events.cend(), [](const GlobalInputEvent &event) {
            return event.type == GlobalInputEventType::MouseButton;
        }));
    QVERIFY(relativeCount > 0);
    QVERIFY(relativeCount < 1000);
    QCOMPARE(buttonCount, 1);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorMixedInputRemainsLive()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backend = fakeBackend.get();
    const QString pointer = QStringLiteral("/dev/input/event-mixed-pointer");
    const QString keyboard = QStringLiteral("/dev/input/event-mixed-keyboard");
    backend->discoveredDevices = {
        makeDevice(pointer, QStringLiteral("Pointer"), QStringLiteral("mouse"), true),
        makeDevice(keyboard, QStringLiteral("Keyboard"), QStringLiteral("keyboard"), true)
    };
    GlobalInputMonitor monitor(std::move(fakeBackend));
    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());

    for (int cycle = 0; cycle < 500; ++cycle) {
        backend->queueInputEvent(pointer, makeInputEvent(EV_REL, cycle % 2 ? REL_X : REL_Y, 1));
        backend->queueInputEvent(pointer, makeInputEvent(EV_KEY, BTN_LEFT, cycle % 7 == 0 ? 1 : 0));
        backend->queueInputEvent(keyboard, makeInputEvent(EV_KEY, KEY_A, cycle % 2));
        backend->queueInputEvent(keyboard, makeInputEvent(EV_KEY, KEY_B, cycle % 3 == 0 ? 1 : 0));
    }
    backend->queueInputEvent(pointer, makeInputEvent(EV_REL, REL_X, 1));

    QTRY_VERIFY_WITH_TIMEOUT(monitor.inputDeviceLifecycleHealthSnapshot().pointerRelEventsRead >= 501, 5000);
    const InputDeviceLifecycleHealth health = monitor.inputDeviceLifecycleHealthSnapshot();
    QVERIFY(health.pointerButtonEventsRead >= 500);
    QVERIFY(health.keyboardEventsRead >= 1000);
    QCOMPARE(health.activePointerDevices, quint64(1));
    QCOMPARE(health.activeKeyboardDevices, quint64(1));
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorReopensPointerAfterPollLoss()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backend = fakeBackend.get();
    const QString pointer = QStringLiteral("/dev/input/event-reopen-pointer");
    const EvdevDeviceInfo pointerDevice = makeDevice(pointer, QStringLiteral("Pointer"), QStringLiteral("mouse"), true);
    const EvdevDeviceInfo keyboardDevice = makeDevice(QStringLiteral("/dev/input/event-reopen-keyboard"), QStringLiteral("Keyboard"), QStringLiteral("keyboard"), true);
    const EvdevDeviceInfo virtualPointer = makeDevice(QStringLiteral("/dev/input/event-virtual-pointer"), QString::fromLatin1(VirtualDeviceIdentity::PointerName), QStringLiteral("mouse"), true, true);
    backend->discoveredDevices = {pointerDevice, keyboardDevice, virtualPointer};
    GlobalInputMonitor monitor(std::move(fakeBackend));
    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backend->setDiscoveredDevices({keyboardDevice, virtualPointer});
    backend->queuePollCondition(pointer, POLLHUP);

    QTRY_COMPARE_WITH_TIMEOUT(monitor.inputDeviceLifecycleHealthSnapshot().pointerDevicesRemoved, quint64(1), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(monitor.inputDeviceLifecycleHealthSnapshot().rescans >= 1, 3000);
    QCOMPARE(monitor.inputDeviceLifecycleHealthSnapshot().activePointerDevices, quint64(0));
    backend->setDiscoveredDevices({pointerDevice, keyboardDevice, virtualPointer});
    QTRY_COMPARE_WITH_TIMEOUT(monitor.inputDeviceLifecycleHealthSnapshot().pointerDevicesReopened, quint64(1), 3000);
    QCOMPARE(std::count(backend->openedPaths.cbegin(), backend->openedPaths.cend(), pointer), 2);
    QVERIFY(!backend->openedPaths.contains(QStringLiteral("/dev/input/event-virtual-pointer")));
    backend->queueInputEvent(pointer, makeInputEvent(EV_REL, REL_X, 1));
    QTRY_COMPARE_WITH_TIMEOUT(monitor.inputDeviceLifecycleHealthSnapshot().pointerRelEventsRead, quint64(1), 3000);
    const InputDeviceLifecycleHealth health = monitor.inputDeviceLifecycleHealthSnapshot();
    QCOMPARE(health.devicePollHup, quint64(1));
    QCOMPARE(health.pointerDevicesRemoved, quint64(1));
    QCOMPARE(health.activePointerDevices, quint64(1));
    QCOMPARE(health.activeKeyboardDevices, quint64(1));
    QVERIFY(health.rescans >= 1);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorKeepsPointerAfterRecoverableReadError()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backend = fakeBackend.get();
    const QString pointer = QStringLiteral("/dev/input/event-eagain-pointer");
    backend->discoveredDevices = {
        makeDevice(pointer, QStringLiteral("Pointer"), QStringLiteral("mouse"), true)
    };
    GlobalInputMonitor monitor(std::move(fakeBackend));
    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backend->queueReadError(pointer, EAGAIN);
    QTRY_COMPARE_WITH_TIMEOUT(monitor.inputDeviceLifecycleHealthSnapshot().deviceReadEagain, quint64(1), 3000);
    backend->queueInputEvent(pointer, makeInputEvent(EV_REL, REL_X, 1));
    QTRY_COMPARE_WITH_TIMEOUT(monitor.inputDeviceLifecycleHealthSnapshot().pointerRelEventsRead, quint64(1), 3000);
    const InputDeviceLifecycleHealth health = monitor.inputDeviceLifecycleHealthSnapshot();
    QCOMPARE(health.devicesRemoved, quint64(0));
    QCOMPARE(health.activePointerDevices, quint64(1));
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorSynDroppedSuppressesUntilReport()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backend = fakeBackend.get();
    const QString pointer = QStringLiteral("/dev/input/event-sync-pointer");
    backend->discoveredDevices = {
        makeDevice(pointer, QStringLiteral("Pointer"), QStringLiteral("mouse"), true)
    };
    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVector<GlobalInputEvent> events;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured,
                     [&](const GlobalInputEvent &event) { events.push_back(event); });
    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backend->queueInputEvent(pointer, makeInputEvent(EV_SYN, SYN_DROPPED, 0));
    backend->queueInputEvent(pointer, makeInputEvent(EV_REL, REL_X, 1));
    backend->queueInputEvent(pointer, makeInputEvent(EV_KEY, BTN_LEFT, 1));
    backend->queueInputEvent(pointer, makeInputEvent(EV_SYN, SYN_REPORT, 0));
    backend->queueInputEvent(pointer, makeInputEvent(EV_REL, REL_Y, 1));

    QTRY_COMPARE_WITH_TIMEOUT(monitor.inputDeviceLifecycleHealthSnapshot().syncRecoveries, quint64(1), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(events.size(), 1, 3000);
    QCOMPARE(events.first().type, GlobalInputEventType::RelativeMotion);
    QCOMPARE(events.first().code, static_cast<uint32_t>(REL_Y));
    QCOMPARE(monitor.inputDeviceLifecycleHealthSnapshot().synDropped, quint64(1));
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendTranslatesMouseButtons()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43b"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43b"), makeInputEvent(EV_KEY, BTN_LEFT, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43b"), makeInputEvent(EV_KEY, BTN_LEFT, 0));

    QTest::qWait(25);
    QVERIFY(captured.isEmpty());
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendSerializesVerticalWheel()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43c"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43c"), makeInputEvent(EV_REL, REL_WHEEL, -1));

    QTest::qWait(25);
    QVERIFY(captured.isEmpty());
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendSerializesHorizontalWheel()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43d"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43d"), makeInputEvent(EV_REL, REL_HWHEEL, 1));

    QTest::qWait(25);
    QVERIFY(captured.isEmpty());
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendUsesAbsoluteCursorPositionForMotion()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43e"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    FakeCursorPositionProvider cursorProvider;
    cursorProvider.hasPosition = true;
    cursorProvider.position = QPointF(321.0, 654.0);

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &cursorProvider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43e"), makeInputEvent(EV_REL, REL_X, 5));

    QTRY_COMPARE(captured.size(), 1);
    QCOMPARE(captured.first().type, MacroEventType::MouseMove);
    QCOMPARE(captured.first().x, 321.0);
    QCOMPARE(captured.first().y, 654.0);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendUsesAbsoluteCursorPositionForRelativeY()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43ey"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    FakeCursorPositionProvider provider;
    provider.hasPosition = true;
    provider.position = QPointF(222.0, 333.0);
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43ey"), makeInputEvent(EV_REL, REL_Y, 4));
    QTRY_COMPARE(captured.size(), 1);
    QCOMPARE(captured.first().type, MacroEventType::MouseMove);
    QCOMPARE(captured.first().x, 222.0);
    QCOMPARE(captured.first().y, 333.0);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendSamplesLiveProviderChanges()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43live"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    FakeCursorPositionProvider provider;
    provider.hasPosition = true;
    provider.position = QPointF(100.0, 100.0);
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43live"), makeInputEvent(EV_REL, REL_X, 1));
    QTRY_COMPARE(captured.size(), 1);
    provider.position = QPointF(500.0, 400.0);
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43live"), makeInputEvent(EV_REL, REL_Y, 1));
    QTRY_COMPARE(captured.size(), 2);
    QCOMPARE(captured.at(0).x, 100.0);
    QCOMPARE(captured.at(0).y, 100.0);
    QCOMPARE(captured.at(1).x, 500.0);
    QCOMPARE(captured.at(1).y, 400.0);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendSamplesFourDistinctClickAnchors()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43clicks");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    FakeCursorPositionProvider provider;
    provider.hasPosition = true;
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    const QList<QPointF> positions = {
        QPointF(100.0, 100.0), QPointF(500.0, 200.0),
        QPointF(900.0, 600.0), QPointF(1400.0, 800.0)
    };
    for (int i = 0; i < positions.size(); ++i) {
        provider.position = positions.at(i);
        backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));
        QTRY_COMPARE(captured.size(), i * 2 + 1);
        backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 0));
        QTRY_COMPARE(captured.size(), i * 2 + 2);
    }
    QCOMPARE(captured.size(), 8);
    for (int i = 0; i < positions.size(); ++i) {
        for (int phase = 0; phase < 2; ++phase) {
            const MacroEvent &event = captured.at(i * 2 + phase);
            QVERIFY(event.hasCursorAnchor);
            QCOMPARE(event.anchorX, positions.at(i).x());
            QCOMPARE(event.anchorY, positions.at(i).y());
        }
    }
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::cursorUpdatesProgressDuringContinuousRelativeFlood()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43progress");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    ThreadSafeFakeCursorPositionProvider provider;
    provider.setPosition(QPointF(1.0, 2.0));
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) {
                         if (event.type == MacroEventType::MouseMove) captured.push_back(event);
                     });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());

    std::atomic_bool run = true;
    std::thread cursorUpdater([&]() {
        int coordinate = 2;
        while (run.load()) {
            provider.setPosition(QPointF(coordinate, coordinate * 2));
            ++coordinate;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    std::thread relProducer([&]() {
        int axis = 0;
        while (run.load()) {
            backendPtr->queueInputEvent(path,
                makeInputEvent(EV_REL, axis++ % 2 ? REL_X : REL_Y, 1));
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 250) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    run = false;
    cursorUpdater.join();
    relProducer.join();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    QSet<QString> positions;
    for (const MacroEvent &event : captured) {
        positions.insert(QStringLiteral("%1,%2").arg(event.x).arg(event.y));
    }
    QVERIFY(captured.size() > 3);
    QVERIFY(positions.size() > 3);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::recordingLifecycleSamplesProviderAfterCaptureStarts()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43lifecycle");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    FakeCursorPositionProvider provider;
    provider.hasPosition = true;
    provider.position = QPointF(100.0, 100.0);
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    MacroRecorder recorder;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     &recorder, &MacroRecorder::appendEvent);

    // This mirrors ApplicationController: the monitor is already active, startMonitoring()
    // is called again, capture forwarding starts, and then MacroRecorder begins.
    QVERIFY(monitor.startMonitoring());
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    MacroDisplayInfo display;
    display.logicalWidth = 1920;
    display.logicalHeight = 1080;
    recorder.begin(display, QStringLiteral("Lifecycle"));

    provider.position = QPointF(500.0, 400.0);
    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_X, 1));
    QTRY_COMPARE(recorder.eventCount(), 1);
    provider.position = QPointF(900.0, 700.0);
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));
    QTRY_COMPARE(recorder.eventCount(), 2);

    backend.stopCapture();
    const Macro macro = recorder.finish();
    QCOMPARE(macro.events.size(), 2);
    QCOMPARE(macro.events.at(0).type, MacroEventType::MouseMove);
    QCOMPARE(macro.events.at(0).x, 500.0);
    QCOMPARE(macro.events.at(0).y, 400.0);
    QCOMPARE(macro.events.at(1).type, MacroEventType::MouseButton);
    QVERIFY(macro.events.at(1).pressed);
    QVERIFY(macro.events.at(1).hasCursorAnchor);
    QCOMPARE(macro.events.at(1).anchorX, 900.0);
    QCOMPARE(macro.events.at(1).anchorY, 700.0);
    monitor.stopMonitoring();
}

void MacroCoreTests::recordingDoesNotUsePositionInvalidatedByLeaveOrRefresh()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43invalidated");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    provider.applyEnter();
    provider.applyPositionForGeneration(QPointF(100.0, 100.0), mapping, 1);
    provider.applyLeave();

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));
    QTest::qWait(25);
    QVERIFY(captured.isEmpty());

    provider.applyEnter();
    provider.applyPositionForGeneration(QPointF(500.0, 400.0), mapping, 1);
    QVERIFY(provider.requestCursorSessionRefresh());
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_RIGHT, 1));
    QTest::qWait(25);
    QVERIFY(captured.isEmpty());

    provider.applyCursorSessionRecreated(2);
    provider.applyEnter();
    provider.applyPositionForGeneration(QPointF(900.0, 700.0), mapping, 2);
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_RIGHT, 1));
    QTRY_COMPARE(captured.size(), 1);
    QVERIFY(captured.at(0).hasCursorAnchor);
    QCOMPARE(captured.at(0).anchorX, 900.0);
    QCOMPARE(captured.at(0).anchorY, 700.0);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerCoalescesRelAndDeliversGenerations()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43sampler");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    provider.applyEnterForGeneration(1);
    provider.applyPositionForGeneration(QPointF(100.0, 100.0), mapping, 1);

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());

    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_X, 7));
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(1));
    QVERIFY(!provider.cursorSnapshot().valid);
    for (int i = 0; i < 1000; ++i) {
        backendPtr->queueInputEvent(path,
            makeInputEvent(EV_REL, i % 2 == 0 ? REL_X : REL_Y, i % 3 + 1));
    }
    QTRY_VERIFY(backend.samplerHealthSnapshot().refreshCoalesced > 0);
    QCOMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(1));
    QCOMPARE(provider.healthSnapshot().cursorSessionGeneration, quint64(2));

    provider.applyCursorSessionRecreated(2);
    provider.applyEnterForGeneration(2);
    provider.applyPositionForGeneration(QPointF(500.0, 400.0), mapping, 2);
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(2));
    QTRY_VERIFY(!captured.isEmpty());
    QCOMPARE(captured.at(0).type, MacroEventType::MouseMove);
    QCOMPARE(captured.at(0).x, 500.0);
    QCOMPARE(captured.at(0).y, 400.0);
    QCOMPARE(provider.healthSnapshot().cursorSessionGeneration, quint64(3));

    provider.applyCursorSessionRecreated(3);
    provider.applyEnterForGeneration(3);
    provider.applyPositionForGeneration(QPointF(900.0, 700.0), mapping, 3);
    QTRY_COMPARE(backend.samplerHealthSnapshot().samplesDelivered, quint64(2));
    QCOMPARE(captured.at(1).type, MacroEventType::MouseMove);
    QCOMPARE(captured.at(1).x, 900.0);
    QCOMPARE(captured.at(1).y, 700.0);
    QCOMPARE(backend.samplerHealthSnapshot().refreshCompletions, quint64(2));
    QVERIFY(captured.at(0).timeUs <= captured.at(1).timeUs);

    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerDefersMouseButNotKeyboard()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43samplerorder");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("Keyboard Mouse"), QStringLiteral("mouse"), true)
    };
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    provider.applyEnterForGeneration(1);
    provider.applyPositionForGeneration(QPointF(100.0, 100.0), mapping, 1);
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());

    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_X, 4));
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(1));
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 0));
    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_WHEEL, -1));
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, KEY_A, 1));
    QTRY_COMPARE(captured.size(), 1);
    QCOMPARE(captured.first().type, MacroEventType::Key);
    QCOMPARE(captured.first().keyCode, uint32_t(KEY_A));
    QCOMPARE(backend.samplerHealthSnapshot().deferredButtonEvents, quint64(2));
    QCOMPARE(backend.samplerHealthSnapshot().deferredScrollEvents, quint64(1));

    provider.applyCursorSessionRecreated(2);
    provider.applyEnterForGeneration(2);
    provider.applyPositionForGeneration(QPointF(640.0, 360.0), mapping, 2);
    QTRY_COMPARE(captured.size(), 5);
    const auto buttonIt = std::find_if(captured.cbegin(), captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::MouseButton;
    });
    const auto scrollIt = std::find_if(captured.cbegin(), captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::Scroll;
    });
    QVERIFY(buttonIt != captured.cend());
    QVERIFY(scrollIt != captured.cend());
    QVERIFY(buttonIt->hasCursorAnchor);
    QVERIFY(scrollIt->hasCursorAnchor);
    QCOMPARE(buttonIt->anchorX, 640.0);
    QCOMPARE(buttonIt->anchorY, 360.0);
    QCOMPARE(scrollIt->anchorX, 640.0);
    QCOMPARE(scrollIt->anchorY, 360.0);
    QVERIFY(buttonIt->timeUs < scrollIt->timeUs);
    QVERIFY(scrollIt->timeUs < captured.first().timeUs); // Original evdev time, despite later delivery.
    const auto buttonUpIt = std::find_if(std::next(buttonIt), captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::MouseButton && !event.pressed;
    });
    QVERIFY(buttonUpIt != captured.cend());
    QVERIFY(buttonIt->pressed);
    QVERIFY(buttonIt->timeUs < buttonUpIt->timeUs);

    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerStationaryClickAndStopAreSafe()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43samplerstop");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    provider.applyEnterForGeneration(1);
    provider.applyPositionForGeneration(QPointF(300.0, 250.0), mapping, 1);
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());

    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 0));
    QTRY_COMPARE(captured.size(), 2);
    QCOMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(0));
    QVERIFY(captured.at(0).hasCursorAnchor);
    QCOMPARE(captured.at(0).anchorX, 300.0);

    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_Y, 5));
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(1));
    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_RIGHT, 1));
    QTRY_COMPARE(backend.samplerHealthSnapshot().deferredButtonEvents, quint64(1));
    QElapsedTimer stopTimer;
    stopTimer.start();
    backend.stopCapture();
    QVERIFY(stopTimer.elapsed() < 100);
    QCOMPARE(backend.samplerHealthSnapshot().unresolvedMouseEventsDroppedOnStop, quint64(2));
    QCOMPARE(captured.size(), 2);

    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerSupportsThreeRecordingsOnSameBackend()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43three-recordings");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    provider.applyEnterForGeneration(1);
    provider.applyPositionForGeneration(QPointF(10.0, 10.0), mapping, 1);
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());

    const QList<QPointF> positions = {
        QPointF(100.0, 100.0), QPointF(500.0, 400.0), QPointF(900.0, 700.0)
    };
    for (int recording = 0; recording < positions.size(); ++recording) {
        captured.clear();
        QVERIFY(backend.startCapture());
        QCOMPARE(backend.samplerHealthSnapshot().captureSessionNumber,
                 static_cast<quint64>(recording + 1));
        backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_X, recording + 1));
        QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(1));
        const quint64 generation = provider.healthSnapshot().cursorSessionGeneration;
        provider.applyCursorSessionRecreated(generation);
        provider.applyEnterForGeneration(generation);
        provider.applyPositionForGeneration(positions.at(recording), mapping, generation);
        QTRY_COMPARE(captured.size(), 1);
        backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));
        backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 0));
        QTRY_COMPARE(captured.size(), 3);
        QCOMPARE(captured.at(0).type, MacroEventType::MouseMove);
        QCOMPARE(QPointF(captured.at(0).x, captured.at(0).y), positions.at(recording));
        QVERIFY(captured.at(1).hasCursorAnchor);
        QVERIFY(captured.at(2).hasCursorAnchor);
        QCOMPARE(QPointF(captured.at(1).anchorX, captured.at(1).anchorY), positions.at(recording));
        backend.stopCapture();
    }

    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerIgnoresLateCompletionAndRecoversNextRecording()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43late-completion");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());

    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_X, 1));
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(1));
    const quint64 recordingAGeneration = provider.healthSnapshot().cursorSessionGeneration;
    backend.stopCapture();
    provider.applyCursorSessionRecreated(recordingAGeneration);
    provider.applyEnterForGeneration(recordingAGeneration);
    provider.applyPositionForGeneration(QPointF(200.0, 200.0), mapping, recordingAGeneration);
    QCoreApplication::processEvents();
    QVERIFY(captured.isEmpty());

    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_Y, 1));
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(1));
    const quint64 recordingBGeneration = provider.healthSnapshot().cursorSessionGeneration;
    QVERIFY(recordingBGeneration > recordingAGeneration);
    provider.applyPositionForGeneration(QPointF(300.0, 300.0), mapping, recordingAGeneration);
    QCoreApplication::processEvents();
    QVERIFY(captured.isEmpty());
    provider.applyCursorSessionRecreated(recordingBGeneration);
    provider.applyEnterForGeneration(recordingBGeneration);
    provider.applyPositionForGeneration(QPointF(500.0, 400.0), mapping, recordingBGeneration);
    QTRY_COMPARE(captured.size(), 1);
    QCOMPARE(QPointF(captured.first().x, captured.first().y), QPointF(500.0, 400.0));
    backend.stopCapture();

    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerDoesNotStallMidRecording()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43mid-session");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());

    const QList<QPointF> positions = {
        QPointF(100.0, 100.0), QPointF(300.0, 250.0), QPointF(600.0, 450.0)
    };
    for (int sample = 0; sample < positions.size(); ++sample) {
        backendPtr->queueInputEvent(path,
            makeInputEvent(EV_REL, sample % 2 == 0 ? REL_X : REL_Y, sample + 1));
        QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests,
                     static_cast<quint64>(sample + 1));
        if (sample == 2) {
            for (int burst = 0; burst < 100; ++burst) {
                backendPtr->queueInputEvent(path,
                    makeInputEvent(EV_REL, burst % 2 == 0 ? REL_X : REL_Y, 1));
            }
            // A lossless key queued behind the coalesced REL burst is an ordering
            // barrier: observing it proves the worker and queued Qt publication
            // have delivered all preceding physical input. Merely inspecting
            // followUpPending here races those two asynchronous stages.
            backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, KEY_A, 1));
            QTRY_VERIFY(std::any_of(captured.cbegin(), captured.cend(), [](const MacroEvent &event) {
                return event.type == MacroEventType::Key && event.keyCode == KEY_A;
            }));
            QTRY_VERIFY(backend.samplerHealthSnapshot().refreshCoalesced > 0);
        }
        const quint64 generation = provider.healthSnapshot().cursorSessionGeneration;
        provider.applyCursorSessionRecreated(generation);
        provider.applyEnterForGeneration(generation);
        provider.applyPositionForGeneration(positions.at(sample), mapping, generation);
        QTRY_VERIFY(backend.samplerHealthSnapshot().samplesDelivered >= static_cast<quint64>(sample + 1));
        if (sample == 2) {
            QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(4));
            const quint64 followUpGeneration = provider.healthSnapshot().cursorSessionGeneration;
            provider.applyCursorSessionRecreated(followUpGeneration);
            provider.applyEnterForGeneration(followUpGeneration);
            provider.applyPositionForGeneration(QPointF(650.0, 500.0), mapping, followUpGeneration);
            QTRY_COMPARE(backend.samplerHealthSnapshot().samplesDelivered, quint64(4));
            QTRY_VERIFY(!backend.samplerHealthSnapshot().refreshOutstanding);
        }
    }

    // Exercise another complete movement cycle after the coalesced follow-up.
    // Waiting for the increment from four to five prevents the previous request
    // count from satisfying the condition before this REL event is consumed.
    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_Y, 4));
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(5));
    const quint64 finalGeneration = provider.healthSnapshot().cursorSessionGeneration;
    provider.applyCursorSessionRecreated(finalGeneration);
    provider.applyEnterForGeneration(finalGeneration);
    provider.applyPositionForGeneration(QPointF(900.0, 700.0), mapping, finalGeneration);
    QTRY_COMPARE(backend.samplerHealthSnapshot().samplesDelivered, quint64(5));
    QTRY_VERIFY(!backend.samplerHealthSnapshot().refreshOutstanding);

    backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));
    QTRY_VERIFY(std::any_of(captured.cbegin(), captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::MouseButton && event.hasCursorAnchor;
    }));
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerLongActiveSessionKeepsProgressing()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43long-session");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("Combined Keyboard Mouse"), QStringLiteral("mouse"), true)
    };
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 4096, 2160, 4096, 2160, 0};
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());

    constexpr int generationCycles = 300;
    quint64 expectedRequests = 0;
    quint64 expectedSamples = 0;
    for (int cycle = 0; cycle < generationCycles; ++cycle) {
        backendPtr->queueInputEvent(path,
            makeInputEvent(EV_REL, cycle % 2 == 0 ? REL_X : REL_Y, cycle % 7 + 1));
        ++expectedRequests;
        QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, expectedRequests);

        if (cycle % 25 == 0) {
            for (int flood = 0; flood < 40; ++flood) {
                backendPtr->queueInputEvent(path,
                    makeInputEvent(EV_REL, flood % 2 == 0 ? REL_X : REL_Y, 1));
            }
            backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, KEY_A, cycle % 50 == 0 ? 1 : 0));
        }
        if (cycle == 60) {
            backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));
        } else if (cycle == 150) {
            backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 0));
        }
        if (cycle > generationCycles * 2 / 3 && cycle % 40 == 0) {
            backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_RIGHT, 1));
            backendPtr->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_RIGHT, 0));
        }

        const quint64 generation = provider.healthSnapshot().cursorSessionGeneration;
        provider.applyCursorSessionRecreated(generation);
        provider.applyEnterForGeneration(generation);
        const QPointF position(cycle + 1.0, (cycle * 3) % 2000 + 1.0);
        provider.applyPositionForGeneration(position, mapping, generation);
        ++expectedSamples;
        QTRY_COMPARE(backend.samplerHealthSnapshot().samplesDelivered, expectedSamples);

        if (backend.samplerHealthSnapshot().refreshOutstanding) {
            ++expectedRequests;
            QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, expectedRequests);
            const quint64 followUpGeneration = provider.healthSnapshot().cursorSessionGeneration;
            provider.applyCursorSessionRecreated(followUpGeneration);
            provider.applyEnterForGeneration(followUpGeneration);
            provider.applyPositionForGeneration(position + QPointF(0.25, 0.25), mapping,
                                                followUpGeneration);
            ++expectedSamples;
            QTRY_COMPARE(backend.samplerHealthSnapshot().samplesDelivered, expectedSamples);
        }

        const CursorSamplerHealth health = backend.samplerHealthSnapshot();
        QCOMPARE(health.refreshCompletions, expectedSamples);
        QVERIFY(!health.refreshOutstanding);
        QVERIFY(!health.movementPending);
        QVERIFY(!health.followUpPending);
        if ((cycle + 1) % 50 == 0) {
            const RelativeMotionHealth relative = monitor.relativeMotionHealthSnapshot();
            QVERIFY(relative.acceptedTriggers >= static_cast<quint64>(cycle + 1));
            QVERIFY(relative.deliveredTriggers >= static_cast<quint64>(cycle + 1));
            QVERIFY(!relative.payloadPending);
            QVERIFY(!relative.deliveryPosted);
        }
    }

    const auto finalThirdBegin = captured.cbegin() + captured.size() * 2 / 3;
    QVERIFY(std::any_of(finalThirdBegin, captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::MouseMove;
    }));
    QVERIFY(std::any_of(finalThirdBegin, captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::MouseButton && event.hasCursorAnchor;
    }));
    QVERIFY(std::any_of(captured.cbegin(), captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::Key;
    }));
    QCOMPARE(backend.samplerHealthSnapshot().samplesDelivered, expectedSamples);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerSuppressesStaleCoordinatesAndRecovers()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event43stale-position");
    backendPtr->discoveredDevices = {
        makeDevice(path, QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());

    const QPointF positionA(640.0, 360.0);
    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_X, 1));
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(1));
    quint64 generation = provider.healthSnapshot().cursorSessionGeneration;
    provider.applyCursorSessionRecreated(generation);
    provider.applyEnterForGeneration(generation);
    provider.applyPositionForGeneration(positionA, mapping, generation);
    QTRY_COMPARE(backend.samplerHealthSnapshot().samplesDelivered, quint64(1));

    constexpr int identicalCompletions = 64;
    for (int sample = 0; sample < identicalCompletions; ++sample) {
        backendPtr->queueInputEvent(path,
            makeInputEvent(EV_REL, sample % 2 == 0 ? REL_X : REL_Y, 1));
        QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests,
                     static_cast<quint64>(sample + 2));
        generation = provider.healthSnapshot().cursorSessionGeneration;
        provider.applyCursorSessionRecreated(generation);
        provider.applyEnterForGeneration(generation);
        provider.applyPositionForGeneration(positionA, mapping, generation);
        QTRY_COMPARE(backend.samplerHealthSnapshot().refreshCompletions,
                     static_cast<quint64>(sample + 2));
    }

    CursorSamplerHealth health = backend.samplerHealthSnapshot();
    QCOMPARE(health.resolvedSampleAttempts, quint64(identicalCompletions + 1));
    QCOMPARE(health.resolvedCoordinateChanges, quint64(1));
    QCOMPARE(health.resolvedIdenticalCoordinates, quint64(identicalCompletions));
    QCOMPARE(health.duplicateMoveSuppressions, quint64(identicalCompletions));
    QCOMPARE(health.consecutiveIdenticalResolvedSamples, quint64(identicalCompletions));
    QCOMPARE(health.samplesDelivered, quint64(1));
    QVERIFY(health.lastDuplicateSuppressionMonotonicUs > 0);
    QCOMPARE(health.staleHardRefreshRequests, quint64(0));
    QCOMPARE(provider.healthSnapshot().staleHardRefreshRequests, quint64(0));

    const QPointF positionB(900.0, 700.0);
    backendPtr->queueInputEvent(path, makeInputEvent(EV_REL, REL_X, 1));
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests,
                 quint64(identicalCompletions + 2));
    generation = provider.healthSnapshot().cursorSessionGeneration;
    provider.applyCursorSessionRecreated(generation);
    provider.applyEnterForGeneration(generation);
    provider.applyPositionForGeneration(positionB, mapping, generation);
    QTRY_COMPARE(backend.samplerHealthSnapshot().samplesDelivered, quint64(2));

    health = backend.samplerHealthSnapshot();
    QCOMPARE(health.resolvedCoordinateChanges, quint64(2));
    QCOMPARE(health.consecutiveIdenticalResolvedSamples, quint64(0));
    QCOMPARE(captured.size(), 2);
    QCOMPARE(QPointF(captured.at(0).x, captured.at(0).y), positionA);
    QCOMPARE(QPointF(captured.at(1).x, captured.at(1).y), positionB);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerHardRefreshRecoversWithGenerationIsolation()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *input = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event-stale-hard-refresh");
    input->discoveredDevices = {makeDevice(path, QStringLiteral("Combined Input"), QStringLiteral("mouse"), true)};
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());

    const QPointF positionA(400.0, 300.0);
    const auto completeSample = [&](const QPointF &position) {
        const quint64 generation = provider.healthSnapshot().cursorSessionGeneration;
        provider.applyCursorSessionRecreated(generation);
        provider.applyEnterForGeneration(generation);
        provider.applyPositionForGeneration(position, mapping, generation);
    };
    input->queueInputEvent(path, makeInputEvent(EV_REL, REL_X, 1));
    QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, quint64(1));
    completeSample(positionA);
    QTRY_COMPARE(backend.samplerHealthSnapshot().samplesDelivered, quint64(1));

    constexpr int staleThreshold = 128;
    quint64 preHardGeneration = 0;
    for (int sample = 0; sample < staleThreshold; ++sample) {
        input->queueInputEvent(path, makeInputEvent(EV_REL, sample % 2 ? REL_X : REL_Y, 1));
        QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, static_cast<quint64>(sample + 2));
        preHardGeneration = provider.healthSnapshot().cursorSessionGeneration;
        completeSample(positionA);
        QTRY_COMPARE(backend.samplerHealthSnapshot().refreshCompletions, static_cast<quint64>(sample + 2));
    }
    QCOMPARE(backend.samplerHealthSnapshot().staleHardRefreshRequests, quint64(1));
    QCOMPARE(provider.healthSnapshot().staleHardRefreshRequests, quint64(1));
    QVERIFY(provider.healthSnapshot().staleHardRefreshOutstanding);
    QVERIFY(!provider.cursorSnapshot().valid);

    input->queueInputEvent(path, makeInputEvent(EV_KEY, KEY_C, 1));
    QTRY_VERIFY(std::any_of(captured.cbegin(), captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::Key && event.keyCode == KEY_C;
    }));

    provider.applyEnterForGeneration(preHardGeneration);
    provider.applyPositionForGeneration(QPointF(800.0, 600.0), mapping, preHardGeneration);
    QCOMPARE(backend.samplerHealthSnapshot().samplesDelivered, quint64(1));
    QVERIFY(!provider.cursorSnapshot().valid);

    const quint64 hardGeneration = provider.healthSnapshot().cursorSessionGeneration;
    provider.applyCursorSessionRecreated(hardGeneration);
    provider.applyEnterForGeneration(hardGeneration);
    const QPointF positionB(900.0, 700.0);
    provider.applyPositionForGeneration(positionB, mapping, hardGeneration);
    QTRY_COMPARE(backend.samplerHealthSnapshot().samplesDelivered, quint64(2));
    QCOMPARE(provider.healthSnapshot().staleHardRefreshCompletions, quint64(1));
    QVERIFY(!provider.healthSnapshot().staleHardRefreshOutstanding);
    QCOMPARE(backend.samplerHealthSnapshot().consecutiveIdenticalResolvedSamples, quint64(0));
    QVERIFY(std::any_of(captured.cbegin(), captured.cend(), [&](const MacroEvent &event) {
        return event.type == MacroEventType::MouseMove && QPointF(event.x, event.y) == positionB;
    }));
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerHardRefreshNeverFabricatesAnchors()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *input = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event-stale-never-recovers");
    input->discoveredDevices = {makeDevice(path, QStringLiteral("Combined Input"), QStringLiteral("mouse"), true)};
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    const QPointF positionA(500.0, 500.0);
    for (int sample = 0; sample <= 128; ++sample) {
        input->queueInputEvent(path, makeInputEvent(EV_REL, REL_X, 1));
        QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, static_cast<quint64>(sample + 1));
        const quint64 generation = provider.healthSnapshot().cursorSessionGeneration;
        provider.applyCursorSessionRecreated(generation);
        provider.applyEnterForGeneration(generation);
        provider.applyPositionForGeneration(positionA, mapping, generation);
    }
    QCOMPARE(provider.healthSnapshot().staleHardRefreshRequests, quint64(1));
    input->queueInputEvent(path, makeInputEvent(EV_KEY, BTN_LEFT, 1));
    input->queueInputEvent(path, makeInputEvent(EV_REL, REL_WHEEL, 1));
    input->queueInputEvent(path, makeInputEvent(EV_KEY, KEY_D, 1));
    QTRY_VERIFY(std::any_of(captured.cbegin(), captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::Key && event.keyCode == KEY_D;
    }));
    backend.stopCapture();
    QVERIFY(std::none_of(captured.cbegin(), captured.cend(), [](const MacroEvent &event) {
        return event.type == MacroEventType::MouseButton || event.type == MacroEventType::Scroll;
    }));
    QCOMPARE(provider.healthSnapshot().staleHardRefreshRequests, quint64(1));
    monitor.stopMonitoring();
}

void MacroCoreTests::cosmicSamplerHardRefreshIsRateLimited()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *input = fakeBackend.get();
    const QString path = QStringLiteral("/dev/input/event-stale-rate-limit");
    input->discoveredDevices = {makeDevice(path, QStringLiteral("Pointer"), QStringLiteral("mouse"), true)};
    CosmicCursorPositionProvider provider;
    const CosmicCursorOutputMapping mapping{0, 0, 1920, 1080, 1920, 1080, 0};
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &provider);
    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    const QPointF positionA(600.0, 400.0);
    quint64 firstRequestAttempt = 0;
    bool observedSecondRequest = false;
    for (int attempt = 0; attempt < 500 && !observedSecondRequest; ++attempt) {
        input->queueInputEvent(path, makeInputEvent(EV_REL, attempt % 2 ? REL_X : REL_Y, 1));
        QTRY_COMPARE(backend.samplerHealthSnapshot().refreshRequests, static_cast<quint64>(attempt + 1));
        const quint64 generation = provider.healthSnapshot().cursorSessionGeneration;
        provider.applyCursorSessionRecreated(generation);
        provider.applyEnterForGeneration(generation);
        provider.applyPositionForGeneration(positionA, mapping, generation);
        const CursorSamplerHealth afterSample = backend.samplerHealthSnapshot();
        if (afterSample.staleHardRefreshRequests == 1 && firstRequestAttempt == 0) {
            firstRequestAttempt = afterSample.resolvedSampleAttempts;
        } else if (afterSample.staleHardRefreshRequests == 2) {
            QVERIFY(afterSample.resolvedSampleAttempts - firstRequestAttempt >= 256);
            observedSecondRequest = true;
        }
        if (provider.healthSnapshot().staleHardRefreshOutstanding) {
            const quint64 hardGeneration = provider.healthSnapshot().cursorSessionGeneration;
            provider.applyCursorSessionRecreated(hardGeneration);
            provider.applyEnterForGeneration(hardGeneration);
            provider.applyPositionForGeneration(positionA, mapping, hardGeneration);
        }
    }
    QVERIFY(observedSecondRequest);
    QCOMPARE(provider.healthSnapshot().staleHardRefreshRequests, quint64(2));
    QCOMPARE(provider.healthSnapshot().staleHardRefreshCompletions, quint64(2));
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendDeduplicatesResolvedCursorPosition()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43e2"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    FakeCursorPositionProvider cursorProvider;
    cursorProvider.hasPosition = true;
    cursorProvider.position = QPointF(321.0, 654.0);
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &cursorProvider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43e2"), makeInputEvent(EV_REL, REL_X, 5));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43e2"), makeInputEvent(EV_REL, REL_Y, 3));
    QTRY_COMPARE(captured.size(), 1);
    QTest::qWait(25);
    QCOMPARE(captured.size(), 1);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendUsesAbsoluteCursorAnchorForClicks()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43f"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    FakeCursorPositionProvider cursorProvider;
    cursorProvider.hasPosition = true;
    cursorProvider.position = QPointF(777.0, 222.0);

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &cursorProvider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43f"), makeInputEvent(EV_KEY, BTN_LEFT, 1));

    QTRY_COMPARE(captured.size(), 1);
    QCOMPARE(captured.first().type, MacroEventType::MouseButton);
    QVERIFY(captured.first().hasCursorAnchor);
    QCOMPARE(captured.first().anchorX, 777.0);
    QCOMPARE(captured.first().anchorY, 222.0);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendLeavesClickUnanchoredWithoutPosition()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43f2"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };
    FakeCursorPositionProvider cursorProvider;
    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &cursorProvider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured,
                     [&](const MacroEvent &event) { captured.push_back(event); });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43f2"), makeInputEvent(EV_KEY, BTN_LEFT, 1));
    QTest::qWait(25);
    QVERIFY(captured.isEmpty());
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendUsesAbsoluteCursorAnchorForScroll()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event43g"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    FakeCursorPositionProvider cursorProvider;
    cursorProvider.hasPosition = true;
    cursorProvider.position = QPointF(888.0, 444.0);

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor, &cursorProvider);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event43g"), makeInputEvent(EV_REL, REL_WHEEL, -1));

    QTRY_COMPARE(captured.size(), 1);
    QCOMPARE(captured.first().type, MacroEventType::Scroll);
    QVERIFY(captured.first().hasCursorAnchor);
    QCOMPARE(captured.first().anchorX, 888.0);
    QCOMPARE(captured.first().anchorY, 444.0);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendIgnoresRelativeMotion()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event44"), QStringLiteral("USB Mouse"), QStringLiteral("mouse"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event44"), makeInputEvent(EV_REL, REL_X, 10));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event44"), makeInputEvent(EV_REL, REL_Y, -5));
    QTest::qWait(50);

    QCOMPARE(captured.size(), 0);
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::globalShortcutExactMatch()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event45"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    GlobalShortcutManager shortcuts;
    shortcuts.setRecordShortcut(QStringLiteral("Ctrl+F8"));
    monitor.setShortcutBindings(shortcuts.recordBinding(), shortcuts.playBinding(), shortcuts.stopBinding());

    QSignalSpy shortcutSpy(&monitor, &GlobalInputMonitor::globalShortcutTriggered);
    QVERIFY(monitor.startMonitoring());

    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event45"), makeInputEvent(EV_KEY, KEY_LEFTCTRL, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event45"), makeInputEvent(EV_KEY, KEY_LEFTALT, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event45"), makeInputEvent(EV_KEY, KEY_F8, 1));
    QTest::qWait(50);

    QCOMPARE(shortcutSpy.count(), 0);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalShortcutAcceptsLeftRightModifiers()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event46"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    GlobalShortcutManager shortcuts;
    shortcuts.setRecordShortcut(QStringLiteral("Ctrl+Shift+F8"));
    monitor.setShortcutBindings(shortcuts.recordBinding(), shortcuts.playBinding(), shortcuts.stopBinding());

    QSignalSpy shortcutSpy(&monitor, &GlobalInputMonitor::globalShortcutTriggered);
    QVERIFY(monitor.startMonitoring());

    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event46"), makeInputEvent(EV_KEY, KEY_RIGHTCTRL, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event46"), makeInputEvent(EV_KEY, KEY_LEFTSHIFT, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event46"), makeInputEvent(EV_KEY, KEY_F8, 1));

    QTRY_COMPARE(shortcutSpy.count(), 1);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalShortcutIgnoresAutorepeat()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event47"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    GlobalShortcutManager shortcuts;
    shortcuts.setRecordShortcut(QStringLiteral("F8"));
    monitor.setShortcutBindings(shortcuts.recordBinding(), shortcuts.playBinding(), shortcuts.stopBinding());

    QSignalSpy shortcutSpy(&monitor, &GlobalInputMonitor::globalShortcutTriggered);
    QVERIFY(monitor.startMonitoring());

    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event47"), makeInputEvent(EV_KEY, KEY_F8, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event47"), makeInputEvent(EV_KEY, KEY_F8, 2));

    QTRY_COMPARE(shortcutSpy.count(), 1);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalShortcutSuppressesStartRecordChord()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event48"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    GlobalShortcutManager shortcuts;
    shortcuts.setRecordShortcut(QStringLiteral("Ctrl+Shift+F8"));
    monitor.setShortcutBindings(shortcuts.recordBinding(), shortcuts.playBinding(), shortcuts.stopBinding());

    QVector<GlobalInputEvent> recordedEvents;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured, [&](const GlobalInputEvent &event) {
        recordedEvents.push_back(event);
    });
    QSignalSpy shortcutSpy(&monitor, &GlobalInputMonitor::globalShortcutTriggered);

    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event48"), makeInputEvent(EV_KEY, KEY_LEFTCTRL, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event48"), makeInputEvent(EV_KEY, KEY_LEFTSHIFT, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event48"), makeInputEvent(EV_KEY, KEY_F8, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event48"), makeInputEvent(EV_KEY, KEY_F8, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event48"), makeInputEvent(EV_KEY, KEY_LEFTSHIFT, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event48"), makeInputEvent(EV_KEY, KEY_LEFTCTRL, 0));

    QTRY_COMPARE(shortcutSpy.count(), 1);
    QTest::qWait(50);
    QCOMPARE(recordedEvents.size(), 0);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalShortcutSuppressesStopChord()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event49"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    GlobalShortcutManager shortcuts;
    shortcuts.setStopShortcut(QStringLiteral("Ctrl+Shift+F12"));
    monitor.setShortcutBindings(shortcuts.recordBinding(), shortcuts.playBinding(), shortcuts.stopBinding());

    QVector<GlobalInputEvent> recordedEvents;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured, [&](const GlobalInputEvent &event) {
        recordedEvents.push_back(event);
    });
    QSignalSpy shortcutSpy(&monitor, &GlobalInputMonitor::globalShortcutTriggered);

    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event49"), makeInputEvent(EV_KEY, KEY_LEFTCTRL, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event49"), makeInputEvent(EV_KEY, KEY_LEFTSHIFT, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event49"), makeInputEvent(EV_KEY, KEY_F12, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event49"), makeInputEvent(EV_KEY, KEY_F12, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event49"), makeInputEvent(EV_KEY, KEY_LEFTCTRL, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event49"), makeInputEvent(EV_KEY, KEY_LEFTSHIFT, 0));

    QTRY_COMPARE(shortcutSpy.count(), 1);
    QTest::qWait(50);
    QCOMPARE(recordedEvents.size(), 0);
    monitor.stopMonitoring();
}

void MacroCoreTests::globalShortcutLeavesUnrelatedNearbyInput()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event50"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    GlobalShortcutManager shortcuts;
    shortcuts.setRecordShortcut(QStringLiteral("Ctrl+Shift+F8"));
    shortcuts.setStopShortcut(QStringLiteral("Ctrl+Shift+F12"));
    monitor.setShortcutBindings(shortcuts.recordBinding(), shortcuts.playBinding(), shortcuts.stopBinding());

    QVector<GlobalInputEvent> recordedEvents;
    QObject::connect(&monitor, &GlobalInputMonitor::globalEventCaptured, [&](const GlobalInputEvent &event) {
        recordedEvents.push_back(event);
    });
    QSignalSpy shortcutSpy(&monitor, &GlobalInputMonitor::globalShortcutTriggered);

    monitor.setCaptureForwardingEnabled(true);
    QVERIFY(monitor.startMonitoring());
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_LEFTCTRL, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_LEFTSHIFT, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_F8, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_F8, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_LEFTSHIFT, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_LEFTCTRL, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_A, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_A, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_LEFTCTRL, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_LEFTSHIFT, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_F12, 1));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_F12, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_LEFTSHIFT, 0));
    backendPtr->queueInputEvent(QStringLiteral("/dev/input/event50"), makeInputEvent(EV_KEY, KEY_LEFTCTRL, 0));

    QTRY_COMPARE(shortcutSpy.count(), 2);
    QTRY_COMPARE(recordedEvents.size(), 2);
    QCOMPARE(recordedEvents.at(0).code, static_cast<uint32_t>(KEY_A));
    QVERIFY(recordedEvents.at(0).pressed);
    QCOMPARE(recordedEvents.at(1).code, static_cast<uint32_t>(KEY_A));
    QVERIFY(!recordedEvents.at(1).pressed);
    monitor.stopMonitoring();
}

void MacroCoreTests::evdevCaptureBackendRecordsABCD()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event51"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    EvdevCaptureBackend backend(&monitor);
    QVector<MacroEvent> captured;
    QObject::connect(&backend, &EvdevCaptureBackend::eventCaptured, [&](const MacroEvent &event) {
        captured.push_back(event);
    });

    QVERIFY(monitor.startMonitoring());
    QVERIFY(backend.startCapture());
    const uint32_t codes[] = {KEY_A, KEY_B, KEY_C, KEY_D};
    for (uint32_t code : codes) {
        backendPtr->queueInputEvent(QStringLiteral("/dev/input/event51"), makeInputEvent(EV_KEY, static_cast<quint16>(code), 1));
        backendPtr->queueInputEvent(QStringLiteral("/dev/input/event51"), makeInputEvent(EV_KEY, static_cast<quint16>(code), 0));
    }

    QTRY_COMPARE(captured.size(), 8);
    QCOMPARE(captured.at(0).keyCode, static_cast<uint32_t>(KEY_A));
    QVERIFY(captured.at(0).pressed);
    QCOMPARE(captured.at(7).keyCode, static_cast<uint32_t>(KEY_D));
    QVERIFY(!captured.at(7).pressed);
    for (int i = 1; i < captured.size(); ++i) {
        QVERIFY(captured.at(i - 1).timeUs <= captured.at(i).timeUs);
    }
    backend.stopCapture();
    monitor.stopMonitoring();
}

void MacroCoreTests::globalInputMonitorStopWakeup()
{
    auto fakeBackend = std::make_unique<FakeGlobalInputBackend>();
    FakeGlobalInputBackend *backendPtr = fakeBackend.get();
    backendPtr->discoveredDevices = {
        makeDevice(QStringLiteral("/dev/input/event52"), QStringLiteral("USB Keyboard"), QStringLiteral("keyboard"), true)
    };

    GlobalInputMonitor monitor(std::move(fakeBackend));
    QVERIFY(monitor.startMonitoring());

    QElapsedTimer timer;
    timer.start();
    monitor.stopMonitoring();

    QVERIFY2(timer.elapsed() < 500, "Global input monitor stop did not wake promptly.");
    QVERIFY(!backendPtr->closedFds.isEmpty());
}

void MacroCoreTests::loopDisabledStopsAfterOneCompletion()
{
    PlaybackLoopController controller;
    controller.setLoopEnabled(false);

    const quint64 token = controller.startSequence();
    QVERIFY(!controller.shouldRestartAfterFinish(token, true, false));
}

void MacroCoreTests::loopEnabledRestartsAfterSuccess()
{
    PlaybackLoopController controller;
    controller.setLoopEnabled(true);

    const quint64 token = controller.startSequence();
    QVERIFY(controller.shouldRestartAfterFinish(token, true, false));
    QVERIFY(controller.shouldRestartAfterFinish(token, true, false));
}

void MacroCoreTests::stopDuringLoopPreventsNextIteration()
{
    PlaybackLoopController controller;
    controller.setLoopEnabled(true);

    const quint64 token = controller.startSequence();
    controller.requestStop();
    QVERIFY(!controller.shouldRestartAfterFinish(token, true, false));
}

void MacroCoreTests::playbackErrorStopsLoop()
{
    PlaybackLoopController controller;
    controller.setLoopEnabled(true);

    const quint64 token = controller.startSequence();
    QVERIFY(!controller.shouldRestartAfterFinish(token, false, false));
}

void MacroCoreTests::disablingLoopDuringActiveIterationStopsAfterCurrentPass()
{
    PlaybackLoopController controller;
    controller.setLoopEnabled(true);

    const quint64 token = controller.startSequence();
    controller.setLoopEnabled(false);
    QVERIFY(!controller.shouldRestartAfterFinish(token, true, false));
}

void MacroCoreTests::fileChooserResponseSuccess()
{
    const FileChooserPortal::ParsedResponse parsed = FileChooserPortal::parseResponse(
        0,
        {{QStringLiteral("uris"), QStringList{QStringLiteral("file:///tmp/example.catmacro")}}});

    QCOMPARE(parsed.disposition, FileChooserPortal::ResponseDisposition::Accepted);
    QCOMPARE(parsed.url, QUrl(QStringLiteral("file:///tmp/example.catmacro")));
    QVERIFY(parsed.error.isEmpty());
}

void MacroCoreTests::fileChooserResponseCancelled()
{
    const FileChooserPortal::ParsedResponse parsed = FileChooserPortal::parseResponse(1, {});
    QCOMPARE(parsed.disposition, FileChooserPortal::ResponseDisposition::Cancelled);
    QVERIFY(!parsed.url.isValid());
}

void MacroCoreTests::fileChooserResponseMalformed()
{
    FileChooserPortal::ParsedResponse parsed = FileChooserPortal::parseResponse(0, {});
    QCOMPARE(parsed.disposition, FileChooserPortal::ResponseDisposition::Failed);
    QVERIFY(parsed.error.contains(QStringLiteral("usable URI")));

    parsed = FileChooserPortal::parseResponse(
        0,
        {{QStringLiteral("uris"), QStringList{QStringLiteral("file:///tmp/a.catmacro"), QStringLiteral("file:///tmp/b.catmacro")}}});
    QCOMPARE(parsed.disposition, FileChooserPortal::ResponseDisposition::Failed);
    QVERIFY(parsed.error.contains(QStringLiteral("usable URI")));
}

void MacroCoreTests::interfaceModeDefaultsRegularAndPersists()
{
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::SystemScope, settingsDirectory.path());

    Settings initialSettings;
    QVERIFY(!initialSettings.compactInterface());
    initialSettings.setCompactInterface(true);

    Settings reloadedSettings;
    QVERIFY(reloadedSettings.compactInterface());

    QSettings storedSettings(QStringLiteral("CatClicker"), QStringLiteral("CatClicker"));
    QCOMPARE(storedSettings.value(QStringLiteral("ui/interfaceMode")).toString(), QStringLiteral("Compact"));
}

void MacroCoreTests::secondInstanceNotifiesPrimaryAndExits()
{
    const QString serverName = QStringLiteral("catclicker-test-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::Id128));

    SingleInstanceCoordinator primary(serverName);
    const auto primaryResult = primary.start();
    if (primaryResult == SingleInstanceCoordinator::StartResult::Error
        && primary.errorString().contains(QStringLiteral("Unknown error 1"))) {
        QSKIP("The execution sandbox does not permit Unix-domain local sockets.");
    }
    QVERIFY2(primaryResult == SingleInstanceCoordinator::StartResult::Primary,
             qPrintable(primary.errorString()));
    QSignalSpy activationSpy(&primary, &SingleInstanceCoordinator::activationRequested);

    SingleInstanceCoordinator secondary(serverName);
    QCOMPARE(secondary.start(), SingleInstanceCoordinator::StartResult::Secondary);
    QTRY_COMPARE(activationSpy.count(), 1);
}

void MacroCoreTests::versionIsExposedInSafeDiagnostics()
{
    QCoreApplication::setApplicationVersion(QStringLiteral(CATCLICKER_VERSION));
    Diagnostics diagnostics;
    PortalCapabilities capabilities;
    MacroDisplayInfo display;
    EvdevDeviceInspector inspector;
    GlobalInputMonitor monitor;
    MockInputSenderBackend backend;
    const QString report = diagnostics.generateReport(capabilities, display, false, true,
                                                       QStringLiteral("unavailable"), inspector,
                                                       monitor, {}, {}, &backend);
    QVERIFY(report.contains(QStringLiteral("Version: %1").arg(QStringLiteral(CATCLICKER_VERSION))));
    QVERIFY(report.contains(QStringLiteral("Build commit: %1").arg(QStringLiteral(CATCLICKER_GIT_COMMIT))));
}

QTEST_MAIN(MacroCoreTests)
#include "MacroCoreTests.moc"
