#include "GlobalShortcutManager.h"

#include <QtGui/QKeySequence>

#include <linux/input-event-codes.h>

namespace CatClicker {

namespace {

bool isModifierOnly(Qt::Key key)
{
    switch (key) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
        return true;
    default:
        return false;
    }
}

uint32_t mapQtKeyToLinux(Qt::Key key)
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
    default:
        return 0;
    }
}

QSet<uint32_t> pressedModifierAlternatives(const QSet<uint32_t> &pressedKeys, uint32_t leftCode, uint32_t rightCode)
{
    QSet<uint32_t> result;
    if (pressedKeys.contains(leftCode)) {
        result.insert(leftCode);
    }
    if (pressedKeys.contains(rightCode)) {
        result.insert(rightCode);
    }
    return result;
}

}

GlobalShortcutManager::GlobalShortcutManager(QObject *parent)
    : QObject(parent)
{
}

QString GlobalShortcutManager::recordShortcut() const
{
    return m_recordShortcut;
}

QString GlobalShortcutManager::playShortcut() const
{
    return m_playShortcut;
}

QString GlobalShortcutManager::stopShortcut() const
{
    return m_stopShortcut;
}

QStringList GlobalShortcutManager::stopShortcutSequences() const
{
    const QKeySequence baseSequence(m_stopShortcut, QKeySequence::PortableText);
    if (baseSequence.count() != 1) {
        return {m_stopShortcut};
    }

    const QKeyCombination baseCombination = baseSequence[0];
    const Qt::Key key = baseCombination.key();
    if (key == Qt::Key_unknown || isModifierOnly(key)) {
        return {m_stopShortcut};
    }

    const Qt::KeyboardModifier availableModifiers[] = {
        Qt::ControlModifier,
        Qt::AltModifier,
        Qt::ShiftModifier,
        Qt::MetaModifier,
    };
    const Qt::KeyboardModifiers baseModifiers = baseCombination.keyboardModifiers();
    QStringList sequences;
    for (int mask = 0; mask < 16; ++mask) {
        Qt::KeyboardModifiers modifiers;
        for (int bit = 0; bit < 4; ++bit) {
            if (mask & (1 << bit)) {
                modifiers |= availableModifiers[bit];
            }
        }

        if ((modifiers & baseModifiers) != baseModifiers) {
            continue;
        }

        const QString sequence = QKeySequence(QKeyCombination(modifiers, key)).toString(QKeySequence::PortableText);
        if (!sequence.isEmpty() && !sequences.contains(sequence)) {
            if (modifiers == baseModifiers) {
                sequences.prepend(sequence);
            } else {
                sequences.append(sequence);
            }
        }
    }

    return sequences.isEmpty() ? QStringList{m_stopShortcut} : sequences;
}

void GlobalShortcutManager::setRecordShortcut(const QString &value)
{
    const QString normalized = normalizeShortcut(value);
    if (normalized.isEmpty() || m_recordShortcut == normalized) {
        return;
    }

    m_recordShortcut = normalized;
    emit shortcutsChanged();
}

void GlobalShortcutManager::setPlayShortcut(const QString &value)
{
    const QString normalized = normalizeShortcut(value);
    if (normalized.isEmpty() || m_playShortcut == normalized) {
        return;
    }

    m_playShortcut = normalized;
    emit shortcutsChanged();
}

void GlobalShortcutManager::setStopShortcut(const QString &value)
{
    const QString normalized = normalizeShortcut(value);
    if (normalized.isEmpty() || m_stopShortcut == normalized) {
        return;
    }

    m_stopShortcut = normalized;
    emit shortcutsChanged();
}

bool GlobalShortcutManager::hasConflicts(QString *message) const
{
    if (m_recordShortcut == m_playShortcut || m_recordShortcut == m_stopShortcut || m_playShortcut == m_stopShortcut) {
        if (message) {
            *message = QStringLiteral("Record, Play, and Stop shortcuts must be distinct.");
        }
        return true;
    }

    return false;
}

bool GlobalShortcutManager::portalAvailable() const
{
    return true;
}

GlobalShortcutManager::ShortcutBinding GlobalShortcutManager::recordBinding() const
{
    return parseBinding(m_recordShortcut);
}

GlobalShortcutManager::ShortcutBinding GlobalShortcutManager::playBinding() const
{
    return parseBinding(m_playShortcut);
}

GlobalShortcutManager::ShortcutBinding GlobalShortcutManager::stopBinding() const
{
    return parseBinding(m_stopShortcut);
}

QSet<uint32_t> GlobalShortcutManager::relevantLinuxKeyCodes() const
{
    QSet<uint32_t> result = recordBinding().relevantKeyCodes();
    result.unite(playBinding().relevantKeyCodes());
    result.unite(stopBinding().relevantKeyCodes());
    return result;
}

bool GlobalShortcutManager::isModifierKeyCode(uint32_t keyCode)
{
    switch (keyCode) {
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
        return true;
    default:
        return false;
    }
}

