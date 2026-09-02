#include "ApplicationController.h"

#include "../input/PlaybackBackendSelector.h"
#include "../persistence/MacroSerializer.h"

#include <algorithm>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeySequence>

#include <linux/input-event-codes.h>

namespace CatClicker {

namespace {

QString formatDuration(qint64 durationUs)
{
    const qint64 totalMs = durationUs / 1000;
    const qint64 minutes = totalMs / 60000;
    const qint64 seconds = (totalMs / 1000) % 60;
    const qint64 millis = totalMs % 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString fileNameForMacro(const Macro &macro)
{
    QString base = macro.name.trimmed();
    if (base.isEmpty()) {
        base = QStringLiteral("Untitled Macro");
    }
    return base.replace(QLatin1Char(' '), QLatin1Char('_'));
}

QString makeRecordingName()
{
    return QStringLiteral("Recording %1")
        .arg(QLocale::system().toString(QDateTime::currentDateTime(), QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

QString quotedShellArgument(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(escaped);
}

bool traceLoopEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("CATCLICKER_TRACE_LOOP");
    return enabled;
}

uint32_t mapShortcutKeyToLinux(Qt::Key key)
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
    default:
        return 0;
    }
}

QSet<uint32_t> shortcutKeyCodesForString(const QString &shortcut)
{
    QSet<uint32_t> codes;
    const QKeySequence sequence(shortcut, QKeySequence::PortableText);
    if (sequence.count() != 1) {
        return codes;
    }

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    if (modifiers.testFlag(Qt::ControlModifier)) {
        codes.insert(KEY_LEFTCTRL);
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        codes.insert(KEY_LEFTSHIFT);
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        codes.insert(KEY_LEFTALT);
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        codes.insert(KEY_LEFTMETA);
    }

    const uint32_t keyCode = mapShortcutKeyToLinux(combination.key());
    if (keyCode != 0) {
        codes.insert(keyCode);
    }

    return codes;
}

Macro buildPointerTestMacro(const MacroDisplayInfo &display)
{
    Macro macro;
    macro.name = QStringLiteral("Pointer Test");
    macro.display = display;

    const double left = std::max(40.0, display.logicalWidth * 0.30);
    const double top = std::max(40.0, display.logicalHeight * 0.30);
    const double right = std::max(left + 40.0, display.logicalWidth * 0.70);
    const double bottom = std::max(top + 40.0, display.logicalHeight * 0.70);
    const double centerX = display.logicalWidth / 2.0;
    const double centerY = display.logicalHeight / 2.0;

    macro.events = {
        MacroEvent::mouseMove(0, left, top),
        MacroEvent::mouseMove(500000, right, top),
        MacroEvent::mouseMove(1000000, right, bottom),
        MacroEvent::mouseMove(1500000, centerX, centerY),
    };
    macro.durationUs = 1500000;
    return macro;
}

Macro buildKeyboardTestMacro(const MacroDisplayInfo &display)
{
    Macro macro;
    macro.name = QStringLiteral("Keyboard Test");
    macro.display = display;
    macro.events = {
        MacroEvent::keyEvent(2000000, KEY_C, true),
        MacroEvent::keyEvent(2080000, KEY_C, false),
        MacroEvent::keyEvent(2250000, KEY_A, true),
        MacroEvent::keyEvent(2330000, KEY_A, false),
        MacroEvent::keyEvent(2500000, KEY_T, true),
        MacroEvent::keyEvent(2580000, KEY_T, false),
    };
    macro.durationUs = 2580000;
    return macro;
}

Macro buildClickTestMacro(const MacroDisplayInfo &display, double x, double y)
{
    Macro macro;
    macro.name = QStringLiteral("Click Test");
    macro.display = display;
    macro.events = {
        MacroEvent::mouseMove(0, x, y),
        MacroEvent::mouseButton(500000, BTN_LEFT, true, x, y, true),
        MacroEvent::mouseButton(580000, BTN_LEFT, false, x, y, true),
    };
    macro.durationUs = 580000;
    return macro;
}

Macro buildScrollTestMacro(const MacroDisplayInfo &display, double x, double y)
{
    Macro macro;
    macro.name = QStringLiteral("Scroll Test");
    macro.display = display;
    macro.events = {
        MacroEvent::scroll(0, 0.0, -1.0, x, y, true),
        MacroEvent::scroll(300000, 0.0, -1.0, x, y, true),
        MacroEvent::scroll(600000, 0.0, 1.0, x, y, true),
    };
    macro.durationUs = 600000;
    return macro;
}

Macro buildHeldKeyStopTestMacro(const MacroDisplayInfo &display)
{
    Macro macro;
    macro.name = QStringLiteral("Held Key Stop Test");
    macro.display = display;
    macro.events = {
        MacroEvent::keyEvent(0, KEY_LEFTSHIFT, true),
        MacroEvent::keyEvent(5000000, KEY_LEFTSHIFT, false),
    };
    macro.durationUs = 5000000;
    return macro;
}

Macro buildHeldMouseStopTestMacro(const MacroDisplayInfo &display, double x, double y)
{
    Macro macro;
    macro.name = QStringLiteral("Held Mouse Stop Test");
    macro.display = display;
    macro.events = {
        MacroEvent::mouseButton(0, BTN_LEFT, true, x, y, true),
        MacroEvent::mouseButton(5000000, BTN_LEFT, false, x, y, true),
    };
    macro.durationUs = 5000000;
    return macro;
}

Macro buildHeldMouseCompletionTestMacro(const MacroDisplayInfo &display, double x, double y)
{
    Macro macro;
    macro.name = QStringLiteral("Held Mouse Completion Test");
    macro.display = display;
    macro.events = {
        MacroEvent::mouseButton(0, BTN_LEFT, true, x, y, true),
        MacroEvent::mouseButton(2000000, BTN_LEFT, false, x, y, true),
    };
    macro.durationUs = 2000000;
    return macro;
}

}

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_useDirectCosmicCursorProvider(shouldUseDirectCosmicCursorProvider(
          qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
          qEnvironmentVariable("XDG_SESSION_TYPE"),
          qEnvironmentVariable("WAYLAND_DISPLAY"),
          CosmicCursorPositionProvider::buildSupported(),
          qEnvironmentVariableIntValue("CATCLICKER_DISABLE_COSMIC_CURSOR") == 1))
    , m_captureBackend(&m_globalInputMonitor,
                       m_useDirectCosmicCursorProvider
                           ? static_cast<const CursorPositionProvider *>(&m_cosmicCursorProvider)
                           : static_cast<const CursorPositionProvider *>(&m_cursorTracker))
{
    m_eiInputSender.setPortalCapabilities(m_portalController.capabilities());
    if (m_useDirectCosmicCursorProvider) {
        m_cosmicCursorProvider.start(m_portalController.currentDisplayInfo(), QGuiApplication::screens().size());
    } else {
        m_cursorTracker.startTracking(m_portalController.capabilities());
    }
    loadSettings();
    updateGlobalShortcutBindings();
    m_globalInputMonitor.startMonitoring();
    refreshDiagnostics();

    connect(&m_fileChooserPortal, &FileChooserPortal::saveAccepted, this, &ApplicationController::saveMacroToUrl);
    connect(&m_fileChooserPortal, &FileChooserPortal::openAccepted, this, &ApplicationController::loadMacroFromUrl);
    connect(&m_fileChooserPortal, &FileChooserPortal::saveCancelled, this, [this]() {
        setState(AppState::Idle, QStringLiteral("Save cancelled."));
    });
    connect(&m_fileChooserPortal, &FileChooserPortal::openCancelled, this, [this]() {
        setState(AppState::Idle, QStringLiteral("Load cancelled."));
    });
    connect(&m_fileChooserPortal, &FileChooserPortal::failed, this, [this](const QString &message) {
        setState(AppState::Error, message);
    });

    m_elapsedTimer.setInterval(100);
    connect(&m_elapsedTimer, &QTimer::timeout, this, &ApplicationController::timingChanged);
    connect(&m_macroRecorder, &MacroRecorder::eventCountChanged, this, &ApplicationController::macroChanged);
    connectCaptureBackend(&m_qtFocusedCaptureBackend);
    connectCaptureBackend(&m_captureBackend);
    connect(&m_cursorTracker, &CursorTracker::trackingChanged, this, [this]() {
        refreshDiagnostics();
    });
    connect(&m_cursorTracker, &CursorTracker::cursorPositionChanged, this, [this]() {
        refreshDiagnostics();
    });
    connect(&m_cosmicCursorProvider, &CosmicCursorPositionProvider::trackingChanged, this, [this]() {
        refreshDiagnostics();
    });
    connect(&m_cosmicCursorProvider, &CosmicCursorPositionProvider::cursorPositionChanged, this, [this]() {
        refreshDiagnostics();
    });
    connect(&m_globalInputMonitor, &GlobalInputMonitor::availabilityChanged, this, [this]() {
        refreshDiagnostics();
        evaluateStartupPermissions(false);
    });
    connect(&m_globalInputMonitor, &GlobalInputMonitor::globalShortcutTriggered, this,
            [this](GlobalShortcutManager::ShortcutAction action) {
        switch (action) {
        case GlobalShortcutManager::ShortcutAction::Record:
            startRecording(true);
            break;
        case GlobalShortcutManager::ShortcutAction::Play:
            startPlayback();
            break;
        case GlobalShortcutManager::ShortcutAction::Stop:
            stop();
            break;
        case GlobalShortcutManager::ShortcutAction::None:
            break;
        }
    });
    connect(&m_macroPlayer, &MacroPlayer::playbackStarted, this, [this]() {
        m_playbackElapsedUs = 0;
        m_elapsedTimer.start();
        setState(AppState::Playing, QStringLiteral("Playing macro through %1.").arg(m_selectedPlaybackBackend));
        emit diagnosticsChanged();
    });
    connect(&m_macroPlayer, &MacroPlayer::playbackProgress, this, [this](qint64 elapsedUs) {
        m_playbackElapsedUs = elapsedUs;
        emit timingChanged();
        emit diagnosticsChanged();
    });
    connect(&m_macroPlayer, &MacroPlayer::playbackFinished, this, [this](bool completed, bool stoppedByUser, const QString &message) {
        const quint64 finishedToken = m_activePlaybackLoopToken;
        m_selectedInputSender = nullptr;
        m_playbackElapsedUs = 0;
        m_elapsedTimer.stop();
        const bool shouldRestart = m_playbackLoopController.shouldRestartAfterFinish(finishedToken, completed, stoppedByUser);
        if (traceLoopEnabled()) {
            qInfo().noquote() << QStringLiteral("[loop] finished token=%1 completed=%2 stoppedByUser=%3 appState=%4 preference=%5 helperEnabled=%6 stopRequested=%7 shouldRestart=%8")
                                     .arg(finishedToken)
                                     .arg(completed ? 1 : 0)
                                     .arg(stoppedByUser ? 1 : 0)
                                     .arg(appStateString())
                                     .arg(m_loopPlaybackEnabled ? 1 : 0)
                                     .arg(m_playbackLoopController.loopEnabled() ? 1 : 0)
                                     .arg(m_playbackLoopController.stopRequested() ? 1 : 0)
                                     .arg(shouldRestart ? 1 : 0);
        }
        if (shouldRestart) {
            if (!startPlaybackIteration(finishedToken)) {
                m_activePlaybackLoopToken = 0;
            }
            emit timingChanged();
            emit diagnosticsChanged();
            return;
        }

        m_activePlaybackLoopToken = 0;
        if (stoppedByUser || m_state == AppState::Stopping) {
            setState(AppState::Idle, message);
        } else if (completed) {
            setState(AppState::Idle, message);
        } else {
            setState(AppState::Error, message);
        }
        emit timingChanged();
        emit diagnosticsChanged();
    });

    evaluateStartupPermissions(true);
}

ApplicationController::~ApplicationController()
{
    m_elapsedTimer.stop();
    disconnect(&m_macroPlayer, nullptr, this, nullptr);
    disconnect(&m_globalInputMonitor, nullptr, this, nullptr);
    disconnect(&m_cosmicCursorProvider, nullptr, this, nullptr);
    disconnect(&m_cursorTracker, nullptr, this, nullptr);

    if (m_activeCaptureBackend) {
        m_activeCaptureBackend->stopCapture();
        m_activeCaptureBackend = nullptr;
    }
    m_macroPlayer.shutdown();
    m_selectedInputSender = nullptr;
    m_globalInputMonitor.stopMonitoring();
    m_cosmicCursorProvider.stop();
    m_cursorTracker.stopTracking();

    if (m_permissionSetupProcess) {
        m_permissionSetupProcess->disconnect(this);
        if (m_permissionSetupProcess->state() != QProcess::NotRunning) {
            m_permissionSetupProcess->terminate();
            if (!m_permissionSetupProcess->waitForFinished(1000)) {
                m_permissionSetupProcess->kill();
                m_permissionSetupProcess->waitForFinished(1000);
            }
        }
        delete m_permissionSetupProcess;
        m_permissionSetupProcess = nullptr;
    }
}

QString ApplicationController::appStateString() const
{
    switch (m_state) {
    case AppState::Idle:
        return QStringLiteral("Idle");
    case AppState::PreparingRecording:
        return QStringLiteral("Preparing Recording");
    case AppState::Recording:
        return QStringLiteral("Recording");
    case AppState::PreparingPlayback:
        return QStringLiteral("Preparing Playback");
    case AppState::Playing:
        return QStringLiteral("Playing");
    case AppState::Stopping:
        return QStringLiteral("Stopping");
    case AppState::Error:
        return QStringLiteral("Error");
    }

    return QStringLiteral("Unknown");
}

QString ApplicationController::statusText() const
{
    return m_statusText;
}

QString ApplicationController::diagnosticsText() const
{
    return m_diagnostics.generateReport(m_portalController.capabilities(),
                                        m_portalController.currentDisplayInfo(),
                                        m_cursorTracker.hasBuildSupport(),
                                        m_qtFocusedCaptureBackend.isAvailable(),
                                        QStringLiteral("Direct COSMIC provider: %1; ScreenCast metadata provider: %2")
                                            .arg(m_cosmicCursorProvider.statusText(), m_cursorTracker.statusText()),
                                        m_evdevInspector,
                                        m_globalInputMonitor,
                                        m_selectedPlaybackBackend,
                                        m_playbackBackendReason,
                                        activeDiagnosticsBackend());
}

int ApplicationController::virtualHeldKeyCount() const
{
    return activeDiagnosticsBackend()->virtualHeldKeyCount();
}

int ApplicationController::virtualHeldButtonCount() const
{
    return activeDiagnosticsBackend()->virtualHeldButtonCount();
}

bool ApplicationController::darkMode() const
{
    return m_darkMode;
}

QString ApplicationController::macroName() const
{
    return m_currentMacro.name;
}

int ApplicationController::eventCount() const
{
    return m_state == AppState::Recording ? m_macroRecorder.eventCount() : m_currentMacro.events.size();
}

double ApplicationController::playbackSpeed() const
{
    return m_settings.playbackSpeed();
}

bool ApplicationController::loopPlaybackEnabled() const
{
    return m_loopPlaybackEnabled;
}

bool ApplicationController::smoothMousePlaybackEnabled() const
{
    return m_smoothMousePlaybackEnabled;
}

bool ApplicationController::showDeveloperTools() const
{
    return m_showDeveloperTools;
}

QString ApplicationController::recordShortcut() const
{
    return m_shortcuts.recordShortcut();
}

QString ApplicationController::playShortcut() const
{
    return m_shortcuts.playShortcut();
}

QString ApplicationController::stopShortcut() const
{
    return m_shortcuts.stopShortcut();
}

QStringList ApplicationController::stopShortcutSequences() const
{
    return m_shortcuts.stopShortcutSequences();
}

QString ApplicationController::macroPath() const
{
    return m_macroPath;
}

QUrl ApplicationController::macroDirectoryUrl() const
{
    return QUrl::fromLocalFile(m_settings.macroDirectory());
}

QString ApplicationController::elapsedText() const
{
    if (m_state == AppState::Recording) {
        return formatDuration(m_macroRecorder.elapsedUs());
    }

    if (m_state == AppState::Playing || m_state == AppState::Stopping) {
        return formatDuration(m_playbackElapsedUs);
    }

    return formatDuration(m_currentMacro.durationUs);
}

QString ApplicationController::selectedPlaybackBackend() const
{
    return m_selectedPlaybackBackend;
}

QString ApplicationController::playbackBackendReason() const
{
    return m_playbackBackendReason;
}

QString ApplicationController::recordingSummary() const
{
    return m_globalInputMonitor.listenerStatusText();
}

QString ApplicationController::recordingDetails() const
{
    if (m_captureBackend.isAvailable()) {
        if (m_useDirectCosmicCursorProvider) {
            if (m_cosmicCursorProvider.hasCursorPosition()) {
                return QStringLiteral("Global hotkeys and physical recording are active through evdev. Trusted absolute cursor positions are provided directly by COSMIC through Wayland cursor metadata.");
            }
            return QStringLiteral("Global hotkeys and physical recording are active through evdev. %1")
                .arg(m_cosmicCursorProvider.statusText());
        }

        if (m_cursorTracker.isTracking() && m_cursorTracker.hasCursorPosition()) {
            return QStringLiteral("Global hotkeys and physical recording are active through evdev, with ScreenCast cursor metadata supplying absolute pointer positions for mouse movement, clicks, and wheel anchors.");
        }

        if (!m_cursorTracker.lastError().isEmpty()) {
            return QStringLiteral("Global hotkeys and physical recording are active through evdev, but trustworthy absolute cursor tracking is unavailable: %1")
                .arg(m_cursorTracker.lastError());
        }

        return QStringLiteral("Global hotkeys and physical recording are active through evdev. Absolute cursor tracking is still initializing, so early mouse events may lack trusted anchors.");
    }

    if (m_globalInputMonitor.hasPermissionProblem()) {
        return QStringLiteral("Global input is unavailable because this user cannot open physical /dev/input/event* devices. CatClicker is using focused Qt fallback for recording and application-focused shortcuts.");
    }

    return QStringLiteral("Global input listener is unavailable, so CatClicker is using focused Qt fallback for recording and application-focused shortcuts.");
}

bool ApplicationController::globalHotkeysActive() const
{
    return m_globalInputMonitor.globalHotkeysActive();
}

QString ApplicationController::globalInputListenerText() const
{
    return m_globalInputMonitor.listenerStatusText();
}

QString ApplicationController::globalHotkeysText() const
{
    return m_globalInputMonitor.hotkeyStatusText();
}

QString ApplicationController::activeRecordingBackendText() const
{
    return m_captureBackend.isAvailable()
        ? QStringLiteral("Evdev global capture")
        : QStringLiteral("Qt focused fallback");
}

bool ApplicationController::permissionPromptVisible() const
{
    return m_permissionPromptVisible;
}

QString ApplicationController::permissionPromptMessage() const
{
    return m_permissionPromptMessage;
}

QString ApplicationController::permissionPromptDetails() const
{
    return m_permissionPromptDetails;
}

bool ApplicationController::permissionSetupCanUsePkexec() const
{
    return m_permissionSetupCanUsePkexec;
}

QString ApplicationController::permissionManualCommand() const
{
    return m_permissionManualCommand;
}

bool ApplicationController::permissionSetupInProgress() const
{
    return m_permissionSetupInProgress;
}

QString ApplicationController::permissionSetupStatus() const
{
    return m_permissionSetupStatus;
}

void ApplicationController::setPlaybackSpeed(double value)
{
    m_settings.setPlaybackSpeed(value);
    emit playbackSpeedChanged();
}

void ApplicationController::setLoopPlaybackEnabled(bool value)
{
    if (traceLoopEnabled()) {
        qInfo().noquote() << QStringLiteral("[loop] preference setter old=%1 new=%2 appState=%3")
                                 .arg(m_loopPlaybackEnabled ? 1 : 0)
                                 .arg(value ? 1 : 0)
                                 .arg(appStateString());
    }

    if (m_loopPlaybackEnabled == value) {
        return;
    }

    m_loopPlaybackEnabled = value;
    m_playbackLoopController.setLoopEnabled(value);
    m_settings.setLoopPlaybackEnabled(value);
    emit loopPlaybackChanged();
}

void ApplicationController::setSmoothMousePlaybackEnabled(bool value)
{
    if (m_smoothMousePlaybackEnabled == value) {
        return;
    }
    m_smoothMousePlaybackEnabled = value;
    m_settings.setSmoothMousePlaybackEnabled(value);
    emit smoothMousePlaybackChanged();
}

void ApplicationController::setShowDeveloperTools(bool value)
{
    if (m_showDeveloperTools == value) {
        return;
    }

    m_showDeveloperTools = value;
    m_settings.setShowDeveloperTools(value);
    emit showDeveloperToolsChanged();
}

void ApplicationController::setDarkMode(bool value)
{
    if (m_darkMode == value) {
        return;
    }

    m_darkMode = value;
    m_settings.setDarkMode(value);
    emit themeChanged();
}

void ApplicationController::setRecordShortcut(const QString &value)
{
    const QString oldRecord = m_shortcuts.recordShortcut();
    const QString oldPlay = m_shortcuts.playShortcut();
    const QString oldStop = m_shortcuts.stopShortcut();
    m_shortcuts.setRecordShortcut(value);
    applyShortcutChange(oldRecord, oldPlay, oldStop);
}

void ApplicationController::setPlayShortcut(const QString &value)
{
    const QString oldRecord = m_shortcuts.recordShortcut();
    const QString oldPlay = m_shortcuts.playShortcut();
    const QString oldStop = m_shortcuts.stopShortcut();
    m_shortcuts.setPlayShortcut(value);
    applyShortcutChange(oldRecord, oldPlay, oldStop);
}

void ApplicationController::setStopShortcut(const QString &value)
{
    const QString oldRecord = m_shortcuts.recordShortcut();
    const QString oldPlay = m_shortcuts.playShortcut();
    const QString oldStop = m_shortcuts.stopShortcut();
    m_shortcuts.setStopShortcut(value);
    applyShortcutChange(oldRecord, oldPlay, oldStop);
}

void ApplicationController::startRecording(bool fromShortcut)
{
    if (m_state == AppState::Playing || m_state == AppState::PreparingPlayback || m_state == AppState::Stopping) {
        return;
    }

    if (m_state == AppState::Recording) {
        if (m_activeCaptureBackend) {
            m_activeCaptureBackend->stopCapture();
        }
        traceCursorHealth(QStringLiteral("RECORD STOP"));
        m_currentMacro = m_macroRecorder.finish();
        if (fromShortcut && m_activeCaptureBackend == &m_qtFocusedCaptureBackend) {
            trimTrailingShortcutEvents(shortcutKeyCodesForString(recordShortcut()));
        }
        m_activeCaptureBackend = nullptr;
        setState(AppState::Idle, QStringLiteral("Recording stopped."));
        m_elapsedTimer.stop();
        emit macroChanged();
        emit timingChanged();
        return;
    }

    m_currentMacro.clear();
    m_currentMacro.name = makeRecordingName();
    emit macroChanged();

    m_globalInputMonitor.startMonitoring();
    m_activeCaptureBackend = selectRecordingBackend();
    if (fromShortcut && m_activeCaptureBackend == &m_qtFocusedCaptureBackend) {
        m_qtFocusedCaptureBackend.suppressShortcutUntilRelease(recordShortcut());
    }

    if (!m_activeCaptureBackend || !m_activeCaptureBackend->startCapture()) {
        m_activeCaptureBackend = nullptr;
        setState(AppState::Error, QStringLiteral("Failed to start capture backend."));
        return;
    }

    if (m_useDirectCosmicCursorProvider) {
        if (qEnvironmentVariableIntValue("CATCLICKER_TRACE_CURSOR_HEALTH") == 1) {
            m_cosmicCursorProvider.requestHealthProbe();
        }
    }

    m_macroRecorder.begin(m_portalController.currentDisplayInfo(), m_currentMacro.name);
    traceCursorHealth(QStringLiteral("RECORD START"));
    m_elapsedTimer.start();
    setState(AppState::Recording,
             m_activeCaptureBackend == &m_captureBackend
                 ? QStringLiteral("Recording physical input globally through evdev.")
                 : QStringLiteral("Recording input while CatClicker is focused."));
    emit macroChanged();
    emit timingChanged();
    emit diagnosticsChanged();
}

void ApplicationController::traceCursorHealth(const QString &phase) const
{
    if (qEnvironmentVariableIntValue("CATCLICKER_TRACE_CURSOR_HEALTH") != 1
        || !m_useDirectCosmicCursorProvider) {
        return;
    }
    const CursorProviderHealth health = m_cosmicCursorProvider.healthSnapshot();
    const CursorSamplerHealth sampler = m_captureBackend.samplerHealthSnapshot();
    const QString latest = health.latestPublished.valid
        ? QStringLiteral("%1,%2")
              .arg(health.latestPublished.position.x())
              .arg(health.latestPublished.position.y())
        : QStringLiteral("none");
    qInfo().noquote()
        << QStringLiteral("[cursor-health] %1 workerAlive=%2 loops=%3 dispatches=%4 prepareOk=%5 prepareRetry=%6 polls=%7 waylandReadable=%8 wakeReadable=%9 readOk=%10 readFail=%11 dispatchPending=%12 flushFail=%13 displayError=%14 enter=%15 leave=%16 position=%17 hotspot=%18 publishes=%19 syncDone=%20 generation=%21 recreates=%22 positionAfterRecreate=%23 refreshOutstanding=%24 latest=%25 lastCallbackMonotonicUs=%26 captureSession=%27 relTriggers=%28 refreshRequests=%29 refreshCoalesced=%30 refreshCompletions=%31 samplesDelivered=%32 deferredButtons=%33 deferredScroll=%34 unresolvedDroppedOnStop=%35 samplerRefreshOutstanding=%36 movementPending=%37 followupPending=%38 pendingMouseEvents=%39")
               .arg(phase)
               .arg(health.workerAlive ? 1 : 0)
               .arg(health.workerLoopCount)
               .arg(health.dispatchCount)
               .arg(health.prepareReadSuccessCount)
               .arg(health.prepareReadRetryCount)
               .arg(health.pollCount)
               .arg(health.waylandFdReadableCount)
               .arg(health.wakeFdReadableCount)
               .arg(health.readEventsSuccessCount)
               .arg(health.readEventsFailureCount)
               .arg(health.dispatchPendingCount)
               .arg(health.flushFailureCount)
               .arg(health.wlDisplayError)
               .arg(health.enterCount)
               .arg(health.leaveCount)
               .arg(health.positionCallbackCount)
               .arg(health.hotspotCount)
               .arg(health.snapshotPublishCount)
               .arg(health.syncDoneCount)
               .arg(health.cursorSessionGeneration)
               .arg(health.cursorSessionRecreateCount)
               .arg(health.positionAfterRecreateCount)
               .arg(health.cursorSessionRefreshOutstanding ? 1 : 0)
               .arg(latest)
               .arg(health.latestPositionCallbackMonotonicUs)
               .arg(sampler.captureSessionNumber)
               .arg(sampler.relMovementTriggers)
               .arg(sampler.refreshRequests)
               .arg(sampler.refreshCoalesced)
               .arg(sampler.refreshCompletions)
               .arg(sampler.samplesDelivered)
               .arg(sampler.deferredButtonEvents)
               .arg(sampler.deferredScrollEvents)
               .arg(sampler.unresolvedMouseEventsDroppedOnStop)
               .arg(sampler.refreshOutstanding ? 1 : 0)
               .arg(sampler.movementPending ? 1 : 0)
               .arg(sampler.followUpPending ? 1 : 0)
               .arg(sampler.pendingMouseEventCount);
}

void ApplicationController::stop()
{
    if (m_state == AppState::Recording) {
        startRecording(false);
        return;
    }

    if (m_state == AppState::Playing) {
        m_playbackLoopController.requestStop();
        setState(AppState::Stopping, QStringLiteral("Stopping playback and releasing held input."));
        m_macroPlayer.stopPlayback();
    }
}

void ApplicationController::startPlayback()
{
    if (m_state == AppState::Recording || m_state == AppState::PreparingPlayback || m_state == AppState::Playing || m_state == AppState::Stopping) {
        return;
    }

    if (m_currentMacro.isEmpty()) {
        setState(AppState::Error, QStringLiteral("No macro loaded."));
        return;
    }

    QString compatibilityError;
    if (!m_currentMacro.isCompatibleWith(m_portalController.currentDisplayInfo(), &compatibilityError)) {
        setState(AppState::Error, compatibilityError);
        return;
    }

    if (traceLoopEnabled()) {
        qInfo().noquote() << QStringLiteral("[loop] start playback preference=%1")
                                 .arg(m_loopPlaybackEnabled ? 1 : 0);
    }

    m_activePlaybackLoopToken = m_playbackLoopController.startSequence();
    if (!startPlaybackIteration(m_activePlaybackLoopToken)) {
        m_activePlaybackLoopToken = 0;
    }
}

void ApplicationController::saveMacro()
{
    if (!m_fileChooserPortal.requestSave(QStringLiteral("Save CatClicker Macro"),
                                         QStringLiteral("%1.catmacro").arg(fileNameForMacro(m_currentMacro)),
                                         macroDirectoryUrl())) {
        return;
    }

    setState(AppState::Idle, QStringLiteral("Waiting for the system save dialog."));
}

void ApplicationController::saveMacroToUrl(const QUrl &url)
{
    if (!url.isLocalFile()) {
        setState(AppState::Error, QStringLiteral("Save target must be a local file."));
        return;
    }

    QString path = url.toLocalFile();
    if (!path.endsWith(QStringLiteral(".catmacro"))) {
        path.append(QStringLiteral(".catmacro"));
    }
    saveMacroToPath(path);
}

void ApplicationController::loadMacro()
{
    if (!m_fileChooserPortal.requestOpen(QStringLiteral("Load CatClicker Macro"), macroDirectoryUrl())) {
        return;
    }

    setState(AppState::Idle, QStringLiteral("Waiting for the system open dialog."));
}

void ApplicationController::loadMacroFromUrl(const QUrl &url)
{
    if (!url.isLocalFile()) {
        setState(AppState::Error, QStringLiteral("Load source must be a local file."));
        return;
    }

    loadMacroFromPath(url.toLocalFile());
}

void ApplicationController::generatePlaybackTestMacro()
{
    m_currentMacro = buildPointerTestMacro(m_portalController.currentDisplayInfo());
    m_macroPath.clear();
    setState(AppState::Idle, QStringLiteral("Generated pointer playback test macro."));
    emit macroChanged();
    emit timingChanged();
}

void ApplicationController::generateClickTestMacro(double x, double y)
{
    const MacroDisplayInfo display = m_portalController.currentDisplayInfo();
    if (display.logicalWidth <= 0 || display.logicalHeight <= 0) {
        setState(AppState::Error, QStringLiteral("No usable display geometry is available for click test generation."));
        return;
    }

    m_currentMacro = buildClickTestMacro(display, x, y);
    m_macroPath.clear();
    setState(AppState::Idle,
             QStringLiteral("Generated anchored click test macro at (%1, %2).")
                 .arg(x, 0, 'f', 1)
                 .arg(y, 0, 'f', 1));
    emit macroChanged();
    emit timingChanged();
}

void ApplicationController::generateKeyboardTestMacro()
{
    m_currentMacro = buildKeyboardTestMacro(m_portalController.currentDisplayInfo());
    m_macroPath.clear();
    setState(AppState::Idle, QStringLiteral("Generated keyboard playback test macro. It will type into the currently focused application when played."));
    emit macroChanged();
    emit timingChanged();
}

void ApplicationController::generateHeldKeyStopTestMacro()
{
    m_currentMacro = buildHeldKeyStopTestMacro(m_portalController.currentDisplayInfo());
    m_macroPath.clear();
    setState(AppState::Idle,
             QStringLiteral("Generated held key stop test macro. Press Play, then use the configured Stop shortcut before the 5-second release."));
    emit macroChanged();
    emit timingChanged();
}

void ApplicationController::generateHeldMouseCompletionTestMacro(double x, double y)
{
    m_currentMacro = buildHeldMouseCompletionTestMacro(m_portalController.currentDisplayInfo(), x, y);
    m_macroPath.clear();
    setState(AppState::Idle,
             QStringLiteral("Generated held mouse completion test macro. Place the cursor over the test target and allow the 2-second release to occur normally."));
    emit macroChanged();
    emit timingChanged();
}

void ApplicationController::generateHeldMouseStopTestMacro(double x, double y)
{
    m_currentMacro = buildHeldMouseStopTestMacro(m_portalController.currentDisplayInfo(), x, y);
    m_macroPath.clear();
    setState(AppState::Idle,
             QStringLiteral("Generated held mouse stop test macro. Place the cursor over the test pad, press Play, then stop before the 5-second release."));
    emit macroChanged();
    emit timingChanged();
}

void ApplicationController::generateScrollTestMacro(double x, double y)
{
    m_currentMacro = buildScrollTestMacro(m_portalController.currentDisplayInfo(), x, y);
    m_macroPath.clear();
    setState(AppState::Idle, QStringLiteral("Generated scroll playback test macro."));
    emit macroChanged();
    emit timingChanged();
}

QString ApplicationController::macroDebugDump() const
{
    QStringList lines;
    for (const MacroEvent &event : m_currentMacro.events) {
        if (event.type == MacroEventType::MouseMove) {
            lines << QStringLiteral("t=%1 x=%2 y=%3").arg(event.timeUs / 1000000.0, 0, 'f', 3).arg(event.x, 0, 'f', 1).arg(event.y, 0, 'f', 1);
        } else if (event.type == MacroEventType::MouseButton) {
            lines << QStringLiteral("click t=%1 button=%2 x=%3 y=%4")
                         .arg(event.timeUs / 1000000.0, 0, 'f', 3)
                         .arg(event.button)
                         .arg(event.anchorX, 0, 'f', 1)
                         .arg(event.anchorY, 0, 'f', 1);
        } else if (event.type == MacroEventType::Key) {
            lines << QStringLiteral("key t=%1 code=%2 pressed=%3")
                         .arg(event.timeUs / 1000000.0, 0, 'f', 3)
                         .arg(event.keyCode)
                         .arg(event.pressed);
        } else {
            lines << QStringLiteral("scroll t=%1 dx=%2 dy=%3")
                         .arg(event.timeUs / 1000000.0, 0, 'f', 3)
                         .arg(event.deltaX)
                         .arg(event.deltaY);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void ApplicationController::copyDiagnostics() const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(diagnosticsText());
    }
}

void ApplicationController::enableGlobalInput()
{
    if (m_permissionSetupInProgress) {
        return;
    }

    const QString helperPath = permissionHelperScriptPath();
    if (helperPath.isEmpty()) {
        updatePermissionPromptState(true,
                                    m_permissionPromptMessage,
                                    m_permissionPromptDetails,
                                    QStringLiteral("CatClicker could not find scripts/setup-input-permissions.sh."));
        return;
    }

    const QString pkexecPath = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexecPath.isEmpty()) {
        updatePermissionPromptState(true,
                                    m_permissionPromptMessage,
                                    m_permissionPromptDetails,
                                    QStringLiteral("pkexec is not available here. Run the command below in a terminal."));
        return;
    }

    if (m_permissionSetupProcess) {
        m_permissionSetupProcess->deleteLater();
        m_permissionSetupProcess = nullptr;
    }

    m_permissionSetupProcess = new QProcess(this);
    m_permissionSetupInProgress = true;
    m_permissionSetupStatus = QStringLiteral("Waiting for the system authentication dialog.");
    emit permissionPromptChanged();

    connect(m_permissionSetupProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &ApplicationController::finishPermissionSetupProcess);

    m_permissionSetupProcess->start(pkexecPath, {helperPath});
    if (!m_permissionSetupProcess->waitForStarted(1000)) {
        m_permissionSetupInProgress = false;
        m_permissionSetupStatus = QStringLiteral("Failed to start pkexec. Run the command below in a terminal.");
        m_permissionSetupProcess->deleteLater();
        m_permissionSetupProcess = nullptr;
        emit permissionPromptChanged();
    }
}

void ApplicationController::dismissPermissionPrompt()
{
    m_permissionPromptDismissedForSession = true;
    updatePermissionPromptState(false);
}

void ApplicationController::copyPermissionManualCommand() const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(permissionHelperManualCommand());
    }
}

