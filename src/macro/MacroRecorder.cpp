#include "MacroRecorder.h"

namespace CatClicker {

MacroRecorder::MacroRecorder(QObject *parent)
    : QObject(parent)
{
}

void MacroRecorder::begin(const MacroDisplayInfo &display, const QString &name)
{
    m_macro.clear();
    m_macro.name = name;
    m_macro.display = display;
    m_timer.start();
    m_recording = true;
    emit eventCountChanged();
}

void MacroRecorder::appendEvent(const MacroEvent &event)
{
    if (!m_recording) {
        return;
    }

    m_macro.events.push_back(event);
    if (event.timeUs > m_macro.durationUs) {
        m_macro.durationUs = event.timeUs;
    }
    emit eventCountChanged();
}

Macro MacroRecorder::finish()
{
    m_recording = false;
    m_macro.sortChronologically();
    return m_macro;
}

bool MacroRecorder::isRecording() const
{
    return m_recording;
}

qint64 MacroRecorder::elapsedUs() const
{
    if (!m_recording) {
        return m_macro.durationUs;
    }

    return m_timer.nsecsElapsed() / 1000;
}

int MacroRecorder::eventCount() const
{
    return m_macro.events.size();
}

}
