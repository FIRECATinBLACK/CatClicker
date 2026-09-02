#pragma once

#include "Macro.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>

namespace CatClicker {

class MacroRecorder : public QObject {
    Q_OBJECT

public:
    explicit MacroRecorder(QObject *parent = nullptr);

    void begin(const MacroDisplayInfo &display, const QString &name = QStringLiteral("Untitled Macro"));
    void appendEvent(const MacroEvent &event);
    Macro finish();
    bool isRecording() const;
    qint64 elapsedUs() const;
    int eventCount() const;

signals:
    void eventCountChanged();

private:
    QElapsedTimer m_timer;
    Macro m_macro;
    bool m_recording = false;
};

}
