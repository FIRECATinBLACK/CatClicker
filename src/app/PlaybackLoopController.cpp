#include "PlaybackLoopController.h"

#include <QtCore/QDebug>

namespace CatClicker {

namespace {

bool traceLoopEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("CATCLICKER_TRACE_LOOP");
    return enabled;
}

}

void PlaybackLoopController::setLoopEnabled(bool enabled)
{
    if (traceLoopEnabled()) {
        qInfo().noquote() << QStringLiteral("[loop] coordinator enabled old=%1 new=%2")
                                 .arg(m_loopEnabled ? 1 : 0)
                                 .arg(enabled ? 1 : 0);
    }
    m_loopEnabled = enabled;
}

bool PlaybackLoopController::loopEnabled() const
{
    return m_loopEnabled;
}

bool PlaybackLoopController::stopRequested() const
{
    return m_stopRequested;
}

quint64 PlaybackLoopController::startSequence()
{
    m_sequenceActive = true;
    m_stopRequested = false;
    m_activeToken = ++m_nextToken;
    if (traceLoopEnabled()) {
        qInfo().noquote() << QStringLiteral("[loop] start sequence token=%1 loopEnabled=%2 stopRequested=%3")
                                 .arg(m_activeToken)
                                 .arg(m_loopEnabled ? 1 : 0)
                                 .arg(m_stopRequested ? 1 : 0);
    }
    return m_activeToken;
}

void PlaybackLoopController::requestStop()
{
    if (!m_sequenceActive) {
        return;
    }

    m_stopRequested = true;
    if (traceLoopEnabled()) {
        qInfo().noquote() << QStringLiteral("[loop] request stop token=%1 loopEnabled=%2")
                                 .arg(m_activeToken)
                                 .arg(m_loopEnabled ? 1 : 0);
    }
}

void PlaybackLoopController::cancelSequence()
{
    if (traceLoopEnabled()) {
        qInfo().noquote() << QStringLiteral("[loop] cancel sequence token=%1 stopRequested=%2 loopEnabled=%3")
                                 .arg(m_activeToken)
                                 .arg(m_stopRequested ? 1 : 0)
                                 .arg(m_loopEnabled ? 1 : 0);
    }
    m_sequenceActive = false;
    m_stopRequested = false;
    m_activeToken = 0;
}

bool PlaybackLoopController::shouldRestartAfterFinish(quint64 token, bool completed, bool stoppedByUser)
{
    if (!m_sequenceActive || token != m_activeToken) {
        return false;
    }

    const bool shouldRestart = completed && !stoppedByUser && !m_stopRequested && m_loopEnabled;
    if (!shouldRestart) {
        cancelSequence();
    }

    return shouldRestart;
}

}