void ApplicationController::setState(AppState state, const QString &status)
{
    m_state = state;
    m_statusText = status;
    emit appStateChanged();
    emit statusTextChanged();
}

void ApplicationController::refreshDiagnostics()
{
    emit diagnosticsChanged();
}

void ApplicationController::loadSettings()
{
    m_darkMode = m_settings.darkMode();
    m_loopPlaybackEnabled = m_settings.loopPlaybackEnabled();
    m_smoothMousePlaybackEnabled = m_settings.smoothMousePlaybackEnabled();
    m_showDeveloperTools = m_settings.showDeveloperTools();
    m_shortcuts.setRecordShortcut(m_settings.recordShortcut());
    m_shortcuts.setPlayShortcut(m_settings.playShortcut());
    m_shortcuts.setStopShortcut(m_settings.stopShortcut());
    m_playbackLoopController.setLoopEnabled(m_loopPlaybackEnabled);
    if (traceLoopEnabled()) {
        qInfo().noquote() << QStringLiteral("[loop] load settings preference=%1")
                                 .arg(m_loopPlaybackEnabled ? 1 : 0);
    }
}

bool ApplicationController::applyShortcutChange(const QString &oldRecord, const QString &oldPlay, const QString &oldStop)
{
    QString message;
    if (m_shortcuts.hasConflicts(&message)) {
        m_shortcuts.setRecordShortcut(oldRecord);
        m_shortcuts.setPlayShortcut(oldPlay);
        m_shortcuts.setStopShortcut(oldStop);
        emit shortcutsChanged();
        setState(AppState::Error, message);
        return false;
    }

    saveShortcuts();
    return true;
}

