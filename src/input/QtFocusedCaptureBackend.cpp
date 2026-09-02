#include "QtFocusedCaptureBackend.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>

#include <linux/input-event-codes.h>

namespace CatClicker {

namespace {

uint32_t modifierToLinux(Qt::KeyboardModifier modifier)
{
    switch (modifier) {
    case Qt::ShiftModifier:
        return KEY_LEFTSHIFT;
    case Qt::ControlModifier:
        return KEY_LEFTCTRL;
    case Qt::AltModifier:
        return KEY_LEFTALT;
    case Qt::MetaModifier:
        return KEY_LEFTMETA;
    default:
        return 0;
    }
}

}

QtFocusedCaptureBackend::QtFocusedCaptureBackend(QObject *parent)
    : InputCaptureBackend(parent)
{
}

bool QtFocusedCaptureBackend::isAvailable() const
{
    return QGuiApplication::instance() != nullptr;
}

QString QtFocusedCaptureBackend::unavailabilityReason() const
{
    if (isAvailable()) {
        return {};
    }

    return QStringLiteral("A Qt GUI application instance is required for focused recording.");
}

bool QtFocusedCaptureBackend::startCapture()
{
    if (!isAvailable()) {
        emit backendError(unavailabilityReason());
        return false;
    }

    if (m_capturing) {
        return true;
    }

    m_capturing = true;
    m_lastTimestampUs = 0;
    m_timer.start();
    m_heldKeys.clear();
    m_heldButtons.clear();
    QGuiApplication::instance()->installEventFilter(this);
    return true;
}

void QtFocusedCaptureBackend::stopCapture()
{
    if (!m_capturing) {
        return;
    }

    emitReleaseEventsForHeldState();
    if (QGuiApplication::instance()) {
        QGuiApplication::instance()->removeEventFilter(this);
    }
    m_capturing = false;
    m_suppressedKeysUntilRelease.clear();
    m_heldKeys.clear();
    m_heldButtons.clear();
}

void QtFocusedCaptureBackend::suppressShortcutUntilRelease(const QString &shortcut)
{
    m_suppressedKeysUntilRelease.unite(shortcutKeyCodes(shortcut));
}

bool QtFocusedCaptureBackend::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)

    if (!m_capturing || !event) {
        return false;
    }

    switch (event->type()) {
    case QEvent::KeyPress:
        return handleKeyEvent(static_cast<QKeyEvent *>(event), true);
    case QEvent::KeyRelease:
        return handleKeyEvent(static_cast<QKeyEvent *>(event), false);
    case QEvent::MouseMove:
        return handleMouseEvent(static_cast<QMouseEvent *>(event), true);
    case QEvent::MouseButtonPress:
        return handleMouseEvent(static_cast<QMouseEvent *>(event), true);
    case QEvent::MouseButtonRelease:
        return handleMouseEvent(static_cast<QMouseEvent *>(event), false);
    case QEvent::Wheel:
        return handleWheelEvent(static_cast<QWheelEvent *>(event));
    default:
        return false;
    }
}

qint64 QtFocusedCaptureBackend::nextTimestampUs()
{
    const qint64 current = m_timer.nsecsElapsed() / 1000;
    if (current < m_lastTimestampUs) {
        return m_lastTimestampUs;
    }

    m_lastTimestampUs = current;
    return current;
}

bool QtFocusedCaptureBackend::handleKeyEvent(QKeyEvent *event, bool pressed)
{
    if (!event || event->isAutoRepeat()) {
        return false;
    }

    const uint32_t keyCode = mapQtKeyToLinux(event->key());
    if (keyCode == 0) {
        return false;
    }

    if (m_suppressedKeysUntilRelease.contains(keyCode)) {
        if (!pressed) {
            m_suppressedKeysUntilRelease.remove(keyCode);
        }
        return false;
    }

    if (pressed) {
        m_heldKeys.insert(keyCode);
    } else {
        m_heldKeys.remove(keyCode);
    }

    emit eventCaptured(MacroEvent::keyEvent(nextTimestampUs(), keyCode, pressed));
    return false;
}

bool QtFocusedCaptureBackend::handleMouseEvent(QMouseEvent *event, bool pressed)
{
    if (!event) {
        return false;
    }

    if (event->type() == QEvent::MouseMove) {
        const QPointF global = event->globalPosition();
        emit eventCaptured(MacroEvent::mouseMove(nextTimestampUs(), global.x(), global.y()));
        return false;
    }

    const int button = mapQtButtonToLinux(event->button());
    if (button == 0) {
        return false;
    }

    const QPointF global = event->globalPosition();
    if (pressed) {
        m_heldButtons.insert(button);
    } else {
        m_heldButtons.remove(button);
    }

    emit eventCaptured(MacroEvent::mouseButton(nextTimestampUs(), button, pressed, global.x(), global.y(), true));
    return false;
}

