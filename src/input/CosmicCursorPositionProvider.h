#pragma once

#include "CursorTracker.h"
#include "../macro/Macro.h"

#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QSize>
#include <QtCore/QString>

#include <atomic>
#include <memory>
#include <thread>

namespace CatClicker {

struct CosmicCursorOutputMapping {
    int logicalX = 0;
    int logicalY = 0;
    int logicalWidth = 0;
    int logicalHeight = 0;
    int bufferWidth = 0;
    int bufferHeight = 0;
    int transform = 0;
};

struct CursorProviderHealth {
    bool workerAlive = false;
    quint64 workerLoopCount = 0;
    quint64 dispatchCount = 0;
    quint64 prepareReadSuccessCount = 0;
    quint64 prepareReadRetryCount = 0;
    quint64 pollCount = 0;
    quint64 waylandFdReadableCount = 0;
    quint64 wakeFdReadableCount = 0;
    quint64 readEventsSuccessCount = 0;
    quint64 readEventsFailureCount = 0;
    quint64 dispatchPendingCount = 0;
    quint64 flushFailureCount = 0;
    int wlDisplayError = 0;
    quint64 enterCount = 0;
    quint64 leaveCount = 0;
    quint64 positionCallbackCount = 0;
    quint64 hotspotCount = 0;
    quint64 snapshotPublishCount = 0;
    quint64 syncDoneCount = 0;
    quint64 cursorSessionGeneration = 0;
    quint64 cursorSessionRecreateCount = 0;
    quint64 positionAfterRecreateCount = 0;
    bool cursorSessionRefreshOutstanding = false;
    qint64 latestPositionCallbackMonotonicUs = 0;
    qint64 latestRefreshRequestMonotonicUs = 0;
    CursorSnapshot latestPublished;
};

class CosmicCursorState {
public:
    void entered();
    bool updatePosition(const QPointF &position);
    void left();
    void stopped();
    bool hasPosition() const;
    QPointF position() const;

private:
    bool m_inside = false;
    bool m_hasPosition = false;
    QPointF m_position;
};

bool mapCosmicCursorPosition(const CosmicCursorOutputMapping &mapping,
                             const QPointF &bufferPosition,
                             QPointF *logicalDesktopPosition,
                             QString *error = nullptr);

bool shouldUseDirectCosmicCursorProvider(const QString &currentDesktop,
                                         const QString &sessionType,
                                         const QString &waylandDisplay,
                                         bool buildSupport,
                                         bool explicitlyDisabled = false);

class CosmicCursorPositionProvider final : public QObject, public CursorPositionProvider {
    Q_OBJECT

public:
    struct Runtime;

    explicit CosmicCursorPositionProvider(QObject *parent = nullptr);
    ~CosmicCursorPositionProvider() override;

    static bool buildSupported();
    bool hasBuildSupport() const;
    bool start(const MacroDisplayInfo &display, int outputCount);
    void stop();

    CursorSnapshot cursorSnapshot() const override;
    CursorProviderHealth healthSnapshot() const;
    void requestHealthProbe();
    bool requestCursorSessionRefresh();
    bool supersedeCursorSessionRefresh();
    QString statusText() const;
    QString diagnosticState() const;

    // Protocol callbacks; public only so generated C listener thunks can forward safely.
    void applyEnter();
    void applyEnterForGeneration(quint64 generation);
    void applyLeave();
    void applyLeaveForGeneration(quint64 generation);
    void applyPosition(const QPointF &rawPosition, const CosmicCursorOutputMapping &mapping);
    void applyPositionForGeneration(const QPointF &rawPosition,
                                    const CosmicCursorOutputMapping &mapping,
                                    quint64 generation);
    void applyHotspot();
    void applySyncDone();
    void applyCursorSessionRecreated(quint64 generation);
    void applyStopped(const QString &reason);

signals:
    void trackingChanged();
    void cursorPositionChanged(const QPointF &position);

private:
    std::unique_ptr<Runtime> m_runtime;
    mutable QMutex m_mutex;
    std::atomic<quint64> m_snapshotSequence = 0;
    std::atomic_bool m_snapshotValid = false;
    std::atomic<double> m_snapshotX = 0.0;
    std::atomic<double> m_snapshotY = 0.0;
    bool m_cursorInside = false;
    QString m_status = QStringLiteral("disabled");
    QString m_detail = QStringLiteral("Direct COSMIC cursor metadata tracking has not started.");
    std::atomic_bool m_stopRequested = false;
    std::atomic_bool m_workerAlive = false;
    std::atomic<quint64> m_workerLoopCount = 0;
    std::atomic<quint64> m_dispatchCount = 0;
    std::atomic<quint64> m_prepareReadSuccessCount = 0;
    std::atomic<quint64> m_prepareReadRetryCount = 0;
    std::atomic<quint64> m_pollCount = 0;
    std::atomic<quint64> m_waylandFdReadableCount = 0;
    std::atomic<quint64> m_wakeFdReadableCount = 0;
    std::atomic<quint64> m_readEventsSuccessCount = 0;
    std::atomic<quint64> m_readEventsFailureCount = 0;
    std::atomic<quint64> m_dispatchPendingCount = 0;
    std::atomic<quint64> m_flushFailureCount = 0;
    std::atomic<int> m_wlDisplayError = 0;
    std::atomic<quint64> m_enterCount = 0;
    std::atomic<quint64> m_leaveCount = 0;
    std::atomic<quint64> m_positionCallbackCount = 0;
    std::atomic<quint64> m_hotspotCount = 0;
    std::atomic<quint64> m_snapshotPublishCount = 0;
    std::atomic<quint64> m_syncDoneCount = 0;
    std::atomic<quint64> m_cursorSessionGeneration = 1;
    std::atomic<quint64> m_cursorSessionRecreateCount = 0;
    std::atomic<quint64> m_positionAfterRecreateCount = 0;
    std::atomic_bool m_cursorSessionRefreshOutstanding = false;
    std::atomic_bool m_healthProbeRequested = false;
    std::atomic_bool m_cursorSessionRefreshRequested = false;
    std::atomic<qint64> m_latestPositionCallbackMonotonicUs = 0;
    std::atomic<qint64> m_latestRefreshRequestMonotonicUs = 0;
    int m_wakePipe[2] = {-1, -1};
    std::thread m_thread;

    void run(MacroDisplayInfo display);
    void setStatus(const QString &state, const QString &detail);
    void publishSnapshot(const CursorSnapshot &snapshot);
    void wakeWorker();
};

}