void ApplicationController::saveShortcuts()
{
    QString message;
    if (m_shortcuts.hasConflicts(&message)) {
        setState(AppState::Error, message);
        return;
    }

    m_settings.setRecordShortcut(m_shortcuts.recordShortcut());
    m_settings.setPlayShortcut(m_shortcuts.playShortcut());
    m_settings.setStopShortcut(m_shortcuts.stopShortcut());
    updateGlobalShortcutBindings();
    emit shortcutsChanged();
    emit diagnosticsChanged();
}

InputSenderBackend *ApplicationController::selectPlaybackBackend(QString *reason)
{
    m_eiInputSender.setPortalCapabilities(m_portalController.capabilities());
    const PlaybackBackendSelection selection = PlaybackBackendSelector::select(m_portalController.capabilities(),
                                                                               qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
                                                                               &m_eiInputSender,
                                                                               &m_uinputInputSender);
    m_selectedPlaybackBackend = selection.backendName;
    if (reason) {
        *reason = selection.reason;
    }
    return selection.backend;
}

const InputSenderBackend *ApplicationController::activeDiagnosticsBackend() const
{
    return m_selectedInputSender ? static_cast<const InputSenderBackend *>(m_selectedInputSender)
                                 : static_cast<const InputSenderBackend *>(&m_uinputInputSender);
}

