#pragma once

#include <QtCore/QObject>
#include <QtCore/QSettings>

namespace CatClicker {

class Settings : public QObject {
    Q_OBJECT

public:
    explicit Settings(QObject *parent = nullptr);

    bool darkMode() const;
    QString recordShortcut() const;
    QString playShortcut() const;
    QString stopShortcut() const;
    double playbackSpeed() const;
    QString macroDirectory() const;
    bool loopPlaybackEnabled() const;
    bool smoothMousePlaybackEnabled() const;
    bool showDeveloperTools() const;
    bool compactInterface() const;

    void setDarkMode(bool value);
    void setRecordShortcut(const QString &value);
    void setPlayShortcut(const QString &value);
    void setStopShortcut(const QString &value);
    void setPlaybackSpeed(double value);
    void setLoopPlaybackEnabled(bool value);
    void setSmoothMousePlaybackEnabled(bool value);
    void setShowDeveloperTools(bool value);
    void setCompactInterface(bool value);

private:
    mutable QSettings m_settings;
};

}
