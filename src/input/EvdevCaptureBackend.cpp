#include "EvdevCaptureBackend.h"

#include "CosmicCursorPositionProvider.h"

#include <QtCore/QDebug>
#include <QtCore/QThread>

#include <algorithm>

namespace {

bool traceRecordingCursorEnabled()
{
    static const bool enabled = qEnvironmentVariableIntValue("CATCLICKER_TRACE_RECORDING_CURSOR") == 1;
    return enabled;
}

QString cursorSampleText(const CatClicker::CursorSnapshot &snapshot)
{
    return snapshot.valid
        ? QStringLiteral("%1,%2").arg(snapshot.position.x()).arg(snapshot.position.y())
        : QStringLiteral("none");
}

QString relativeAxisName(uint32_t code)
{
    return code == REL_X ? QStringLiteral("REL_X")
                         : code == REL_Y ? QStringLiteral("REL_Y")
                                         : QStringLiteral("REL_%1").arg(code);
}

QString currentThreadId()
{
    return QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
}

}

namespace CatClicker {

EvdevCaptureBackend::EvdevCaptureBackend(GlobalInputMonitor *monitor,
                                         const CursorPositionProvider *cursorPositionProvider,
                                         QObject *parent)
    : InputCaptureBackend(parent)
    , m_monitor(monitor)
    , m_cursorPositionProvider(cursorPositionProvider)
{
    m_cosmicSamplerProvider = dynamic_cast<CosmicCursorPositionProvider *>(
        const_cast<CursorPositionProvider *>(cursorPositionProvider));
    m_samplerEnabled = m_cosmicSamplerProvider
        && qEnvironmentVariableIntValue("CATCLICKER_DISABLE_COSMIC_CURSOR_SAMPLER") != 1;
    if (m_monitor) {
        connect(m_monitor, &GlobalInputMonitor::globalEventCaptured,
                this, &EvdevCaptureBackend::handleGlobalEvent);
    }
    if (m_cosmicSamplerProvider) {
        connect(m_cosmicSamplerProvider, &CosmicCursorPositionProvider::cursorPositionChanged,
                this, &EvdevCaptureBackend::handleSampledCursorPosition);
    }
}

bool EvdevCaptureBackend::isAvailable() const
{
    return m_monitor && m_monitor->hasOpenablePhysicalDevices();
}

QString EvdevCaptureBackend::unavailabilityReason() const
{
    if (!m_monitor) {
        return QStringLiteral("Global input monitor is not configured.");
    }

    if (m_monitor->hasPermissionProblem()) {
        return QStringLiteral("Global input unavailable: physical input devices are not readable by this user. Run the setup helper, then log out/in.");
    }

    return QStringLiteral("Global input unavailable: no readable physical keyboard or pointer devices were opened.");
}

bool EvdevCaptureBackend::startCapture()
{
    if (!isAvailable()) {
        emit backendError(unavailabilityReason());
        return false;
    }

    if (m_capturing) {
        return true;
    }

    m_capturing = true;
    m_captureStartTimeUs = m_monitor->currentTimeUs();
    m_lastTimestampUs = 0;
    m_heldKeys.clear();
    m_heldButtons.clear();
    m_lastRecordedCursorPosition = {};
    m_hasLastRecordedCursorPosition = false;
    m_duplicateTraceCount = 0;
    m_samplerHealth = {};
    m_samplerHealth.captureSessionNumber = ++m_captureSessionNumber;
    resetSamplerState();
    m_monitor->setCaptureForwardingEnabled(true);
    return true;
}

void EvdevCaptureBackend::stopCapture()
{
    if (!m_capturing) {
        return;
    }

    if (m_monitor) {
        m_monitor->setCaptureForwardingEnabled(false);
    }
    m_capturing = false;

    const qint64 nowUs = m_monitor ? m_monitor->currentTimeUs() : m_captureStartTimeUs;
    const qint64 timestampUs = std::max<qint64>(m_lastTimestampUs, nowUs - m_captureStartTimeUs);
    flushPendingMouseEventsUnanchored();
    if (m_hasCurrentMovement) {
        ++m_samplerHealth.unresolvedMouseEventsDroppedOnStop;
    }
    if (m_followUpRefreshRequired) {
        ++m_samplerHealth.unresolvedMouseEventsDroppedOnStop;
    }
    resetSamplerState();
    emitReleaseEventsForHeldState(timestampUs);
    m_heldKeys.clear();
    m_heldButtons.clear();
}

CursorSamplerHealth EvdevCaptureBackend::samplerHealthSnapshot() const
{
    CursorSamplerHealth health = m_samplerHealth;
    health.refreshOutstanding = m_samplerState == SamplerState::RefreshOutstanding;
    health.movementPending = m_hasCurrentMovement;
    health.followUpPending = m_followUpRefreshRequired;
    health.pendingMouseEventCount = m_pendingMouseEvents.size();
    return health;
}

void EvdevCaptureBackend::handleGlobalEvent(const CatClicker::GlobalInputEvent &event)
{
    if (!m_capturing) {
        return;
    }

    const qint64 relativeUs = std::max<qint64>(0, event.timeUs - m_captureStartTimeUs);
    m_lastTimestampUs = std::max(m_lastTimestampUs, relativeUs);

    switch (event.type) {
    case GlobalInputEventType::Key:
        if (event.autoRepeat) {
            return;
        }
        if (event.pressed) {
            m_heldKeys.insert(event.code);
        } else {
            m_heldKeys.remove(event.code);
        }
        emit eventCaptured(MacroEvent::keyEvent(relativeUs, event.code, event.pressed));
        break;
    case GlobalInputEventType::MouseButton:
    {
        if (m_samplerEnabled && m_samplerState == SamplerState::RefreshOutstanding) {
            if (m_pendingMouseEvents.size() >= 256) {
                flushPendingMouseEventsUnanchored();
            }
            m_pendingMouseEvents.push_back(MacroEvent::mouseButton(
                relativeUs, static_cast<int>(event.code), event.pressed, 0.0, 0.0, false));
            ++m_samplerHealth.deferredButtonEvents;
            if (event.pressed) {
                m_heldButtons.insert(static_cast<int>(event.code));
            } else {
                m_heldButtons.remove(static_cast<int>(event.code));
            }
            break;
        }
        const CursorSnapshot snapshot = m_cursorPositionProvider
            ? m_cursorPositionProvider->cursorSnapshot() : CursorSnapshot{};
        const bool hasCursorAnchor = snapshot.valid;
        const QPointF cursorPosition = snapshot.position;
        if (traceRecordingCursorEnabled()) {
            qInfo().noquote() << QStringLiteral("[record-cursor] thread=%1 BTN_%2 %3 provider=%4 valid=%5 anchor=%6")
                                     .arg(currentThreadId())
                                     .arg(event.code)
                                     .arg(event.pressed ? QStringLiteral("DOWN") : QStringLiteral("UP"))
                                     .arg(cursorSampleText(snapshot))
                                     .arg(snapshot.valid ? 1 : 0)
                                     .arg(hasCursorAnchor ? 1 : 0);
        }
        if (!hasCursorAnchor) {
            break;
        }
        if (event.pressed) {
            m_heldButtons.insert(static_cast<int>(event.code));
        } else {
            m_heldButtons.remove(static_cast<int>(event.code));
        }
        emit eventCaptured(MacroEvent::mouseButton(relativeUs,
                                                   static_cast<int>(event.code),
                                                   event.pressed,
                                                   cursorPosition.x(),
                                                   cursorPosition.y(),
                                                   hasCursorAnchor));
        break;
    }
    case GlobalInputEventType::Scroll:
    {
        if (m_samplerEnabled && m_samplerState == SamplerState::RefreshOutstanding) {
            if (m_pendingMouseEvents.size() >= 256) {
                flushPendingMouseEventsUnanchored();
            }
            m_pendingMouseEvents.push_back(MacroEvent::scroll(
                relativeUs, event.deltaX, event.deltaY, 0.0, 0.0, false));
            ++m_samplerHealth.deferredScrollEvents;
            break;
        }
        const CursorSnapshot snapshot = m_cursorPositionProvider
            ? m_cursorPositionProvider->cursorSnapshot() : CursorSnapshot{};
        const bool hasCursorAnchor = snapshot.valid;
        const QPointF cursorPosition = snapshot.position;
        if (traceRecordingCursorEnabled()) {
            qInfo().noquote() << QStringLiteral("[record-cursor] thread=%1 SCROLL provider=%2 valid=%3 anchor=%4")
                                     .arg(currentThreadId(), cursorSampleText(snapshot))
                                     .arg(snapshot.valid ? 1 : 0)
                                     .arg(hasCursorAnchor ? 1 : 0);
        }
        if (!hasCursorAnchor) {
            break;
        }
        emit eventCaptured(MacroEvent::scroll(relativeUs,
                                              event.deltaX,
                                              event.deltaY,
                                              cursorPosition.x(),
                                              cursorPosition.y(),
                                              hasCursorAnchor));
        break;
    }
    case GlobalInputEventType::RelativeMotion:
    {
        if (m_samplerEnabled) {
            // REL values are triggers only; absolute coordinates always come from the
            // current generation of COSMIC cursor metadata.
            requestMovementSample(relativeUs);
            break;
        }
        const CursorSnapshot snapshot = m_cursorPositionProvider
            ? m_cursorPositionProvider->cursorSnapshot() : CursorSnapshot{};
        const bool duplicate = snapshot.valid && m_hasLastRecordedCursorPosition
                               && snapshot.position == m_lastRecordedCursorPosition;
        const bool emitMove = snapshot.valid && !duplicate;
        if (duplicate) {
            ++m_duplicateTraceCount;
        } else {
            m_duplicateTraceCount = 0;
        }
        if (traceRecordingCursorEnabled()
            && (!duplicate || m_duplicateTraceCount == 1 || m_duplicateTraceCount % 100 == 0)) {
            qInfo().noquote() << QStringLiteral("[record-cursor] thread=%1 %2 provider=%3 valid=%4 emitMove=%5 duplicate=%6")
                                     .arg(currentThreadId(), relativeAxisName(event.code))
                                     .arg(cursorSampleText(snapshot))
                                     .arg(snapshot.valid ? 1 : 0)
                                     .arg(emitMove ? 1 : 0)
                                     .arg(duplicate ? 1 : 0);
        }
        if (emitMove) {
                m_lastRecordedCursorPosition = snapshot.position;
                m_hasLastRecordedCursorPosition = true;
                emit eventCaptured(MacroEvent::mouseMove(relativeUs, snapshot.position.x(), snapshot.position.y()));
        }
        break;
    }
    }
}

void EvdevCaptureBackend::requestMovementSample(qint64 timestampUs)
{
    ++m_samplerHealth.relMovementTriggers;
    if (m_samplerState == SamplerState::IdleValid) {
        m_currentMovementTimestampUs = timestampUs;
        m_hasCurrentMovement = true;
        if (m_cosmicSamplerProvider && m_cosmicSamplerProvider->supersedeCursorSessionRefresh()) {
            m_samplerState = SamplerState::RefreshOutstanding;
            ++m_samplerHealth.refreshRequests;
        } else {
            m_hasCurrentMovement = false;
        }
        return;
    }

    m_followUpRefreshRequired = true;
    m_followUpMovementTimestampUs = timestampUs;
    ++m_samplerHealth.refreshCoalesced;
}

void EvdevCaptureBackend::handleSampledCursorPosition(const QPointF &position)
{
    if (!m_capturing || !m_samplerEnabled
        || m_samplerState != SamplerState::RefreshOutstanding) {
        return;
    }

    const CursorSnapshot snapshot = m_cursorPositionProvider
        ? m_cursorPositionProvider->cursorSnapshot() : CursorSnapshot{};
    if (!snapshot.valid || snapshot.position != position) {
        return;
    }

    ++m_samplerHealth.refreshCompletions;
    emitResolvedPendingEvents(position);

    if (m_followUpRefreshRequired) {
        m_currentMovementTimestampUs = m_followUpMovementTimestampUs;
        m_hasCurrentMovement = true;
        m_followUpRefreshRequired = false;
        if (m_cosmicSamplerProvider->requestCursorSessionRefresh()) {
            ++m_samplerHealth.refreshRequests;
            m_samplerState = SamplerState::RefreshOutstanding;
        } else {
            m_hasCurrentMovement = false;
            m_samplerState = SamplerState::IdleValid;
        }
    } else {
        m_samplerState = SamplerState::IdleValid;
    }
}

void EvdevCaptureBackend::emitResolvedPendingEvents(const QPointF &position)
{
    QVector<MacroEvent> resolved;
    resolved.reserve(m_pendingMouseEvents.size() + (m_hasCurrentMovement ? 1 : 0));
    if (m_hasCurrentMovement) {
        resolved.push_back(MacroEvent::mouseMove(
            m_currentMovementTimestampUs, position.x(), position.y()));
    }
    for (MacroEvent event : std::as_const(m_pendingMouseEvents)) {
        event.hasCursorAnchor = true;
        event.anchorX = position.x();
        event.anchorY = position.y();
        resolved.push_back(event);
    }
    std::stable_sort(resolved.begin(), resolved.end(), [](const MacroEvent &left, const MacroEvent &right) {
        return left.timeUs < right.timeUs;
    });
    for (const MacroEvent &event : std::as_const(resolved)) {
        if (event.type == MacroEventType::MouseMove) {
            const bool duplicate = m_hasLastRecordedCursorPosition
                && event.x == m_lastRecordedCursorPosition.x()
                && event.y == m_lastRecordedCursorPosition.y();
            if (duplicate) {
                continue;
            }
            m_lastRecordedCursorPosition = QPointF(event.x, event.y);
            m_hasLastRecordedCursorPosition = true;
            ++m_samplerHealth.samplesDelivered;
        }
        emit eventCaptured(event);
    }
    m_pendingMouseEvents.clear();
    m_hasCurrentMovement = false;
}

void EvdevCaptureBackend::flushPendingMouseEventsUnanchored()
{
    m_samplerHealth.unresolvedMouseEventsDroppedOnStop +=
        static_cast<quint64>(m_pendingMouseEvents.size());
    m_pendingMouseEvents.clear();
}

void EvdevCaptureBackend::resetSamplerState()
{
    m_samplerState = SamplerState::IdleValid;
    m_hasCurrentMovement = false;
    m_followUpRefreshRequired = false;
    m_currentMovementTimestampUs = 0;
    m_followUpMovementTimestampUs = 0;
    m_pendingMouseEvents.clear();
}

void EvdevCaptureBackend::emitReleaseEventsForHeldState(qint64 timestampUs)
{
    const auto heldKeys = m_heldKeys.values();
    for (uint32_t keyCode : heldKeys) {
        emit eventCaptured(MacroEvent::keyEvent(timestampUs, keyCode, false));
    }
}

}
