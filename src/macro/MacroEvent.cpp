#include "MacroEvent.h"

namespace CatClicker {

MacroEvent MacroEvent::keyEvent(qint64 eventTimeUs, uint32_t eventKeyCode, bool eventPressed)
{
    MacroEvent event;
    event.type = MacroEventType::Key;
    event.timeUs = eventTimeUs;
    event.keyCode = eventKeyCode;
    event.pressed = eventPressed;
    return event;
}

MacroEvent MacroEvent::mouseMove(qint64 eventTimeUs, double eventX, double eventY)
{
    MacroEvent event;
    event.type = MacroEventType::MouseMove;
    event.timeUs = eventTimeUs;
    event.x = eventX;
    event.y = eventY;
    return event;
}

MacroEvent MacroEvent::mouseButton(qint64 eventTimeUs, int eventButton, bool eventPressed, double eventAnchorX, double eventAnchorY, bool eventHasCursorAnchor)
{
    MacroEvent event;
    event.type = MacroEventType::MouseButton;
    event.timeUs = eventTimeUs;
    event.button = eventButton;
    event.pressed = eventPressed;
    event.hasCursorAnchor = eventHasCursorAnchor;
    event.anchorX = eventAnchorX;
    event.anchorY = eventAnchorY;
    return event;
}

MacroEvent MacroEvent::scroll(qint64 eventTimeUs, double eventDeltaX, double eventDeltaY, double eventX, double eventY, bool eventHasCursorAnchor)
{
    MacroEvent event;
    event.type = MacroEventType::Scroll;
    event.timeUs = eventTimeUs;
    event.deltaX = eventDeltaX;
    event.deltaY = eventDeltaY;
    event.hasCursorAnchor = eventHasCursorAnchor;
    event.anchorX = eventX;
    event.anchorY = eventY;
    return event;
}

QString MacroEvent::typeName() const
{
    switch (type) {
    case MacroEventType::Key:
        return QStringLiteral("key");
    case MacroEventType::MouseMove:
        return QStringLiteral("mouse_move");
    case MacroEventType::MouseButton:
        return QStringLiteral("mouse_button");
    case MacroEventType::Scroll:
        return QStringLiteral("scroll");
    }

    return QStringLiteral("unknown");
}

}
