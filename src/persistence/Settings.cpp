#include "Settings.h"

#include <QtCore/QDir>
#include <QtCore/QStandardPaths>

namespace CatClicker {

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("CatClicker"), QStringLiteral("CatClicker"))
{
}

bool Settings::darkMode() const
{
    return m_settings.value(QStringLiteral("appearance/darkMode"), true).toBool();
}

QString Settings::recordShortcut() const
{
    return m_settings.value(QStringLiteral("shortcuts/record"), QStringLiteral("F8")).toString();
}

QString Settings::playShortcut() const
{
    return m_settings.value(QStringLiteral("shortcuts/play"), QStringLiteral("F9")).toString();
}

QString Settings::stopShortcut() const
{
    return m_settings.value(QStringLiteral("shortcuts/stop"), QStringLiteral("Ctrl+Shift+F12")).toString();
}

double Settings::playbackSpeed() const
{
    return m_settings.value(QStringLiteral("playback/speed"), 1.0).toDouble();
}

QString Settings::macroDirectory() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir directory(base);
    directory.mkpath(QStringLiteral("macros"));
    return directory.filePath(QStringLiteral("macros"));
}

bool Settings::loopPlaybackEnabled() const
{
    return m_settings.value(QStringLiteral("playback/loopEnabled"), false).toBool();
}

bool Settings::smoothMousePlaybackEnabled() const
{
    return m_settings.value(QStringLiteral("playback/smoothMouseMovement"), false).toBool();
}

bool Settings::showDeveloperTools() const
{
    return m_settings.value(QStringLiteral("ui/showDeveloperTools"), false).toBool();
}

void Settings::setDarkMode(bool value)
{
    m_settings.setValue(QStringLiteral("appearance/darkMode"), value);
}

void Settings::setRecordShortcut(const QString &value)
{
    m_settings.setValue(QStringLiteral("shortcuts/record"), value);
}

void Settings::setPlayShortcut(const QString &value)
{
    m_settings.setValue(QStringLiteral("shortcuts/play"), value);
}

void Settings::setStopShortcut(const QString &value)
{
    m_settings.setValue(QStringLiteral("shortcuts/stop"), value);
}

void Settings::setPlaybackSpeed(double value)
{
    m_settings.setValue(QStringLiteral("playback/speed"), value);
}

void Settings::setLoopPlaybackEnabled(bool value)
{
    m_settings.setValue(QStringLiteral("playback/loopEnabled"), value);
}

void Settings::setSmoothMousePlaybackEnabled(bool value)
{
    m_settings.setValue(QStringLiteral("playback/smoothMouseMovement"), value);
}

void Settings::setShowDeveloperTools(bool value)
{
    m_settings.setValue(QStringLiteral("ui/showDeveloperTools"), value);
}

}
