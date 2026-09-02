#pragma once

#include "MacroEvent.h"

#include <QtCore/QDateTime>
#include <QtCore/QObject>
#include <QtCore/QVector>

namespace CatClicker {

struct MacroDisplayInfo {
    QString displayId;
    int logicalWidth = 0;
    int logicalHeight = 0;
    int streamWidth = 0;
    int streamHeight = 0;
    double scale = 1.0;
    int offsetX = 0;
    int offsetY = 0;
};

class Macro {
public:
    QString name = QStringLiteral("Untitled Macro");
    qint64 durationUs = 0;
    MacroDisplayInfo display;
    QVector<MacroEvent> events;
    QDateTime createdAtUtc = QDateTime::currentDateTimeUtc();

    bool isEmpty() const;
    void clear();
    void sortChronologically();
    QString summary() const;
    bool isCompatibleWith(const MacroDisplayInfo &currentDisplay, QString *reason) const;
};

}
