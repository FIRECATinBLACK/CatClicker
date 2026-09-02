#include "MacroSerializer.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <cmath>
#include <limits>

namespace CatClicker {

namespace {

constexpr double kQint64LowerInclusive = -9223372036854775808.0;
constexpr double kQint64UpperExclusive = 9223372036854775808.0;

bool readStringField(const QJsonObject &object, const QString &fieldName, QString *value, QString *error)
{
    const QJsonValue field = object.value(fieldName);
    if (!field.isString()) {
        if (error) {
            *error = QStringLiteral("Field '%1' is missing or is not a string.").arg(fieldName);
        }
        return false;
    }

    if (value) {
        *value = field.toString();
    }
    return true;
}

bool readBoolField(const QJsonObject &object, const QString &fieldName, bool *value, QString *error)
{
    const QJsonValue field = object.value(fieldName);
    if (!field.isBool()) {
        if (error) {
            *error = QStringLiteral("Field '%1' is missing or is not a boolean.").arg(fieldName);
        }
        return false;
    }

    if (value) {
        *value = field.toBool();
    }
    return true;
}

bool readNumberField(const QJsonObject &object, const QString &fieldName, double *value, QString *error)
{
    const QJsonValue field = object.value(fieldName);
    if (!field.isDouble()) {
        if (error) {
            *error = QStringLiteral("Field '%1' is missing or is not a number.").arg(fieldName);
        }
        return false;
    }

    if (value) {
        *value = field.toDouble();
    }
    return true;
}

bool readIntField(const QJsonObject &object, const QString &fieldName, int *value, QString *error)
{
    const QJsonValue field = object.value(fieldName);
    if (!field.isDouble()) {
        if (error) {
            *error = QStringLiteral("Field '%1' is missing or is not an integer number.").arg(fieldName);
        }
        return false;
    }

    const double number = field.toDouble();
    if (!std::isfinite(number)) {
        if (error) {
            *error = QStringLiteral("Field '%1' must be finite.").arg(fieldName);
        }
        return false;
    }

    if (std::floor(number) != number) {
        if (error) {
            *error = QStringLiteral("Field '%1' must be an integer without a fractional component.").arg(fieldName);
        }
        return false;
    }

    if (number < static_cast<double>(std::numeric_limits<int>::min())
        || number > static_cast<double>(std::numeric_limits<int>::max())) {
        if (error) {
            *error = QStringLiteral("Field '%1' is outside the supported int range.").arg(fieldName);
        }
        return false;
    }

    if (value) {
        *value = static_cast<int>(number);
    }
    return true;
}

bool readObjectField(const QJsonObject &object, const QString &fieldName, QJsonObject *value, QString *error)
{
    const QJsonValue field = object.value(fieldName);
    if (!field.isObject()) {
        if (error) {
            *error = QStringLiteral("Field '%1' is missing or is not an object.").arg(fieldName);
        }
        return false;
    }

    if (value) {
        *value = field.toObject();
    }
    return true;
}

bool readArrayField(const QJsonObject &object, const QString &fieldName, QJsonArray *value, QString *error)
{
    const QJsonValue field = object.value(fieldName);
    if (!field.isArray()) {
        if (error) {
            *error = QStringLiteral("Field '%1' is missing or is not an array.").arg(fieldName);
        }
        return false;
    }

    if (value) {
        *value = field.toArray();
    }
    return true;
}

QJsonObject eventToJson(const MacroEvent &event)
{
    QJsonObject object;
    object[QStringLiteral("type")] = event.typeName();
    object[QStringLiteral("time_us")] = QString::number(event.timeUs);

    switch (event.type) {
    case MacroEventType::Key:
        object[QStringLiteral("keycode")] = static_cast<int>(event.keyCode);
        object[QStringLiteral("pressed")] = event.pressed;
        break;
    case MacroEventType::MouseMove:
        object[QStringLiteral("x")] = event.x;
        object[QStringLiteral("y")] = event.y;
        break;
    case MacroEventType::MouseButton:
        object[QStringLiteral("button")] = event.button;
        object[QStringLiteral("pressed")] = event.pressed;
        object[QStringLiteral("cursor_x")] = event.anchorX;
        object[QStringLiteral("cursor_y")] = event.anchorY;
        object[QStringLiteral("has_cursor_anchor")] = event.hasCursorAnchor;
        break;
    case MacroEventType::Scroll:
        object[QStringLiteral("delta_x")] = event.deltaX;
        object[QStringLiteral("delta_y")] = event.deltaY;
        object[QStringLiteral("cursor_x")] = event.anchorX;
        object[QStringLiteral("cursor_y")] = event.anchorY;
        object[QStringLiteral("has_cursor_anchor")] = event.hasCursorAnchor;
        break;
    }

    return object;
}

bool parseIntegerField(const QJsonObject &object, const QString &fieldName, qint64 *value, QString *error)
{
    const QJsonValue field = object.value(fieldName);
    if (field.isString()) {
        bool ok = false;
        const qint64 parsed = field.toString().toLongLong(&ok);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("Field '%1' does not contain a valid integer.").arg(fieldName);
            }
            return false;
        }
        *value = parsed;
        return true;
    }

