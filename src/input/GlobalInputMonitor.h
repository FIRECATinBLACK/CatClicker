#pragma once

#include "../hotkeys/GlobalShortcutManager.h"
#include "EvdevDeviceInspector.h"
#include "GlobalInputEvent.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QStringList>

#include <linux/input.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct pollfd;

namespace CatClicker {

class GlobalInputBackend {
public:
    virtual ~GlobalInputBackend() = default;

    virtual QList<EvdevDeviceInfo> discoverDevices() = 0;
    virtual int openDevice(const QString &path, int flags, int *errorCode) = 0;
    virtual ssize_t readBytes(int fd, void *buffer, size_t maxBytes, int *errorCode) = 0;
    virtual int pollEvents(struct pollfd *fds, std::size_t count, int timeoutMs, int *errorCode) = 0;
    virtual bool createWakePipe(int pipeFds[2], int *errorCode) = 0;
    virtual void closeFd(int fd) = 0;
    virtual bool writeWakeByte(int fd) = 0;
    virtual qint64 monotonicTimeUs() const = 0;
};

std::unique_ptr<GlobalInputBackend> createPosixGlobalInputBackend();

class GlobalInputMonitor : public QObject {
    Q_OBJECT

public:
    explicit GlobalInputMonitor(std::unique_ptr<GlobalInputBackend> backend = createPosixGlobalInputBackend(),
                                QObject *parent = nullptr);
    ~GlobalInputMonitor() override;

    void setShortcutBindings(const GlobalShortcutManager::ShortcutBinding &record,
                             const GlobalShortcutManager::ShortcutBinding &play,
                             const GlobalShortcutManager::ShortcutBinding &stop);
    bool startMonitoring();
    void stopMonitoring();
    bool isListenerActive() const;
    bool hasOpenablePhysicalDevices() const;
    bool hasPermissionProblem() const;
    bool globalHotkeysActive() const;
    QString listenerStatusText() const;
    QString hotkeyStatusText() const;
    QString recordingBackendText() const;
    int keyboardNodeCount() const;
    int pointerNodeCount() const;
    int openFailureCount() const;
    qint64 currentTimeUs() const;
    QStringList diagnosticLines() const;
    QList<EvdevDeviceInfo> devices() const;

public slots:
    void setCaptureForwardingEnabled(bool enabled);

signals:
    void globalEventCaptured(const CatClicker::GlobalInputEvent &event);
    void globalShortcutTriggered(CatClicker::GlobalShortcutManager::ShortcutAction action);
    void availabilityChanged();

private:
    struct OpenDevice {
        QString path;
        QString category;
        int fd = -1;
    };

    struct SuppressedChord {
        QSet<uint32_t> activeKeys;
    };

    std::unique_ptr<GlobalInputBackend> m_backend;
    mutable std::mutex m_stateMutex;
    QList<EvdevDeviceInfo> m_devices;
    QList<OpenDevice> m_openDevices;
    QStringList m_diagnosticLines;
    QString m_listenerStatusText = QStringLiteral("Global input unavailable");
    QString m_hotkeyStatusText = QStringLiteral("Application-only fallback");
    QString m_recordingBackendText = QStringLiteral("Qt focused fallback");
    GlobalShortcutManager::ShortcutBinding m_recordBinding;
    GlobalShortcutManager::ShortcutBinding m_playBinding;
    GlobalShortcutManager::ShortcutBinding m_stopBinding;
    QSet<uint32_t> m_shortcutRelevantKeys;
    QList<GlobalInputEvent> m_pendingShortcutKeyEvents;
    QList<SuppressedChord> m_suppressedChords;
    QSet<uint32_t> m_pressedKeys;
    int m_keyboardNodeCount = 0;
    int m_pointerNodeCount = 0;
    int m_openFailureCount = 0;
    int m_wakePipe[2] = {-1, -1};
    std::thread m_worker;
    std::atomic<bool> m_running = false;
    std::atomic<bool> m_captureForwardingEnabled = false;
    std::mutex m_relativeMotionMutex;
    GlobalInputEvent m_pendingRelativeMotion;
    bool m_hasPendingRelativeMotion = false;
    bool m_relativeMotionDeliveryPosted = false;
    quint64 m_captureForwardingGeneration = 0;
    std::atomic<quint64> m_relativeTracePostCount = 0;
    quint64 m_relativeTraceDeliveryCount = 0;

    void refreshDevicesLocked();
    void rebuildDiagnosticStateLocked();
    void rebuildRelevantShortcutKeysLocked();
    void joinWorker();
    void workerMain();
    void publishShortcutAction(GlobalShortcutManager::ShortcutAction action);
    void publishRecordableEvent(const GlobalInputEvent &event);
    void publishRelativeMotion(const GlobalInputEvent &event);
    void deliverPendingRelativeMotion(quint64 generation);
    void processInputEvent(const input_event &event, const QString &devicePath, qint64 timeUs);
    void processKeyLikeEvent(uint32_t code, bool pressed, bool autoRepeat, bool mouseButton, const QString &devicePath, qint64 timeUs);
    void processRelativeEvent(quint16 code, qint32 value, const QString &devicePath, qint64 timeUs);
    void retireOpenDevice(int fd);
    GlobalShortcutManager::ShortcutAction matchShortcutActionLocked(uint32_t keyCode) const;
    QSet<uint32_t> pressedChordKeysForActionLocked(GlobalShortcutManager::ShortcutAction action) const;
    bool isSuppressedKeyLocked(uint32_t keyCode) const;
    void removeReleasedSuppressionLocked(uint32_t keyCode);
    QList<GlobalInputEvent> takeFlushablePendingKeyEventsLocked();
};

}
