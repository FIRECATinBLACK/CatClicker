#pragma once

#include "CursorTracker.h"
#include "GlobalInputMonitor.h"
#include "InputCaptureBackend.h"

#include <QtCore/QSet>
#include <QtCore/QVector>

namespace CatClicker {

class CosmicCursorPositionProvider;

struct CursorSamplerHealth {
    quint64 captureSessionNumber = 0;
    quint64 relMovementTriggers = 0;
    quint64 refreshRequests = 0;
    quint64 refreshCoalesced = 0;
    quint64 refreshCompletions = 0;
    quint64 resolvedSampleAttempts = 0;
    quint64 resolvedCoordinateChanges = 0;
    quint64 resolvedIdenticalCoordinates = 0;
    quint64 duplicateMoveSuppressions = 0;
    quint64 consecutiveIdenticalResolvedSamples = 0;
    quint64 staleHardRefreshRequests = 0;
    quint64 samplesDelivered = 0;
    quint64 deferredButtonEvents = 0;
    quint64 deferredScrollEvents = 0;
    quint64 unresolvedMouseEventsDroppedOnStop = 0;
    qint64 lastRelTriggerMonotonicUs = 0;
    qint64 lastRefreshRequestMonotonicUs = 0;
    qint64 lastRefreshCompletionMonotonicUs = 0;
    qint64 lastDuplicateSuppressionMonotonicUs = 0;
    qint64 lastDeliveredSampleMonotonicUs = 0;
    bool refreshOutstanding = false;
    bool movementPending = false;
    bool followUpPending = false;
    int pendingMouseEventCount = 0;
};

class EvdevCaptureBackend : public InputCaptureBackend {
    Q_OBJECT

public:
    explicit EvdevCaptureBackend(GlobalInputMonitor *monitor,
                                 const CursorPositionProvider *cursorPositionProvider = nullptr,
                                 QObject *parent = nullptr);

    bool isAvailable() const override;
    QString unavailabilityReason() const override;
    bool startCapture() override;
    void stopCapture() override;
    CursorSamplerHealth samplerHealthSnapshot() const;

private:
    enum class SamplerState {
        IdleValid,
        RefreshOutstanding,
    };

    void handleGlobalEvent(const CatClicker::GlobalInputEvent &event);
    void handleSampledCursorPosition(const QPointF &position);
    void requestMovementSample(qint64 timestampUs, qint64 monotonicUs);
    bool emitResolvedPendingEvents(const QPointF &position);
    void flushPendingMouseEventsUnanchored();
    void resetSamplerState();
    void emitReleaseEventsForHeldState(qint64 timestampUs);

    GlobalInputMonitor *m_monitor = nullptr;
    const CursorPositionProvider *m_cursorPositionProvider = nullptr;
    CosmicCursorPositionProvider *m_cosmicSamplerProvider = nullptr;
    bool m_samplerEnabled = false;
    SamplerState m_samplerState = SamplerState::IdleValid;
    bool m_hasCurrentMovement = false;
    bool m_followUpRefreshRequired = false;
    qint64 m_currentMovementTimestampUs = 0;
    qint64 m_followUpMovementTimestampUs = 0;
    QVector<MacroEvent> m_pendingMouseEvents;
    CursorSamplerHealth m_samplerHealth;
    quint64 m_captureSessionNumber = 0;
    bool m_capturing = false;
    qint64 m_captureStartTimeUs = 0;
    qint64 m_lastTimestampUs = 0;
    QSet<uint32_t> m_heldKeys;
    QSet<int> m_heldButtons;
    QPointF m_lastRecordedCursorPosition;
    bool m_hasLastRecordedCursorPosition = false;
    quint64 m_duplicateTraceCount = 0;
    quint64 m_resolvedAttemptsAtLastHardRefresh = 0;
};

}
