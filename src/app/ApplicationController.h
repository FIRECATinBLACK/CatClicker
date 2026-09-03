#pragma once

#include "../diagnostics/Diagnostics.h"
#include "../hotkeys/GlobalShortcutManager.h"
#include "../input/CursorTracker.h"
#include "../input/CosmicCursorPositionProvider.h"
#include "../input/EvdevDeviceInspector.h"
#include "../input/GlobalInputMonitor.h"
#include "../input/EiInputSender.h"
#include "../input/FileChooserPortal.h"
#include "../input/EvdevCaptureBackend.h"
#include "../input/PortalController.h"
#include "../input/QtFocusedCaptureBackend.h"
#include "../input/UinputInputSender.h"
#include "../macro/MacroPlayer.h"
#include "../macro/MacroRecorder.h"
#include "../persistence/Settings.h"
#include "PlaybackLoopController.h"

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUrl>

namespace CatClicker {

class ApplicationController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString appState READ appStateString NOTIFY appStateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString diagnosticsText READ diagnosticsText NOTIFY diagnosticsChanged)
    Q_PROPERTY(int virtualHeldKeyCount READ virtualHeldKeyCount NOTIFY diagnosticsChanged)
    Q_PROPERTY(int virtualHeldButtonCount READ virtualHeldButtonCount NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY themeChanged)
    Q_PROPERTY(QString macroName READ macroName NOTIFY macroChanged)
    Q_PROPERTY(int eventCount READ eventCount NOTIFY macroChanged)
    Q_PROPERTY(double playbackSpeed READ playbackSpeed WRITE setPlaybackSpeed NOTIFY playbackSpeedChanged)
    Q_PROPERTY(bool loopPlaybackEnabled READ loopPlaybackEnabled WRITE setLoopPlaybackEnabled NOTIFY loopPlaybackChanged)
    Q_PROPERTY(bool smoothMousePlaybackEnabled READ smoothMousePlaybackEnabled WRITE setSmoothMousePlaybackEnabled NOTIFY smoothMousePlaybackChanged)
    Q_PROPERTY(bool showDeveloperTools READ showDeveloperTools WRITE setShowDeveloperTools NOTIFY showDeveloperToolsChanged)
    Q_PROPERTY(bool compactInterface READ compactInterface WRITE setCompactInterface NOTIFY interfaceModeChanged)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
    Q_PROPERTY(QString buildCommit READ buildCommit CONSTANT)
    Q_PROPERTY(QString recordShortcut READ recordShortcut WRITE setRecordShortcut NOTIFY shortcutsChanged)
    Q_PROPERTY(QString playShortcut READ playShortcut WRITE setPlayShortcut NOTIFY shortcutsChanged)
    Q_PROPERTY(QString stopShortcut READ stopShortcut WRITE setStopShortcut NOTIFY shortcutsChanged)
    Q_PROPERTY(QStringList stopShortcutSequences READ stopShortcutSequences NOTIFY shortcutsChanged)
    Q_PROPERTY(QString macroPath READ macroPath NOTIFY macroChanged)
    Q_PROPERTY(QUrl macroDirectoryUrl READ macroDirectoryUrl NOTIFY macroChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY timingChanged)
    Q_PROPERTY(QString selectedPlaybackBackend READ selectedPlaybackBackend NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString playbackBackendReason READ playbackBackendReason NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString recordingSummary READ recordingSummary NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString recordingDetails READ recordingDetails NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool globalHotkeysActive READ globalHotkeysActive NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString globalInputListenerText READ globalInputListenerText NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString globalHotkeysText READ globalHotkeysText NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString activeRecordingBackendText READ activeRecordingBackendText NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool permissionPromptVisible READ permissionPromptVisible NOTIFY permissionPromptChanged)
    Q_PROPERTY(QString permissionPromptMessage READ permissionPromptMessage NOTIFY permissionPromptChanged)
    Q_PROPERTY(QString permissionPromptDetails READ permissionPromptDetails NOTIFY permissionPromptChanged)
    Q_PROPERTY(bool permissionSetupCanUsePkexec READ permissionSetupCanUsePkexec NOTIFY permissionPromptChanged)
    Q_PROPERTY(QString permissionManualCommand READ permissionManualCommand NOTIFY permissionPromptChanged)
    Q_PROPERTY(bool permissionSetupInProgress READ permissionSetupInProgress NOTIFY permissionPromptChanged)
    Q_PROPERTY(QString permissionSetupStatus READ permissionSetupStatus NOTIFY permissionPromptChanged)
    Q_PROPERTY(bool permissionSetupNeedsSessionRefresh READ permissionSetupNeedsSessionRefresh NOTIFY permissionPromptChanged)
    Q_PROPERTY(bool inputPermissionSetupRequired READ inputPermissionSetupRequired NOTIFY permissionPromptChanged)
    Q_PROPERTY(QString inputPermissionStateText READ inputPermissionStateText NOTIFY permissionPromptChanged)