    if (field.isDouble()) {
        const double number = field.toDouble();
        if (!std::isfinite(number)) {
            if (error) {
                *error = QStringLiteral("Field '%1' must be finite.").arg(fieldName);
            }
            return false;
        }
        if (std::floor(number) != number) {
            if (error) {
                *error = QStringLiteral("Field '%1' must be an integer without a fractional component.").arg(fieldName);
            }
            return false;
        }
        if (number < kQint64LowerInclusive || number >= kQint64UpperExclusive) {
            if (error) {
                *error = QStringLiteral("Field '%1' is outside the supported 64-bit integer range.").arg(fieldName);
            }
            return false;
        }
        *value = static_cast<qint64>(number);
        return true;
    }

    if (error) {
        *error = QStringLiteral("Field '%1' is missing or is not an integer/number.").arg(fieldName);
    }
    return false;
}

bool eventFromJson(const QJsonObject &object, MacroEvent *event, QString *error)
{
    qint64 timeUs = 0;
    if (!parseIntegerField(object, QStringLiteral("time_us"), &timeUs, error)) {
        return false;
    }

    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("key")) {
        int keyCode = 0;
        bool pressed = false;
        if (!readIntField(object, QStringLiteral("keycode"), &keyCode, error)
            || !readBoolField(object, QStringLiteral("pressed"), &pressed, error)) {
            return false;
        }
        *event = MacroEvent::keyEvent(timeUs,
                                      static_cast<uint32_t>(keyCode),
                                      pressed);
        return true;
    }

    if (type == QStringLiteral("mouse_move")) {
        double x = 0.0;
        double y = 0.0;
        if (!readNumberField(object, QStringLiteral("x"), &x, error)
            || !readNumberField(object, QStringLiteral("y"), &y, error)) {
            return false;
        }
        *event = MacroEvent::mouseMove(timeUs,
                                       x,
                                       y);
        return true;
    }

    if (type == QStringLiteral("mouse_button")) {
        int button = 0;
        bool pressed = false;
        double cursorX = 0.0;
        double cursorY = 0.0;
        bool hasCursorAnchor = false;
        if (!readIntField(object, QStringLiteral("button"), &button, error)
            || !readBoolField(object, QStringLiteral("pressed"), &pressed, error)
            || !readNumberField(object, QStringLiteral("cursor_x"), &cursorX, error)
            || !readNumberField(object, QStringLiteral("cursor_y"), &cursorY, error)
            || !readBoolField(object, QStringLiteral("has_cursor_anchor"), &hasCursorAnchor, error)) {
            return false;
        }
        *event = MacroEvent::mouseButton(timeUs,
                                         button,
                                         pressed,
                                         cursorX,
                                         cursorY,
                                         hasCursorAnchor);
        return true;
    }

    if (type == QStringLiteral("scroll")) {
        double deltaX = 0.0;
        double deltaY = 0.0;
        double cursorX = 0.0;
        double cursorY = 0.0;
        bool hasCursorAnchor = false;
        if (!readNumberField(object, QStringLiteral("delta_x"), &deltaX, error)
            || !readNumberField(object, QStringLiteral("delta_y"), &deltaY, error)
            || !readNumberField(object, QStringLiteral("cursor_x"), &cursorX, error)
            || !readNumberField(object, QStringLiteral("cursor_y"), &cursorY, error)
            || !readBoolField(object, QStringLiteral("has_cursor_anchor"), &hasCursorAnchor, error)) {
            return false;
        }
        *event = MacroEvent::scroll(timeUs,
                                    deltaX,
                                    deltaY,
                                    cursorX,
                                    cursorY,
                                    hasCursorAnchor);
        return true;
    }

    *error = QStringLiteral("Unknown event type: %1").arg(type);
    return false;
}

}