bool ApplicationController::startPlaybackIteration(quint64 loopToken)
{
    setState(AppState::PreparingPlayback, QStringLiteral("Selecting playback backend."));
    QString reason;
    m_selectedInputSender = selectPlaybackBackend(&reason);
    m_playbackBackendReason = reason;
    emit diagnosticsChanged();

    if (traceLoopEnabled()) {
        qInfo().noquote() << QStringLiteral("[loop] start iteration token=%1 preference=%2 backend=%3")
                                 .arg(loopToken)
                                 .arg(m_loopPlaybackEnabled ? 1 : 0)
                                 .arg(m_selectedPlaybackBackend.isEmpty() ? QStringLiteral("none") : m_selectedPlaybackBackend);
    }

    if (!m_selectedInputSender) {
        m_playbackLoopController.cancelSequence();
        setState(AppState::Error, reason);
        if (traceLoopEnabled()) {
            qInfo().noquote() << QStringLiteral("[loop] player start result=0 token=%1").arg(loopToken);
        }
        return false;
    }

    if (!m_selectedInputSender->initialize(m_portalController.currentDisplayInfo())) {
        m_playbackLoopController.cancelSequence();
        setState(AppState::Error, QStringLiteral("Failed to initialize %1: %2")
                                   .arg(m_selectedInputSender->backendName(), m_selectedInputSender->statusText()));
        if (traceLoopEnabled()) {
            qInfo().noquote() << QStringLiteral("[loop] player start result=0 token=%1").arg(loopToken);
        }
        return false;
    }

    const bool started = m_macroPlayer.startPlayback(
        m_currentMacro, playbackSpeed(), m_selectedInputSender, m_smoothMousePlaybackEnabled);
    if (traceLoopEnabled()) {
        qInfo().noquote() << QStringLiteral("[loop] player start result=%1 token=%2")
                                 .arg(started ? 1 : 0)
                                 .arg(loopToken);
    }

    if (!started) {
        m_playbackLoopController.cancelSequence();
        setState(AppState::Error, QStringLiteral("Failed to start playback worker."));
        return false;
    }

    m_activePlaybackLoopToken = loopToken;
    return true;
}

