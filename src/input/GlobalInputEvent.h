#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QString>

#include <cstdint>

namespace CatClicker {

enum class GlobalInputEventType {
    Key,
    MouseButton,
    Scroll,
    RelativeMotion
};

struct GlobalInputEvent {
    GlobalInputEventType type = GlobalInputEventType::Key;
    qint64 timeUs = 0;
    uint32_t code = 0;
    bool pressed = false;
    bool autoRepeat = false;
    double deltaX = 0.0;
    double deltaY = 0.0;
    QString devicePath;
};

}

Q_DECLARE_METATYPE(CatClicker::GlobalInputEvent)
