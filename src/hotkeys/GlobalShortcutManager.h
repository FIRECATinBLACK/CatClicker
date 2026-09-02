#pragma once

#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QMetaType>

namespace CatClicker {

class GlobalShortcutManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString recordShortcut READ recordShortcut WRITE setRecordShortcut NOTIFY shortcutsChanged)
    Q_PROPERTY(QString playShortcut READ playShortcut WRITE setPlayShortcut NOTIFY shortcutsChanged)
    Q_PROPERTY(QString stopShortcut READ stopShortcut WRITE setStopShortcut NOTIFY shortcutsChanged)

public:
    enum class ShortcutAction {
        None,
        Record,
        Play,
        Stop
    };
    Q_ENUM(ShortcutAction)

    struct ShortcutBinding {
        QString text;
        uint32_t triggerKeyCode = 0;
        bool requireCtrl = false;
        bool requireShift = false;
        bool requireAlt = false;
        bool requireMeta = false;

        bool isValid() const;
        QSet<uint32_t> relevantKeyCodes() const;
        bool matchesPress(uint32_t keyCode, const QSet<uint32_t> &pressedKeys) const;
        QSet<uint32_t> pressedChordKeys(const QSet<uint32_t> &pressedKeys) const;
    };

    explicit GlobalShortcutManager(QObject *parent = nullptr);

    QString recordShortcut() const;
    QString playShortcut() const;
    QString stopShortcut() const;
    QStringList stopShortcutSequences() const;

    void setRecordShortcut(const QString &value);
    void setPlayShortcut(const QString &value);
    void setStopShortcut(const QString &value);

    bool hasConflicts(QString *message) const;
    bool portalAvailable() const;
    ShortcutBinding recordBinding() const;
    ShortcutBinding playBinding() const;
    ShortcutBinding stopBinding() const;
    QSet<uint32_t> relevantLinuxKeyCodes() const;
    static bool isModifierKeyCode(uint32_t keyCode);

signals:
    void shortcutsChanged();

private:
    static QString normalizeShortcut(const QString &value);
    static ShortcutBinding parseBinding(const QString &value);

    QString m_recordShortcut = QStringLiteral("F8");
    QString m_playShortcut = QStringLiteral("F9");
    QString m_stopShortcut = QStringLiteral("Ctrl+Shift+F12");
};

}

Q_DECLARE_METATYPE(CatClicker::GlobalShortcutManager::ShortcutAction)