InputCaptureBackend *ApplicationController::selectRecordingBackend()
{
    return m_captureBackend.isAvailable()
        ? static_cast<InputCaptureBackend *>(&m_captureBackend)
        : static_cast<InputCaptureBackend *>(&m_qtFocusedCaptureBackend);
}

void ApplicationController::connectCaptureBackend(InputCaptureBackend *backend)
{
    if (!backend) {
        return;
    }

    connect(backend, &InputCaptureBackend::backendError, this, [this](const QString &message) {
        setState(AppState::Error, message);
    });
    connect(backend, &InputCaptureBackend::eventCaptured, this, [this](const MacroEvent &event) {
        m_macroRecorder.appendEvent(event);
        emit timingChanged();
    });
}

void ApplicationController::updateGlobalShortcutBindings()
{
    m_globalInputMonitor.setShortcutBindings(m_shortcuts.recordBinding(),
                                             m_shortcuts.playBinding(),
                                             m_shortcuts.stopBinding());
}

ApplicationController::StartupPermissionState ApplicationController::evaluateStartupPermissions(bool allowPrompt)
{
    const UinputInputSender::AvailabilityProbe uinputProbe = m_uinputInputSender.availabilityProbe();
    const bool globalInputAvailable = m_globalInputMonitor.globalHotkeysActive();
    const bool globalInputPermissionProblem = m_globalInputMonitor.hasPermissionProblem();
    const bool uinputPermissionProblem = uinputProbe.deviceNodeExists
                                         && !uinputProbe.openable
                                         && uinputProbe.openErrorCode == EACCES;
    const bool anyPermissionProblem = globalInputPermissionProblem || uinputPermissionProblem;
    const bool helperPresent = !permissionHelperScriptPath().isEmpty();
    m_permissionSetupCanUsePkexec = helperPresent
                                    && !QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty();
    m_permissionManualCommand = permissionHelperManualCommand();

    if (globalInputAvailable && uinputProbe.openable) {
        updatePermissionPromptState(false);
        return StartupPermissionState::FullyAvailable;
    }

    if (!anyPermissionProblem) {
        updatePermissionPromptState(false);
        return StartupPermissionState::MissingDevices;
    }

    if (allowPrompt && !m_permissionPromptDismissedForSession) {
        QStringList missingAccess;
        if (globalInputPermissionProblem) {
            missingAccess << QStringLiteral("physical input devices");
        }
        if (uinputPermissionProblem) {
            missingAccess << QStringLiteral("/dev/uinput");
        }

        QString details =
            QStringLiteral("CatClicker needs permission to listen for global hotkeys and record input while other apps are focused. Playback also needs access to the virtual input device.");
        if (!missingAccess.isEmpty()) {
            details.append(QStringLiteral(" Missing access: %1.").arg(missingAccess.join(QStringLiteral(" and "))));
        }
        if (!m_permissionSetupCanUsePkexec) {
            details.append(QStringLiteral(" pkexec is unavailable, so run the command below manually."));
        }

        updatePermissionPromptState(true,
                                    QStringLiteral("Enable global input and playback access"),
                                    details,
                                    m_permissionSetupStatus);
    } else if (m_permissionPromptVisible) {
        updatePermissionPromptState(false);
    }

    return StartupPermissionState::PromptForPermissions;
}

