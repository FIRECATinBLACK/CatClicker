#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QString>

#include <cstdint>

namespace CatClicker {

enum class MacroEventType {
    Key,
    MouseMove,
    MouseButton,
    Scroll
};

struct MacroEvent {
    MacroEventType type = MacroEventType::MouseMove;
    qint64 timeUs = 0;

    uint32_t keyCode = 0;
    bool pressed = false;

    int button = 0;
    double x = 0.0;
    double y = 0.0;
    bool hasCursorAnchor = false;
    double anchorX = 0.0;
    double anchorY = 0.0;

    double deltaX = 0.0;
    double deltaY = 0.0;

    static MacroEvent keyEvent(qint64 timeUs, uint32_t keyCode, bool pressed);
    static MacroEvent mouseMove(qint64 timeUs, double x, double y);
    static MacroEvent mouseButton(qint64 timeUs, int button, bool pressed, double anchorX, double anchorY, bool hasCursorAnchor);
    static MacroEvent scroll(qint64 timeUs, double deltaX, double deltaY, double x, double y, bool hasCursorAnchor);

    QString typeName() const;
};

}

Q_DECLARE_METATYPE(CatClicker::MacroEvent)