bool QtFocusedCaptureBackend::handleWheelEvent(QWheelEvent *event)
{
    if (!event) {
        return false;
    }

    const QPoint angle = event->angleDelta();
    if (angle.isNull()) {
        return false;
    }

    const QPointF global = event->globalPosition();
    emit eventCaptured(MacroEvent::scroll(nextTimestampUs(),
                                          angle.x() / 120.0,
                                          angle.y() / 120.0,
                                          global.x(),
                                          global.y(),
                                          true));
    return false;
}

void QtFocusedCaptureBackend::emitReleaseEventsForHeldState()
{
    const qint64 timestampUs = nextTimestampUs();

    const auto heldKeys = m_heldKeys.values();
    for (uint32_t keyCode : heldKeys) {
        emit eventCaptured(MacroEvent::keyEvent(timestampUs, keyCode, false));
    }
}

uint32_t QtFocusedCaptureBackend::mapQtKeyToLinux(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        static constexpr uint32_t linuxLetterKeyCodes[] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
            KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N,
            KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U,
            KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
        };
        return linuxLetterKeyCodes[key - Qt::Key_A];
    }

    if (key >= Qt::Key_1 && key <= Qt::Key_9) {
        return KEY_1 + static_cast<uint32_t>(key - Qt::Key_1);
    }

    if (key == Qt::Key_0) {
        return KEY_0;
    }

    if (key >= Qt::Key_F1 && key <= Qt::Key_F10) {
        return KEY_F1 + static_cast<uint32_t>(key - Qt::Key_F1);
    }

    switch (key) {
    case Qt::Key_F11:
        return KEY_F11;
    case Qt::Key_F12:
        return KEY_F12;
    case Qt::Key_Control:
        return KEY_LEFTCTRL;
    case Qt::Key_Shift:
        return KEY_LEFTSHIFT;
    case Qt::Key_Alt:
        return KEY_LEFTALT;
    case Qt::Key_Meta:
        return KEY_LEFTMETA;
    case Qt::Key_Space:
        return KEY_SPACE;
    case Qt::Key_Tab:
        return KEY_TAB;
    case Qt::Key_Escape:
        return KEY_ESC;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return KEY_ENTER;
    case Qt::Key_Backspace:
        return KEY_BACKSPACE;
    case Qt::Key_Delete:
        return KEY_DELETE;
    case Qt::Key_Insert:
        return KEY_INSERT;
    case Qt::Key_Home:
        return KEY_HOME;
    case Qt::Key_End:
        return KEY_END;
    case Qt::Key_PageUp:
        return KEY_PAGEUP;
    case Qt::Key_PageDown:
        return KEY_PAGEDOWN;
    case Qt::Key_Left:
        return KEY_LEFT;
    case Qt::Key_Right:
        return KEY_RIGHT;
    case Qt::Key_Up:
        return KEY_UP;
    case Qt::Key_Down:
        return KEY_DOWN;
    case Qt::Key_Minus:
        return KEY_MINUS;
    case Qt::Key_Equal:
        return KEY_EQUAL;
    case Qt::Key_BracketLeft:
        return KEY_LEFTBRACE;
    case Qt::Key_BracketRight:
        return KEY_RIGHTBRACE;
    case Qt::Key_Backslash:
        return KEY_BACKSLASH;
    case Qt::Key_Semicolon:
        return KEY_SEMICOLON;
    case Qt::Key_Apostrophe:
        return KEY_APOSTROPHE;
    case Qt::Key_Comma:
        return KEY_COMMA;
    case Qt::Key_Period:
        return KEY_DOT;
    case Qt::Key_Slash:
        return KEY_SLASH;
    case Qt::Key_QuoteLeft:
        return KEY_GRAVE;
    default:
        return 0;
    }
}

int QtFocusedCaptureBackend::mapQtButtonToLinux(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return BTN_LEFT;
    case Qt::RightButton:
        return BTN_RIGHT;
    case Qt::MiddleButton:
        return BTN_MIDDLE;
    case Qt::BackButton:
        return BTN_SIDE;
    case Qt::ForwardButton:
        return BTN_EXTRA;
    default:
        return 0;
    }
}

QSet<uint32_t> QtFocusedCaptureBackend::shortcutKeyCodes(const QString &shortcut)
{
    QSet<uint32_t> codes;
    const QKeySequence sequence(shortcut, QKeySequence::PortableText);
    if (sequence.count() != 1) {
        return codes;
    }

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        codes.insert(modifierToLinux(Qt::ShiftModifier));
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        codes.insert(modifierToLinux(Qt::ControlModifier));
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        codes.insert(modifierToLinux(Qt::AltModifier));
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        codes.insert(modifierToLinux(Qt::MetaModifier));
    }

    const uint32_t keyCode = mapQtKeyToLinux(combination.key());
    if (keyCode != 0) {
        codes.insert(keyCode);
    }

    return codes;
}

}