QString ApplicationController::permissionHelperScriptPath() const
{
    const QString candidates[] = {
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("scripts/setup-input-permissions.sh")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../scripts/setup-input-permissions.sh")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../scripts/setup-input-permissions.sh")),
        QDir(QDir::currentPath()).absoluteFilePath(QStringLiteral("scripts/setup-input-permissions.sh")),
    };

    for (const QString &candidate : candidates) {
        const QFileInfo info(QDir::cleanPath(candidate));
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }

    return {};
}

QString ApplicationController::permissionHelperManualCommand() const
{
    const QString helperPath = permissionHelperScriptPath();
    if (helperPath.isEmpty()) {
        return {};
    }

    return QStringLiteral("sudo %1").arg(quotedShellArgument(helperPath));
}

void ApplicationController::updatePermissionPromptState(bool visible,
                                                        const QString &message,
                                                        const QString &details,
                                                        const QString &status)
{
    m_permissionPromptVisible = visible;
    if (!message.isNull()) {
        m_permissionPromptMessage = message;
    }
    if (!details.isNull()) {
        m_permissionPromptDetails = details;
    }
    if (!status.isNull()) {
        m_permissionSetupStatus = status;
    }
    emit permissionPromptChanged();
}

void ApplicationController::finishPermissionSetupProcess(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_permissionSetupProcess) {
        m_permissionSetupProcess->deleteLater();
        m_permissionSetupProcess = nullptr;
    }

    m_permissionSetupInProgress = false;
    if (exitStatus != QProcess::NormalExit) {
        updatePermissionPromptState(true,
                                    m_permissionPromptMessage,
                                    m_permissionPromptDetails,
                                    QStringLiteral("Permission setup did not complete normally."));
        return;
    }

    if (exitCode != 0) {
        updatePermissionPromptState(true,
                                    m_permissionPromptMessage,
                                    m_permissionPromptDetails,
                                    QStringLiteral("Permission setup was cancelled or failed."));
        return;
    }

    m_globalInputMonitor.stopMonitoring();
    m_globalInputMonitor.startMonitoring();
    const StartupPermissionState state = evaluateStartupPermissions(false);
    if (state == StartupPermissionState::FullyAvailable) {
        updatePermissionPromptState(false,
                                    m_permissionPromptMessage,
                                    m_permissionPromptDetails,
                                    QStringLiteral("Global input permissions are active."));
        refreshDiagnostics();
        return;
    }

    updatePermissionPromptState(true,
                                QStringLiteral("Finish enabling global input"),
                                QStringLiteral("Setup completed, but this session still cannot open all required devices. Log out and back in, then relaunch CatClicker."),
                                QStringLiteral("Permission setup finished. A session refresh is still required."));
    refreshDiagnostics();
}

