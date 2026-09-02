#pragma once

#include <QtCore/QtGlobal>

namespace CatClicker {

class PlaybackLoopController {
public:
    void setLoopEnabled(bool enabled);
    bool loopEnabled() const;
    bool stopRequested() const;

    quint64 startSequence();
    void requestStop();
    void cancelSequence();
    bool shouldRestartAfterFinish(quint64 token, bool completed, bool stoppedByUser);

private:
    bool m_loopEnabled = false;
    bool m_sequenceActive = false;
    bool m_stopRequested = false;
    quint64 m_nextToken = 0;
    quint64 m_activeToken = 0;
};

}
