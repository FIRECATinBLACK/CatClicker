#include "Macro.h"

#include <algorithm>

namespace CatClicker {

bool Macro::isEmpty() const
{
    return events.isEmpty();
}

void Macro::clear()
{
    name = QStringLiteral("Untitled Macro");
    durationUs = 0;
    display = {};
    events.clear();
    createdAtUtc = QDateTime::currentDateTimeUtc();
}

void Macro::sortChronologically()
{
    std::stable_sort(events.begin(), events.end(), [](const MacroEvent &left, const MacroEvent &right) {
        return left.timeUs < right.timeUs;
    });
}

QString Macro::summary() const
{
    return QStringLiteral("%1 events • %2 ms")
        .arg(events.size())
        .arg(durationUs / 1000);
}

bool Macro::isCompatibleWith(const MacroDisplayInfo &currentDisplay, QString *reason) const
{
    if (display.logicalWidth != currentDisplay.logicalWidth || display.logicalHeight != currentDisplay.logicalHeight) {
        if (reason) {
            *reason = QStringLiteral("This macro was recorded on a %1x%2 logical display, but the current target is %3x%4.")
                          .arg(display.logicalWidth)
                          .arg(display.logicalHeight)
                          .arg(currentDisplay.logicalWidth)
                          .arg(currentDisplay.logicalHeight);
        }
        return false;
    }

    return true;
}

}