void ApplicationController::trimTrailingShortcutEvents(const QSet<uint32_t> &shortcutKeyCodes)
{
    if (shortcutKeyCodes.isEmpty()) {
        return;
    }

    while (!m_currentMacro.events.isEmpty()) {
        const MacroEvent &event = m_currentMacro.events.last();
        if (event.type != MacroEventType::Key || !shortcutKeyCodes.contains(event.keyCode)) {
            break;
        }
        m_currentMacro.events.removeLast();
    }

    m_currentMacro.durationUs = m_currentMacro.events.isEmpty() ? 0 : m_currentMacro.events.last().timeUs;
}

bool ApplicationController::saveMacroToPath(const QString &path)
{
    if (m_currentMacro.isEmpty()) {
        setState(AppState::Error, QStringLiteral("No macro available to save."));
        return false;
    }

    const QString directory = QFileInfo(path).absolutePath();
    QDir().mkpath(directory);

    QString error;
    if (MacroSerializer::saveToFile(path, m_currentMacro, &error)) {
        m_macroPath = path;
        setState(AppState::Idle, QStringLiteral("Saved macro to %1").arg(path));
        emit macroChanged();
        return true;
    }

    setState(AppState::Error, QStringLiteral("Failed to save macro: %1").arg(error));
    return false;
}

bool ApplicationController::loadMacroFromPath(const QString &path)
{
    QString error;
    Macro macro;
    if (MacroSerializer::loadFromFile(path, &macro, &error)) {
        m_currentMacro = macro;
        m_macroPath = path;
        setState(AppState::Idle, QStringLiteral("Loaded macro from %1").arg(path));
        emit macroChanged();
        emit timingChanged();
        return true;
    }

    setState(AppState::Error, QStringLiteral("Failed to load macro: %1").arg(error));
    return false;
}

}
