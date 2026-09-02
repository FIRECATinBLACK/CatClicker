#pragma once

#include "../input/InputSenderBackend.h"

#include "Macro.h"

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QVector>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace CatClicker {

class MacroPlayer : public QObject {
    Q_OBJECT

public:
    struct ScheduledEvent {
        MacroEvent event;
        qint64 dueUs = 0;
    };

    explicit MacroPlayer(QObject *parent = nullptr);
    ~MacroPlayer() override;

    QVector<ScheduledEvent> buildSchedule(const Macro &macro, double speed,
                                          bool smoothMouseMovement = false) const;
    static qint64 scaledTimestampUs(qint64 originalUs, double speed);

    bool startPlayback(const Macro &macro, double speed, InputSenderBackend *backend,
                       bool smoothMouseMovement = false);
    void stopPlayback();
    void shutdown();
    bool isPlaying() const;

    void markHeld(const MacroEvent &event);
    QVector<MacroEvent> buildEmergencyReleaseEvents() const;
    void clearHeldState();

signals:
    void playbackStarted();
    void playbackProgress(qint64 elapsedUs);
    void playbackFinished(bool completed, bool stoppedByUser, const QString &message);

private:
    mutable std::mutex m_heldMutex;
    QHash<uint32_t, bool> m_heldKeys;
    QHash<int, QPointF> m_heldButtons;

    mutable std::mutex m_stateMutex;
    std::condition_variable m_stopCondition;
    std::thread m_worker;
    std::atomic<bool> m_stopRequested = false;
    std::atomic<bool> m_playing = false;

    void joinWorker();
    void runPlayback(QVector<ScheduledEvent> schedule, InputSenderBackend *backend);
    bool dispatchEvent(const MacroEvent &event, InputSenderBackend *backend);
    bool waitForAnchorSettle();
    void releaseHeldState(InputSenderBackend *backend, const QString &reason, bool completed, bool stoppedByUser);
};

}