public:
    enum class AppState {
        Idle,
        PreparingRecording,
        Recording,
        PreparingPlayback,
        Playing,
        Stopping,
        Error
    };
    Q_ENUM(AppState)

    explicit ApplicationController(QObject *parent = nullptr);
    ~ApplicationController() override;

    QString appStateString() const;
    QString statusText() const;
    QString diagnosticsText() const;
    int virtualHeldKeyCount() const;
    int virtualHeldButtonCount() const;
    bool darkMode() const;
    QString macroName() const;
    int eventCount() const;
    double playbackSpeed() const;
    bool loopPlaybackEnabled() const;
    bool smoothMousePlaybackEnabled() const;
    bool showDeveloperTools() const;
    bool compactInterface() const;
    QString applicationVersion() const;
    QString buildCommit() const;
    QString recordShortcut() const;
    QString playShortcut() const;
    QString stopShortcut() const;
    QStringList stopShortcutSequences() const;
    QString macroPath() const;
    QUrl macroDirectoryUrl() const;
    QString elapsedText() const;
    QString selectedPlaybackBackend() const;
    QString playbackBackendReason() const;
    QString recordingSummary() const;
    QString recordingDetails() const;
    bool globalHotkeysActive() const;
    QString globalInputListenerText() const;
    QString globalHotkeysText() const;
    QString activeRecordingBackendText() const;
    bool permissionPromptVisible() const;
    QString permissionPromptMessage() const;
    QString permissionPromptDetails() const;
    bool permissionSetupCanUsePkexec() const;
    QString permissionManualCommand() const;
    bool permissionSetupInProgress() const;
    QString permissionSetupStatus() const;
    bool permissionSetupNeedsSessionRefresh() const;
    bool inputPermissionSetupRequired() const;
    QString inputPermissionStateText() const;

    void setPlaybackSpeed(double value);
    void setLoopPlaybackEnabled(bool value);
    void setSmoothMousePlaybackEnabled(bool value);
    void setShowDeveloperTools(bool value);
    void setCompactInterface(bool value);
    void setDarkMode(bool value);
    void setRecordShortcut(const QString &value);
    void setPlayShortcut(const QString &value);
    void setStopShortcut(const QString &value);

    Q_INVOKABLE void startRecording(bool fromShortcut = false);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void startPlayback();
    Q_INVOKABLE void saveMacro();
    Q_INVOKABLE void saveMacroToUrl(const QUrl &url);
    Q_INVOKABLE void loadMacro();
    Q_INVOKABLE void loadMacroFromUrl(const QUrl &url);
    Q_INVOKABLE void generatePlaybackTestMacro();
    Q_INVOKABLE void generateClickTestMacro(double x, double y);
    Q_INVOKABLE void generateKeyboardTestMacro();
    Q_INVOKABLE void generateHeldKeyStopTestMacro();
    Q_INVOKABLE void generateHeldMouseCompletionTestMacro(double x, double y);
    Q_INVOKABLE void generateHeldMouseStopTestMacro(double x, double y);
    Q_INVOKABLE void generateScrollTestMacro(double x, double y);
    Q_INVOKABLE QString macroDebugDump() const;
    Q_INVOKABLE void copyDiagnostics() const;
    Q_INVOKABLE void enableGlobalInput();
    Q_INVOKABLE void dismissPermissionPrompt();
    Q_INVOKABLE void showPermissionSetup();
    Q_INVOKABLE void recheckInputPermissions();
    Q_INVOKABLE void openProjectWebsite() const;
    Q_INVOKABLE void copyPermissionManualCommand() const;