QByteArray MacroSerializer::toJson(const Macro &macro)
{
    QJsonArray eventArray;
    for (const MacroEvent &event : macro.events) {
        eventArray.push_back(eventToJson(event));
    }

    QJsonObject displayObject{
        {QStringLiteral("display_id"), macro.display.displayId},
        {QStringLiteral("width"), macro.display.streamWidth},
        {QStringLiteral("height"), macro.display.streamHeight},
        {QStringLiteral("scale"), macro.display.scale},
        {QStringLiteral("logical_width"), macro.display.logicalWidth},
        {QStringLiteral("logical_height"), macro.display.logicalHeight},
        {QStringLiteral("offset_x"), macro.display.offsetX},
        {QStringLiteral("offset_y"), macro.display.offsetY},
    };

    QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("CatClicker Macro")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("name"), macro.name},
        {QStringLiteral("duration_us"), QString::number(macro.durationUs)},
        {QStringLiteral("created_at_utc"), macro.createdAtUtc.toString(Qt::ISODate)},
        {QStringLiteral("display"), displayObject},
        {QStringLiteral("events"), eventArray},
    };

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool MacroSerializer::fromJson(const QByteArray &data, Macro *macro, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    QString format;
    if (!readStringField(root, QStringLiteral("format"), &format, error)) {
        return false;
    }
    if (format != QStringLiteral("CatClicker Macro")) {
        if (error) {
            *error = QStringLiteral("Unsupported macro format.");
        }
        return false;
    }

    int version = 0;
    if (!readIntField(root, QStringLiteral("version"), &version, error)) {
        return false;
    }
    if (version != 1) {
        if (error) {
            *error = QStringLiteral("Unsupported macro version.");
        }
        return false;
    }

    Macro parsed;
    QString createdAtString;
    QJsonObject displayObject;
    QJsonArray events;
    if (!readStringField(root, QStringLiteral("name"), &parsed.name, error)
        || !readStringField(root, QStringLiteral("created_at_utc"), &createdAtString, error)
        || !parseIntegerField(root, QStringLiteral("duration_us"), &parsed.durationUs, error)
        || !readObjectField(root, QStringLiteral("display"), &displayObject, error)
        || !readArrayField(root, QStringLiteral("events"), &events, error)) {
        return false;
    }

    parsed.createdAtUtc = QDateTime::fromString(createdAtString, Qt::ISODate);
    if (!parsed.createdAtUtc.isValid()) {
        if (error) {
            *error = QStringLiteral("Field 'created_at_utc' is not a valid ISO-8601 timestamp.");
        }
        return false;
    }

    if (!readStringField(displayObject, QStringLiteral("display_id"), &parsed.display.displayId, error)
        || !readIntField(displayObject, QStringLiteral("width"), &parsed.display.streamWidth, error)
        || !readIntField(displayObject, QStringLiteral("height"), &parsed.display.streamHeight, error)
        || !readNumberField(displayObject, QStringLiteral("scale"), &parsed.display.scale, error)
        || !readIntField(displayObject, QStringLiteral("logical_width"), &parsed.display.logicalWidth, error)
        || !readIntField(displayObject, QStringLiteral("logical_height"), &parsed.display.logicalHeight, error)
        || !readIntField(displayObject, QStringLiteral("offset_x"), &parsed.display.offsetX, error)
        || !readIntField(displayObject, QStringLiteral("offset_y"), &parsed.display.offsetY, error)) {
        return false;
    }

    parsed.events.reserve(events.size());
    for (const QJsonValue &value : events) {
        MacroEvent event;
        QString eventError;
        if (!value.isObject() || !eventFromJson(value.toObject(), &event, &eventError)) {
            if (error) {
                *error = eventError.isEmpty() ? QStringLiteral("Event entry is malformed.") : eventError;
            }
            return false;
        }
        parsed.events.push_back(event);
    }

    parsed.sortChronologically();
    if (macro) {
        *macro = parsed;
    }
    return true;
}

bool MacroSerializer::saveToFile(const QString &path, const Macro &macro, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    if (file.write(toJson(macro)) < 0) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    return true;
}

bool MacroSerializer::loadFromFile(const QString &path, Macro *macro, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    return fromJson(file.readAll(), macro, error);
}

}
