#include "MacroPlayer.h"

#include <chrono>

namespace CatClicker {

namespace {

constexpr auto kAnchoredClickSettleDelay = std::chrono::milliseconds(16);
constexpr qint64 kSmoothPointerIntervalUs = 8333;

bool requiresTrustedAnchor(const MacroEvent &event)
{
    return event.type == MacroEventType::MouseButton || event.type == MacroEventType::Scroll;
}

}

MacroPlayer::MacroPlayer(QObject *parent)
    : QObject(parent)
{
}

MacroPlayer::~MacroPlayer()
{
    shutdown();
}

QVector<MacroPlayer::ScheduledEvent> MacroPlayer::buildSchedule(
    const Macro &macro, double speed, bool smoothMouseMovement) const
{
    QVector<ScheduledEvent> schedule;
    schedule.reserve(macro.events.size());

    for (qsizetype index = 0; index < macro.events.size(); ++index) {
        const MacroEvent &event = macro.events.at(index);
        // Button and wheel injection is only valid with a compositor-derived absolute
        // anchor. Unsafe legacy or unresolved events never enter the playback schedule.
        if (requiresTrustedAnchor(event) && !event.hasCursorAnchor) {
            continue;
        }

        if (smoothMouseMovement && index > 0
            && event.type == MacroEventType::MouseMove
            && macro.events.at(index - 1).type == MacroEventType::MouseMove) {
            const MacroEvent &previous = macro.events.at(index - 1);
            const qint64 previousDueUs = scaledTimestampUs(previous.timeUs, speed);
            const qint64 currentDueUs = scaledTimestampUs(event.timeUs, speed);
            const qint64 durationUs = currentDueUs - previousDueUs;
            for (qint64 dueUs = previousDueUs + kSmoothPointerIntervalUs;
                 durationUs > 0 && dueUs < currentDueUs;
                 dueUs += kSmoothPointerIntervalUs) {
                const double progress = static_cast<double>(dueUs - previousDueUs)
                    / static_cast<double>(durationUs);
                MacroEvent interpolated = MacroEvent::mouseMove(
                    previous.timeUs
                        + static_cast<qint64>((event.timeUs - previous.timeUs) * progress),
                    previous.x + (event.x - previous.x) * progress,
                    previous.y + (event.y - previous.y) * progress);
                schedule.push_back({interpolated, dueUs});
            }
        }

        schedule.push_back(ScheduledEvent{
            .event = event,
            .dueUs = scaledTimestampUs(event.timeUs, speed),
        });
    }

    return schedule;
}

qint64 MacroPlayer::scaledTimestampUs(qint64 originalUs, double speed)
{
    if (speed <= 0.0) {
        return originalUs;
    }

    return static_cast<qint64>(static_cast<double>(originalUs) / speed);
}

bool MacroPlayer::startPlayback(const Macro &macro, double speed, InputSenderBackend *backend,
                                bool smoothMouseMovement)
{
    if (!backend || m_playing.load()) {
        return false;
    }

    joinWorker();
    clearHeldState();

    m_stopRequested.store(false);
    m_playing.store(true);

    QVector<ScheduledEvent> schedule = buildSchedule(macro, speed, smoothMouseMovement);
    m_worker = std::thread([this, schedule = std::move(schedule), backend]() mutable {
        runPlayback(std::move(schedule), backend);
    });

    emit playbackStarted();
    return true;
}

void MacroPlayer::stopPlayback()
{
    m_stopRequested.store(true);
    m_stopCondition.notify_all();
}

void MacroPlayer::shutdown()
{
    stopPlayback();
    joinWorker();
}

bool MacroPlayer::isPlaying() const
{
    return m_playing.load();
}

void MacroPlayer::markHeld(const MacroEvent &event)
{
    std::lock_guard<std::mutex> locker(m_heldMutex);

    if (event.type == MacroEventType::Key) {
        if (event.pressed) {
            m_heldKeys.insert(event.keyCode, true);
        } else {
            m_heldKeys.remove(event.keyCode);
        }
    }

    if (event.type == MacroEventType::MouseButton) {
        if (event.pressed) {
            if (event.hasCursorAnchor) {
                m_heldButtons.insert(event.button, QPointF(event.anchorX, event.anchorY));
            }
        } else {
            m_heldButtons.remove(event.button);
        }
    }
}

QVector<MacroEvent> MacroPlayer::buildEmergencyReleaseEvents() const
{
    std::lock_guard<std::mutex> locker(m_heldMutex);

    QVector<MacroEvent> releases;
    releases.reserve(m_heldKeys.size() + m_heldButtons.size());

    for (auto it = m_heldKeys.cbegin(); it != m_heldKeys.cend(); ++it) {
        releases.push_back(MacroEvent::keyEvent(0, it.key(), false));
    }

    for (auto it = m_heldButtons.cbegin(); it != m_heldButtons.cend(); ++it) {
        releases.push_back(MacroEvent::mouseButton(
            0, it.key(), false, it.value().x(), it.value().y(), true));
    }

    return releases;
}

void MacroPlayer::clearHeldState()
{
    std::lock_guard<std::mutex> locker(m_heldMutex);
    m_heldKeys.clear();
    m_heldButtons.clear();
}

void MacroPlayer::joinWorker()
{
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void MacroPlayer::runPlayback(QVector<ScheduledEvent> schedule, InputSenderBackend *backend)
{
    using Clock = std::chrono::steady_clock;

    const Clock::time_point start = Clock::now();

    for (const ScheduledEvent &scheduled : schedule) {
        const Clock::time_point due = start + std::chrono::microseconds(scheduled.dueUs);
        std::unique_lock<std::mutex> lock(m_stateMutex);
        m_stopCondition.wait_until(lock, due, [this]() {
            return m_stopRequested.load();
        });
        lock.unlock();

        if (m_stopRequested.load()) {
            releaseHeldState(backend, QStringLiteral("Playback stopped by user."), false, true);
            return;
        }

        if (!dispatchEvent(scheduled.event, backend)) {
            releaseHeldState(backend,
                             QStringLiteral("Playback backend failed while injecting an event: %1")
                                 .arg(backend ? backend->statusText() : QStringLiteral("no backend available")),
                             false,
                             false);
            return;
        }

        markHeld(scheduled.event);
        emit playbackProgress(scheduled.dueUs);
    }

    releaseHeldState(backend, QStringLiteral("Playback completed."), true, false);
}

bool MacroPlayer::dispatchEvent(const MacroEvent &event, InputSenderBackend *backend)
{
    switch (event.type) {
    case MacroEventType::Key:
        return backend->sendKey(event.keyCode, event.pressed);
    case MacroEventType::MouseMove:
        return backend->movePointerAbsolute(event.x, event.y);
    case MacroEventType::MouseButton:
        if (!event.hasCursorAnchor) {
            return true;
        }
        // The saved anchor is authoritative even when smoothing inserted movements.
        if (!backend->movePointerAbsolute(event.anchorX, event.anchorY)) {
            return false;
        }
        if (event.pressed && !waitForAnchorSettle()) {
            return false;
        }
        return backend->sendButton(event.button, event.pressed);
    case MacroEventType::Scroll:
        if (!event.hasCursorAnchor) {
            return true;
        }
        if (!backend->movePointerAbsolute(event.anchorX, event.anchorY)) {
            return false;
        }
        return backend->sendScroll(event.deltaX, event.deltaY);
    }

    return false;
}

bool MacroPlayer::waitForAnchorSettle()
{
    std::unique_lock<std::mutex> lock(m_stateMutex);
    m_stopCondition.wait_for(lock, kAnchoredClickSettleDelay, [this]() {
        return m_stopRequested.load();
    });
    return !m_stopRequested.load();
}

void MacroPlayer::releaseHeldState(InputSenderBackend *backend, const QString &reason, bool completed, bool stoppedByUser)
{
    if (backend) {
        const QVector<MacroEvent> releases = buildEmergencyReleaseEvents();
        for (const MacroEvent &release : releases) {
            if (release.type == MacroEventType::Key) {
                backend->sendKey(release.keyCode, false);
            } else if (release.type == MacroEventType::MouseButton
                       && release.hasCursorAnchor
                       && backend->movePointerAbsolute(release.anchorX, release.anchorY)) {
                backend->sendButton(release.button, false);
            }
        }

        backend->releaseEverything();
    }

    clearHeldState();
    m_stopRequested.store(false);
    m_playing.store(false);
    emit playbackFinished(completed, stoppedByUser, reason);
}

}