signals:
    void appStateChanged();
    void statusTextChanged();
    void diagnosticsChanged();
    void themeChanged();
    void macroChanged();
    void playbackSpeedChanged();
    void loopPlaybackChanged();
    void smoothMousePlaybackChanged();
    void showDeveloperToolsChanged();
    void interfaceModeChanged();
    void shortcutsChanged();
    void timingChanged();
    void permissionPromptChanged();

private:
    enum class StartupPermissionState {
        FullyAvailable,
        PromptForPermissions,
        MissingDevices
    };

    AppState m_state = AppState::Idle;
    QString m_statusText = QStringLiteral("Ready");
    bool m_darkMode = true;
    bool m_loopPlaybackEnabled = false;
    bool m_smoothMousePlaybackEnabled = false;
    bool m_showDeveloperTools = false;
    bool m_compactInterface = false;
    QString m_macroPath;
    QString m_selectedPlaybackBackend;
    QString m_playbackBackendReason;
    QString m_permissionPromptMessage;
    QString m_permissionPromptDetails;
    QString m_permissionManualCommand;
    QString m_permissionSetupStatus;
    qint64 m_playbackElapsedUs = 0;
    quint64 m_activePlaybackLoopToken = 0;
    bool m_permissionPromptVisible = false;
    bool m_permissionSetupCanUsePkexec = false;
    bool m_permissionSetupInProgress = false;
    bool m_permissionPromptDismissedForSession = false;
    bool m_permissionSetupNeedsSessionRefresh = false;
    bool m_useDirectCosmicCursorProvider = false;
    Macro m_currentMacro;
    FileChooserPortal m_fileChooserPortal;
    PortalController m_portalController;
    CursorTracker m_cursorTracker;
    CosmicCursorPositionProvider m_cosmicCursorProvider;
    GlobalInputMonitor m_globalInputMonitor;
    QtFocusedCaptureBackend m_qtFocusedCaptureBackend;
    EvdevCaptureBackend m_captureBackend;
    EvdevDeviceInspector m_evdevInspector;
    EiInputSender m_eiInputSender;
    UinputInputSender m_uinputInputSender;
    InputSenderBackend *m_selectedInputSender = nullptr;
    InputCaptureBackend *m_activeCaptureBackend = nullptr;
    MacroRecorder m_macroRecorder;
    MacroPlayer m_macroPlayer;
    GlobalShortcutManager m_shortcuts;
    Settings m_settings;
    Diagnostics m_diagnostics;
    QTimer m_elapsedTimer;
    QTimer m_cursorHealthTimer;
    PlaybackLoopController m_playbackLoopController;
    QProcess *m_permissionSetupProcess = nullptr;

    void setState(AppState state, const QString &status);
    void refreshDiagnostics();
    void loadSettings();
    bool applyShortcutChange(const QString &oldRecord, const QString &oldPlay, const QString &oldStop);
    void saveShortcuts();
    InputSenderBackend *selectPlaybackBackend(QString *reason);
    const InputSenderBackend *activeDiagnosticsBackend() const;
    void trimTrailingShortcutEvents(const QSet<uint32_t> &shortcutKeyCodes);
    bool saveMacroToPath(const QString &path);
    bool loadMacroFromPath(const QString &path);
    bool startPlaybackIteration(quint64 loopToken);
    InputCaptureBackend *selectRecordingBackend();
    void connectCaptureBackend(InputCaptureBackend *backend);
    void updateGlobalShortcutBindings();
    StartupPermissionState evaluateStartupPermissions(bool allowPrompt);
    QString permissionHelperScriptPath() const;
    QString permissionHelperManualCommand() const;
    void updatePermissionPromptState(bool visible,
                                     const QString &message = QString(),
                                     const QString &details = QString(),
                                     const QString &status = QString());
    void finishPermissionSetupProcess(int exitCode, QProcess::ExitStatus exitStatus);
    void traceCursorHealth(const QString &phase) const;
};

}