QString GlobalShortcutManager::normalizeShortcut(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QKeySequence sequence(trimmed, QKeySequence::PortableText);
    if (sequence.count() != 1) {
        return {};
    }

    const QKeyCombination combination = sequence[0];
    if (combination.key() == Qt::Key_unknown || isModifierOnly(combination.key())) {
        return {};
    }

    return sequence.toString(QKeySequence::PortableText);
}

GlobalShortcutManager::ShortcutBinding GlobalShortcutManager::parseBinding(const QString &value)
{
    ShortcutBinding binding;
    binding.text = normalizeShortcut(value);
    if (binding.text.isEmpty()) {
        return binding;
    }

    const QKeySequence sequence(binding.text, QKeySequence::PortableText);
    if (sequence.count() != 1) {
        binding.text.clear();
        return binding;
    }

    const QKeyCombination combination = sequence[0];
    binding.triggerKeyCode = mapQtKeyToLinux(combination.key());
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    binding.requireCtrl = modifiers.testFlag(Qt::ControlModifier);
    binding.requireShift = modifiers.testFlag(Qt::ShiftModifier);
    binding.requireAlt = modifiers.testFlag(Qt::AltModifier);
    binding.requireMeta = modifiers.testFlag(Qt::MetaModifier);
    if (binding.triggerKeyCode == 0 || isModifierKeyCode(binding.triggerKeyCode)) {
        binding.text.clear();
    }
    return binding;
}

bool GlobalShortcutManager::ShortcutBinding::isValid() const
{
    return !text.isEmpty() && triggerKeyCode != 0;
}

QSet<uint32_t> GlobalShortcutManager::ShortcutBinding::relevantKeyCodes() const
{
    QSet<uint32_t> result;
    if (!isValid()) {
        return result;
    }

    result.insert(triggerKeyCode);
    if (requireCtrl) {
        result.insert(KEY_LEFTCTRL);
        result.insert(KEY_RIGHTCTRL);
    }
    if (requireShift) {
        result.insert(KEY_LEFTSHIFT);
        result.insert(KEY_RIGHTSHIFT);
    }
    if (requireAlt) {
        result.insert(KEY_LEFTALT);
        result.insert(KEY_RIGHTALT);
    }
    if (requireMeta) {
        result.insert(KEY_LEFTMETA);
        result.insert(KEY_RIGHTMETA);
    }
    return result;
}

bool GlobalShortcutManager::ShortcutBinding::matchesPress(uint32_t keyCode, const QSet<uint32_t> &pressedKeys) const
{
    if (!isValid() || keyCode != triggerKeyCode || GlobalShortcutManager::isModifierKeyCode(keyCode)) {
        return false;
    }

    if (requireCtrl && !pressedKeys.contains(KEY_LEFTCTRL) && !pressedKeys.contains(KEY_RIGHTCTRL)) {
        return false;
    }
    if (requireShift && !pressedKeys.contains(KEY_LEFTSHIFT) && !pressedKeys.contains(KEY_RIGHTSHIFT)) {
        return false;
    }
    if (requireAlt && !pressedKeys.contains(KEY_LEFTALT) && !pressedKeys.contains(KEY_RIGHTALT)) {
        return false;
    }
    if (requireMeta && !pressedKeys.contains(KEY_LEFTMETA) && !pressedKeys.contains(KEY_RIGHTMETA)) {
        return false;
    }

    if (!requireCtrl && (pressedKeys.contains(KEY_LEFTCTRL) || pressedKeys.contains(KEY_RIGHTCTRL))) {
        return false;
    }
    if (!requireShift && (pressedKeys.contains(KEY_LEFTSHIFT) || pressedKeys.contains(KEY_RIGHTSHIFT))) {
        return false;
    }
    if (!requireAlt && (pressedKeys.contains(KEY_LEFTALT) || pressedKeys.contains(KEY_RIGHTALT))) {
        return false;
    }
    if (!requireMeta && (pressedKeys.contains(KEY_LEFTMETA) || pressedKeys.contains(KEY_RIGHTMETA))) {
        return false;
    }

    for (uint32_t pressedKey : pressedKeys) {
        if (pressedKey == triggerKeyCode || GlobalShortcutManager::isModifierKeyCode(pressedKey)) {
            continue;
        }
        return false;
    }

    return true;
}

QSet<uint32_t> GlobalShortcutManager::ShortcutBinding::pressedChordKeys(const QSet<uint32_t> &pressedKeys) const
{
    QSet<uint32_t> result;
    if (!isValid()) {
        return result;
    }

    result.insert(triggerKeyCode);
    if (requireCtrl) {
        result.unite(pressedModifierAlternatives(pressedKeys, KEY_LEFTCTRL, KEY_RIGHTCTRL));
    }
    if (requireShift) {
        result.unite(pressedModifierAlternatives(pressedKeys, KEY_LEFTSHIFT, KEY_RIGHTSHIFT));
    }
    if (requireAlt) {
        result.unite(pressedModifierAlternatives(pressedKeys, KEY_LEFTALT, KEY_RIGHTALT));
    }
    if (requireMeta) {
        result.unite(pressedModifierAlternatives(pressedKeys, KEY_LEFTMETA, KEY_RIGHTMETA));
    }
    return result;
}

}
