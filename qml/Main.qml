import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CatClicker

ApplicationWindow {
    id: rootWindow
    visible: true
    width: 1100
    height: 820
    minimumWidth: 960
    minimumHeight: 720
    title: "CatClicker"
    color: Theme.windowBackground
    property int clickTestCount: 0
    property int scrollUpCount: 0
    property int scrollDownCount: 0
    property int heldMousePressCount: 0
    property int heldMouseReleaseCount: 0
    property real regularWindowWidth: 1100
    property real regularWindowHeight: 820
    readonly property real compactWindowWidth: 540
    readonly property real compactWindowHeight: 68
    property bool modeTransitionInProgress: false
    property bool compactModeApplied: false
    onWidthChanged: {
        if (!modeTransitionInProgress && !compactModeApplied) regularWindowWidth = width
    }
    onHeightChanged: {
        if (!modeTransitionInProgress && !compactModeApplied) regularWindowHeight = height
    }

    function applyInterfaceMode() {
        settingsWindow.close()
        modeTransitionInProgress = true
        if (appController.compactInterface && !compactModeApplied) {
            regularWindowWidth = Math.max(rootWindow.width, 960)
            regularWindowHeight = Math.max(rootWindow.height, 720)
        }
        Qt.callLater(function() {
            if (appController.compactInterface) {
                rootWindow.minimumWidth = compactWindowWidth
                rootWindow.maximumWidth = compactWindowWidth
                rootWindow.minimumHeight = compactWindowHeight
                rootWindow.maximumHeight = compactWindowHeight
                rootWindow.width = compactWindowWidth
                rootWindow.height = compactWindowHeight
            } else {
                rootWindow.maximumWidth = 16777215
                rootWindow.maximumHeight = 16777215
                rootWindow.minimumWidth = 960
                rootWindow.minimumHeight = 720
                rootWindow.width = Math.max(regularWindowWidth, 960)
                rootWindow.height = Math.max(regularWindowHeight, 720)
            }
            Qt.callLater(function() {
                rootWindow.compactModeApplied = appController.compactInterface
                rootWindow.modeTransitionInProgress = false
            })
        })
    }

    function openSettings() {
        settingsWindow.show()
        settingsWindow.requestActivate()
    }

    function closeSettingsIfInactiveLater() {
        Qt.callLater(function() {
            if (settingsWindow.visible && !settingsWindow.active
                    && !settingsWindow.internalPopupOpen) settingsWindow.close()
        })
    }

    Connections {
        target: appController
        function onInterfaceModeChanged() {
            rootWindow.applyInterfaceMode()
        }
    }

    Window {
        id: settingsWindow
        property bool internalPopupOpen: interfaceModeCombo.popup.visible
        visible: false
        width: 460
        height: 720
        minimumWidth: 420
        minimumHeight: 560
        title: "CatClicker Settings"
        color: Theme.windowBackground
        transientParent: rootWindow
        onActiveChanged: {
            if (visible && !active) rootWindow.closeSettingsIfInactiveLater()
        }

        Connections {
            target: interfaceModeCombo.popup
            function onClosed() {
                rootWindow.closeSettingsIfInactiveLater()
            }
        }

        Flickable {
            anchors.fill: parent
            anchors.margins: 24
            contentHeight: settingsColumn.implicitHeight
            clip: true

          Column {
            id: settingsColumn
            width: parent.width
            spacing: 12

            Text {
                text: "Settings"
                color: Theme.textPrimary
                font.pixelSize: 28
                font.bold: true
            }

            Text {
                width: parent.width
                text: "Window, playback, and interface settings."
                color: Theme.textSecondary
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }

            Rectangle {
                width: parent.width
                implicitHeight: smoothingRow.implicitHeight + 32
                radius: 18
                color: Theme.cardBackgroundAlt
                border.width: 1
                border.color: Theme.outline

                RowLayout {
                    id: smoothingRow
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "Smooth mouse playback"
                            color: Theme.textPrimary
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Linearly interpolate between consecutive recorded pointer moves. Click and scroll coordinates remain exact."
                            color: Theme.textMuted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                    }

                    ToggleSwitch {
                        checked: appController.smoothMousePlaybackEnabled
                        onToggledByUser: function(checked) {
                            appController.smoothMousePlaybackEnabled = checked
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: interfaceModeRow.implicitHeight + 32
                radius: 18
                color: Theme.cardBackgroundAlt
                border.width: 1
                border.color: Theme.outline

                RowLayout {
                    id: interfaceModeRow
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "Interface mode"
                            color: Theme.textPrimary
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Choose the full interface or a small toolbar."
                            color: Theme.textMuted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                    }

                    StyledComboBox {
                        id: interfaceModeCombo
                        model: ["Regular", "Compact"]
                        currentIndex: appController.compactInterface ? 1 : 0
                        implicitWidth: 130
                        onActivated: {
                            settingsWindow.close()
                            appController.compactInterface = currentIndex === 1
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: loopSettingsRow.implicitHeight + 32
                radius: 18
                color: Theme.cardBackgroundAlt
                border.width: 1
                border.color: Theme.outline

                RowLayout {
                    id: loopSettingsRow
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14
                    Text { Layout.fillWidth: true; text: "Loop playback"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
                    ToggleSwitch {
                        checked: appController.loopPlaybackEnabled
                        onToggledByUser: function(checked) {
                            appController.loopPlaybackEnabled = checked
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: themeSettingsRow.implicitHeight + 32
                radius: 18
                color: Theme.cardBackgroundAlt
                border.width: 1
                border.color: Theme.outline
                RowLayout {
                    id: themeSettingsRow
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14
                    Text { Layout.fillWidth: true; text: "Dark theme"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
                    ToggleSwitch {
                        checked: appController.darkMode
                        onToggledByUser: function(checked) {
                            appController.darkMode = checked
                            Theme.darkMode = checked
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: settingsShortcutColumn.implicitHeight + 32
                radius: 18
                color: Theme.cardBackgroundAlt
                border.width: 1
                border.color: Theme.outline
                Column {
                    id: settingsShortcutColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    Text { text: "Shortcuts"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
                    ShortcutField { label: "Record / Stop Recording"; value: appController.recordShortcut; onShortcutEdited: function(value) { appController.recordShortcut = value } }
                    ShortcutField { label: "Play"; value: appController.playShortcut; onShortcutEdited: function(value) { appController.playShortcut = value } }
                    ShortcutField { label: "Stop Playback"; value: appController.stopShortcut; onShortcutEdited: function(value) { appController.stopShortcut = value } }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: 52
                visible: appController.inputPermissionSetupRequired
                radius: 16
                color: Theme.cardBackgroundAlt
                border.width: 1
                border.color: Theme.outline

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    Text {
                        Layout.fillWidth: true
                        text: appController.inputPermissionStateText
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    ActionButton {
                        text: "Set up input permissions"
                        accentColor: Theme.primaryPink
                        implicitWidth: 205
                        onClicked: {
                            settingsWindow.close()
                            appController.showPermissionSetup()
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: settingsToolsRow.implicitHeight + 32
                radius: 18
                color: Theme.cardBackgroundAlt
                border.width: 1
                border.color: Theme.outline

                RowLayout {
                    id: settingsToolsRow
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "Developer Info"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
                        Text {
                            Layout.fillWidth: true
                            text: "Show implementation details, safe diagnostics, and developer test controls."
                            color: Theme.textMuted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                    }
                    ToggleSwitch {
                        checked: appController.showDeveloperTools
                        onToggledByUser: function(checked) { appController.showDeveloperTools = checked }
                    }
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "CatClicker " + appController.applicationVersion
                color: Theme.textMuted
                font.pixelSize: 12
            }

            Rectangle {
                width: parent.width
                implicitHeight: 52
                radius: 16
                color: Theme.panelBackground
                border.width: 1
                border.color: Theme.outline

                Text {
                    anchors.centerIn: parent
                    text: "Close"
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: settingsWindow.close()
                }
            }
          }
        }
    }

    Popup {
        id: permissionPopup
        parent: Overlay.overlay
        modal: true
        focus: visible
        visible: appController.permissionPromptVisible
        width: Math.min(rootWindow.width - 48, 560)
        x: (parent.width - width) / 2
        y: Math.max(24, (parent.height - implicitHeight) / 2)
        padding: 24
        closePolicy: Popup.NoAutoClose
        background: Rectangle {
            radius: 28
            color: Theme.cardBackground
            border.width: 1
            border.color: Theme.outlineStrong
        }

        contentItem: ColumnLayout {
            width: parent.width
            spacing: 16

            Text {
                Layout.fillWidth: true
                text: appController.permissionPromptMessage
                color: Theme.textPrimary
                font.pixelSize: 28
                font.bold: true
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                text: "Global shortcuts and recording outside CatClicker need physical input access. Playback needs /dev/uinput."
                color: Theme.textPrimary
                font.pixelSize: 15
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                visible: appController.permissionPromptDetails.length > 0
                text: appController.permissionPromptDetails
                color: Theme.textSecondary
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true
                visible: appController.permissionSetupStatus.length > 0
                radius: 18
                color: Theme.cardBackgroundAlt
                border.width: 1
                border.color: Theme.outline
                implicitHeight: permissionStatusText.implicitHeight + 24

                Text {
                    id: permissionStatusText
                    anchors.fill: parent
                    anchors.margins: 12
                    text: appController.permissionSetupStatus
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: !appController.permissionSetupCanUsePkexec && appController.permissionManualCommand.length > 0
                radius: 18
                color: Theme.panelBackground
                border.width: 1
                border.color: Theme.outline
                implicitHeight: manualCommandColumn.implicitHeight + 24

                ColumnLayout {
                    id: manualCommandColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: "Run this command in a terminal:"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    TextArea {
                        Layout.fillWidth: true
                        readOnly: true
                        text: appController.permissionManualCommand
                        color: Theme.textPrimary
                        wrapMode: Text.WrapAnywhere
                        background: Rectangle {
                            radius: 14
                            color: Theme.cardBackgroundAlt
                            border.width: 1
                            border.color: Theme.outline
                        }
                        font.family: "monospace"
                        implicitHeight: Math.max(56, contentHeight + 18)
                    }

                    ActionButton {
                        Layout.alignment: Qt.AlignLeft
                        text: "Copy command"
                        accentColor: Theme.primaryLavender
                        onClicked: appController.copyPermissionManualCommand()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ActionButton {
                    Layout.fillWidth: true
                    text: appController.permissionSetupNeedsSessionRefresh ? "Recheck access" : "Enable global input"
                    accentColor: Theme.primaryPink
                    enabled: !appController.permissionSetupInProgress
                    onClicked: {
                        if (appController.permissionSetupNeedsSessionRefresh) {
                            appController.recheckInputPermissions()
                        } else {
                            appController.enableGlobalInput()
                        }
                    }
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: appController.permissionSetupNeedsSessionRefresh ? "Close" : "Not now"
                    accentColor: Theme.primaryBlue
                    enabled: !appController.permissionSetupInProgress
                    onClicked: appController.dismissPermissionPrompt()
                }
            }
        }
    }

    Component.onCompleted: {
        Theme.darkMode = appController.darkMode
        rootWindow.applyInterfaceMode()
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.backgroundStart }
            GradientStop { position: 0.52; color: Theme.backgroundMid }
            GradientStop { position: 1.0; color: Theme.backgroundEnd }
        }
    }

    Rectangle {
        width: 360
        height: 360
        x: -90
        y: -80
        radius: width / 2
        color: Qt.rgba(Theme.backgroundGlowPink.r, Theme.backgroundGlowPink.g, Theme.backgroundGlowPink.b, Theme.darkMode ? 0.16 : 0.28)
    }

    Rectangle {
        width: 300
        height: 300
        x: parent ? parent.width - 230 : 0
        y: 48
        radius: width / 2
        color: Qt.rgba(Theme.backgroundGlowBlue.r, Theme.backgroundGlowBlue.g, Theme.backgroundGlowBlue.b, Theme.darkMode ? 0.16 : 0.24)
    }

    Rectangle {
        width: 220
        height: 220
        x: parent ? parent.width * 0.42 : 0
        y: parent ? parent.height * 0.08 : 0
        radius: width / 2
        color: Qt.rgba(Theme.backgroundGlowLavender.r, Theme.backgroundGlowLavender.g, Theme.backgroundGlowLavender.b, Theme.darkMode ? 0.11 : 0.18)
    }

    Repeater {
        model: 10

        Rectangle {
            required property int index
            width: index % 3 === 0 ? 4 : 3
            height: width
            radius: width / 2
            x: 90 + (index * 87) % Math.max(200, parent.width - 120)
            y: 34 + (index * 113) % Math.max(220, parent.height - 120)
            color: Qt.rgba(1, 1, 1, Theme.darkMode ? 0.1 : 0.22)
        }
    }

    Shortcut {
        sequence: appController.recordShortcut
        context: Qt.ApplicationShortcut
        enabled: !recordShortcutField.listening
                 && !playShortcutField.listening
                 && !stopShortcutField.listening
                 && !appController.globalHotkeysActive
                 && appController.appState !== "Playing"
                 && appController.appState !== "Preparing Playback"
                 && appController.appState !== "Stopping"
        onActivated: appController.startRecording(true)
    }

    Shortcut {
        sequence: appController.playShortcut
        context: Qt.ApplicationShortcut
        enabled: !recordShortcutField.listening
                 && !playShortcutField.listening
                 && !stopShortcutField.listening
                 && !appController.globalHotkeysActive
                 && appController.appState !== "Recording"
                 && appController.appState !== "Preparing Playback"
                 && appController.appState !== "Playing"
                 && appController.appState !== "Stopping"
        onActivated: appController.startPlayback()
    }

    Shortcut {
        sequences: appController.stopShortcutSequences
        context: Qt.ApplicationShortcut
        enabled: !recordShortcutField.listening
                 && !playShortcutField.listening
                 && !stopShortcutField.listening
                 && !appController.globalHotkeysActive
                 && appController.appState === "Playing"
        onActivated: appController.stop()
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 8
        visible: appController.compactInterface
        radius: 24
        color: Theme.cardBackground
        border.width: 1
        border.color: Theme.outlineStrong

        RowLayout {
            anchors.centerIn: parent
            spacing: 8

            Image {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                source: "qrc:/CatClicker/branding/catclicker.png"
                fillMode: Image.PreserveAspectFit
                Layout.rightMargin: 4
            }

            StatusPill {
                Layout.preferredHeight: 32
                Layout.rightMargin: 4
                text: appController.appState === "Recording" ? "REC"
                    : appController.appState === "Playing" ? "PLAY" : "IDLE"
                accentColor: appController.appState === "Recording" ? Theme.primaryPink
                    : appController.appState === "Playing" ? Theme.primaryBlue : Theme.primaryLavender
            }

            CompactIconButton {
                iconSource: "qrc:/CatClicker/compact/assets/icons/compact/record.svg"
                toolTipText: "Record"
                accentColor: Theme.primaryPink
                enabled: appController.appState !== "Playing" && appController.appState !== "Stopping"
                onClicked: appController.startRecording(false)
            }
            CompactIconButton {
                iconSource: "qrc:/CatClicker/compact/assets/icons/compact/play.svg"
                toolTipText: "Play"
                accentColor: Theme.primaryBlue
                enabled: appController.appState !== "Recording"
                    && appController.appState !== "Playing"
                    && appController.appState !== "Stopping"
                onClicked: appController.startPlayback()
            }
            CompactIconButton {
                iconSource: "qrc:/CatClicker/compact/assets/icons/compact/stop.svg"
                toolTipText: "Stop"
                accentColor: Theme.stopAccent
                enabled: appController.appState === "Playing"
                onClicked: appController.stop()
            }
            CompactIconButton {
                iconSource: "qrc:/CatClicker/compact/assets/icons/compact/open.svg"
                toolTipText: "Open macro"
                accentColor: Theme.primaryPink
                onClicked: appController.loadMacro()
            }
            CompactIconButton {
                iconSource: "qrc:/CatClicker/compact/assets/icons/compact/save.svg"
                toolTipText: "Save macro"
                accentColor: Theme.primaryBlue
                onClicked: appController.saveMacro()
            }
            CompactIconButton {
                iconSource: "qrc:/CatClicker/compact/assets/icons/compact/settings.svg"
                toolTipText: "Settings"
                accentColor: Theme.primaryLavender
                onClicked: rootWindow.openSettings()
            }
        }
    }

    ScrollView {
        visible: !appController.compactInterface
        anchors.fill: parent
        contentWidth: availableWidth
        leftPadding: 32
        rightPadding: 32
        topPadding: 30
        bottomPadding: 30
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 22

            InfoCard {
                Layout.fillWidth: true

                ColumnLayout {
                    width: parent.width
                    spacing: 18

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 18

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Image {
                                Layout.preferredWidth: 70
                                Layout.preferredHeight: 70
                                Layout.alignment: Qt.AlignTop
                                source: "qrc:/CatClicker/branding/catclicker.png"
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                mipmap: true
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: "CatClicker"
                                    color: Theme.textPrimary
                                    font.family: "Inter"
                                    font.pixelSize: 36
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 0.3
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "Macro recording and playback for Pop!_OS COSMIC on Wayland"
                                    color: Theme.textSecondary
                                    font.pixelSize: 15
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        StatusPill {
                            Layout.alignment: Qt.AlignTop
                            text: appController.appState
                            accentColor: appController.appState === "Error" ? Theme.danger :
                                         appController.appState === "Playing" ? Theme.primaryBlue :
                                         appController.appState === "Stopping" ? Theme.stopAccent :
                                         Theme.primaryPink
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            width: 180
                            height: 78
                            radius: 20
                            color: Theme.cardBackgroundAlt
                            border.width: 1
                            border.color: appController.loopPlaybackEnabled ? Theme.primaryPink : Theme.outline

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2

                                    Text {
                                        text: "Loop playback"
                                        Layout.fillWidth: true
                                        color: Theme.textSecondary
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: appController.loopPlaybackEnabled ? "On" : "Off"
                                        Layout.fillWidth: true
                                        color: Theme.textPrimary
                                        font.pixelSize: 16
                                        font.bold: true
                                    }
                                }

                                ToggleSwitch {
                                    Layout.preferredWidth: 58
                                    Layout.alignment: Qt.AlignVCenter
                                    checked: appController.loopPlaybackEnabled
                                    onToggledByUser: function(checked) {
                                        appController.loopPlaybackEnabled = checked
                                    }
                                }
                            }
                        }

                        Rectangle {
                            width: 170
                            height: 78
                            radius: 20
                            color: Theme.cardBackgroundAlt
                            border.width: 1
                            border.color: Theme.outline

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2

                                    Text {
                                        text: "Theme"
                                        Layout.fillWidth: true
                                        color: Theme.textSecondary
                                        font.pixelSize: 13
                                    }

                                    Text {
                                        text: appController.darkMode ? "Dark mode" : "Light mode"
                                        Layout.fillWidth: true
                                        color: Theme.textPrimary
                                        font.pixelSize: 16
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                }

                                ToggleSwitch {
                                    Layout.preferredWidth: 58
                                    Layout.alignment: Qt.AlignVCenter
                                    checked: appController.darkMode
                                    onToggledByUser: function(checked) {
                                        appController.darkMode = checked
                                        Theme.darkMode = checked
                                    }
                                }
                            }
                        }

                        Rectangle {
                            width: 190
                            height: 78
                            radius: 20
                            color: Theme.cardBackgroundAlt
                            border.width: 1
                            border.color: appController.compactInterface ? Theme.primaryBlue : Theme.outline

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2

                                    Text {
                                        text: "Compact mode"
                                        Layout.fillWidth: true
                                        color: Theme.textSecondary
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: appController.compactInterface ? "On" : "Off"
                                        Layout.fillWidth: true
                                        color: Theme.textPrimary
                                        font.pixelSize: 16
                                        font.bold: true
                                    }
                                }

                                ToggleSwitch {
                                    Layout.preferredWidth: 58
                                    Layout.alignment: Qt.AlignVCenter
                                    checked: appController.compactInterface
                                    onToggledByUser: function(checked) {
                                        appController.compactInterface = checked
                                    }
                                }
                            }
                        }

                        Rectangle {
                            width: 120
                            height: 78
                            radius: 20
                            color: Theme.cardBackgroundAlt
                            border.width: 1
                            border.color: Theme.outline

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 10

                                Image {
                                    source: Theme.darkMode
                                            ? "qrc:/CatClicker/branding/github-mark-dark.svg"
                                            : "qrc:/CatClicker/branding/github-mark-light.svg"
                                    sourceSize.width: 24
                                    sourceSize.height: 24
                                }

                                Text {
                                    text: "GitHub"
                                    color: Theme.textPrimary
                                    font.pixelSize: 16
                                    font.bold: true
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: appController.openProjectWebsite()
                            }
                        }

                        Rectangle {
                            width: 120
                            height: 78
                            radius: 20
                            color: Qt.rgba(Theme.primaryLavender.r, Theme.primaryLavender.g, Theme.primaryLavender.b, Theme.darkMode ? 0.18 : 0.14)
                            border.width: 1
                            border.color: Theme.outlineStrong

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                Text {
                                    text: "Settings"
                                    color: Theme.textPrimary
                                    font.pixelSize: 16
                                    font.bold: true
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: "\u2699"
                                    color: Theme.textPrimary
                                    font.pixelSize: 22
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: rootWindow.openSettings()
                            }
                        }
                    }
                }
            }

            InfoCard {
                Layout.fillWidth: true

                Text {
                    text: "Current macro"
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }

                Text {
                    width: parent.width
                    text: appController.macroName
                    color: Theme.textPrimary
                    font.pixelSize: 28
                    font.bold: true
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    text: appController.elapsedText + " • " + appController.eventCount + " events"
                    color: Theme.textMuted
                    font.pixelSize: 15
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                ActionButton {
                    Layout.fillWidth: true
                    text: appController.appState === "Recording" ? "\u25a0 STOP RECORDING" : "\u25cf RECORD"
                    accentColor: Theme.primaryPink
                    enabled: appController.appState !== "Playing" && appController.appState !== "Stopping"
                    onClicked: appController.startRecording(false)
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: "\u25b6 PLAY"
                    accentColor: Theme.primaryBlue
                    enabled: appController.appState !== "Recording"
                             && appController.appState !== "Playing"
                             && appController.appState !== "Stopping"
                    onClicked: appController.startPlayback()
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: "\u25a0 STOP"
                    accentColor: Theme.stopAccent
                    enabled: appController.appState === "Playing"
                    onClicked: appController.stop()
                }
            }

            InfoCard {
                Layout.fillWidth: true

                Text {
                    text: "Status"
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }

                Text {
                    width: parent.width
                    text: appController.statusText
                    color: Theme.textPrimary
                    font.pixelSize: 19
                    font.bold: true
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: appController.showDeveloperTools
                    text: appController.globalInputListenerText
                    color: Theme.textSecondary
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: appController.showDeveloperTools
                    text: appController.globalHotkeysText + " • " + appController.activeRecordingBackendText
                    color: Theme.textMuted
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: appController.showDeveloperTools
                    text: "Selected playback backend: " + (appController.selectedPlaybackBackend || "none")
                    color: Theme.textSecondary
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: appController.showDeveloperTools && appController.playbackBackendReason.length > 0
                    text: appController.playbackBackendReason
                    color: Theme.textMuted
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 20

                InfoCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop

                    Text {
                        text: "Shortcuts"
                        color: Theme.textSecondary
                        font.pixelSize: 14
                    }

                    ShortcutField {
                        id: recordShortcutField
                        label: "Record / Stop Recording"
                        value: appController.recordShortcut
                        onShortcutEdited: function(shortcut) {
                            appController.recordShortcut = shortcut
                        }
                    }

                    ShortcutField {
                        id: playShortcutField
                        label: "Play"
                        value: appController.playShortcut
                        onShortcutEdited: function(shortcut) {
                            appController.playShortcut = shortcut
                        }
                    }

                    ShortcutField {
                        id: stopShortcutField
                        label: "Stop Playback / Emergency Stop"
                        value: appController.stopShortcut
                        onShortcutEdited: function(shortcut) {
                            appController.stopShortcut = shortcut
                        }
                    }

                    Rectangle {
                        width: parent.width
                        implicitHeight: speedRow.implicitHeight + 28
                        radius: 16
                        color: Theme.cardBackgroundAlt
                        border.width: 1
                        border.color: Theme.outline

                        RowLayout {
                            id: speedRow
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 12

                            Text {
                                text: "Playback speed"
                                color: Theme.textSecondary
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            ComboBox {
                                id: speedCombo
                                model: ["0.25x", "0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x"]
                                implicitWidth: 132

                                Component.onCompleted: {
                                    const values = [0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
                                    const idx = values.indexOf(appController.playbackSpeed)
                                    currentIndex = idx >= 0 ? idx : 3
                                }

                                onActivated: {
                                    const values = [0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
                                    appController.playbackSpeed = values[currentIndex]
                                }

                                contentItem: Text {
                                    text: speedCombo.displayText
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 2
                                }

                                background: Rectangle {
                                    radius: 12
                                    color: Theme.panelBackground
                                    border.width: 1
                                    border.color: Theme.outlineStrong
                                }
                            }
                        }
                    }
                }

                InfoCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop

                    Text {
                        text: "Files"
                        color: Theme.textSecondary
                        font.pixelSize: 14
                    }

                    Text {
                        width: parent.width
                        text: appController.macroPath.length > 0 ? appController.macroPath : "No macro saved yet"
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        wrapMode: Text.WrapAnywhere
                    }

                    RowLayout {
                        width: parent.width
                        spacing: 12

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Save Macro"
                            accentColor: Theme.primaryBlue
                            onClicked: appController.saveMacro()
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Load Macro"
                            accentColor: Theme.primaryPink
                            onClicked: appController.loadMacro()
                        }
                    }
                }
            }

            Loader {
                Layout.fillWidth: true
                active: appController.showDeveloperTools
                sourceComponent: Component {
                    InfoCard {
                        Layout.fillWidth: true

                        RowLayout {
                            width: parent.width

                            Text {
                                text: "Diagnostics"
                                color: Theme.textSecondary
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            ActionButton {
                                text: "Copy Diagnostics"
                                accentColor: Theme.primaryBlue
                                implicitWidth: 190
                                onClicked: appController.copyDiagnostics()
                            }
                        }

                        Text {
                            width: parent.width
                            text: "This report uses a privacy allowlist. Review the preview before copying. It excludes recorded input, coordinates, device names, and personal paths."
                            color: Theme.textMuted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }

                        Rectangle {
                            width: parent.width
                            implicitHeight: 280
                            radius: 18
                            color: Theme.panelBackground
                            border.width: 1
                            border.color: Theme.outline

                            TextArea {
                                anchors.fill: parent
                                anchors.margins: 12
                                readOnly: true
                                text: appController.diagnosticsText
                                color: Theme.textPrimary
                                wrapMode: Text.WrapAnywhere
                                background: null
                                font.family: "monospace"
                            }
                        }
                    }
                }
            }

            Loader {
                Layout.fillWidth: true
                active: appController.showDeveloperTools
                sourceComponent: Component {
                    InfoCard {
                        Layout.fillWidth: true

                        Text {
                            text: "Developer playback tests"
                            color: Theme.textSecondary
                            font.pixelSize: 14
                        }

                        RowLayout {
                            width: parent.width
                            spacing: 12

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Generate Pointer Test Macro"
                                accentColor: Theme.primaryBlue
                                implicitWidth: 260
                                onClicked: appController.generatePlaybackTestMacro()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Generate Keyboard Test Macro"
                                accentColor: Theme.primaryPink
                                implicitWidth: 280
                                onClicked: appController.generateKeyboardTestMacro()
                            }
                        }

                        RowLayout {
                            width: parent.width
                            spacing: 12

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Generate Held Key Stop Test"
                                accentColor: Theme.primaryBlue
                                onClicked: appController.generateHeldKeyStopTestMacro()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Generate Held Mouse Completion Test"
                                accentColor: Theme.primaryBlue
                                onClicked: {
                                    heldMousePressCount = 0
                                    heldMouseReleaseCount = 0
                                    const center = heldMouseStopPad.mapToGlobal(heldMouseStopPad.width / 2, heldMouseStopPad.height / 2)
                                    appController.generateHeldMouseCompletionTestMacro(center.x, center.y)
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Generate Held Mouse Stop Test"
                                accentColor: Theme.stopAccent
                                onClicked: {
                                    heldMousePressCount = 0
                                    heldMouseReleaseCount = 0
                                    const center = heldMouseStopPad.mapToGlobal(heldMouseStopPad.width / 2, heldMouseStopPad.height / 2)
                                    appController.generateHeldMouseStopTestMacro(center.x, center.y)
                                }
                            }
                        }

                        TextField {
                            width: parent.width
                            placeholderText: "Type lowercase text here after the held key stop test"
                            selectByMouse: true
                        }

                        Rectangle {
                            width: parent.width
                            implicitHeight: 84
                            radius: 16
                            color: Theme.cardBackgroundAlt
                            border.width: 1
                            border.color: Theme.outline

                            Column {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 4

                                Text {
                                    text: "Virtual held keys: " + appController.virtualHeldKeyCount
                                    color: Theme.textPrimary
                                    font.pixelSize: 15
                                    font.bold: true
                                }

                                Text {
                                    text: "Virtual held buttons: " + appController.virtualHeldButtonCount
                                    color: Theme.textPrimary
                                    font.pixelSize: 15
                                    font.bold: true
                                }
                            }
                        }

                        Rectangle {
                            id: heldMouseStopPad
                            width: parent.width
                            implicitHeight: 170
                            radius: 20
                            color: appController.virtualHeldButtonCount > 0
                                   ? Qt.rgba(Theme.stopAccent.r, Theme.stopAccent.g, Theme.stopAccent.b, Theme.darkMode ? 0.28 : 0.22)
                                   : Theme.cardBackgroundAlt
                            border.width: 1
                            border.color: appController.virtualHeldButtonCount > 0 ? Theme.stopAccent : Theme.outlineStrong

                            Column {
                                anchors.centerIn: parent
                                spacing: 6

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Held mouse stop pad"
                                    color: Theme.textPrimary
                                    font.pixelSize: 18
                                    font.bold: true
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: appController.virtualHeldButtonCount > 0 ? "Backend state: pressed" : "Backend state: released"
                                    color: Theme.textSecondary
                                    font.pixelSize: 14
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Pad events: presses " + heldMousePressCount + "  releases " + heldMouseReleaseCount
                                    color: Theme.textSecondary
                                    font.pixelSize: 14
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onPressed: {
                                    heldMousePressCount += 1
                                }
                                onReleased: {
                                    heldMouseReleaseCount += 1
                                }
                                onCanceled: {}
                            }
                        }

                        ActionButton {
                            width: parent.width
                            text: "Generate Click Test Macro"
                            accentColor: Theme.stopAccent
                            onClicked: {
                                const center = clickTestPad.mapToGlobal(clickTestPad.width / 2, clickTestPad.height / 2)
                                clickTestCount = 0
                                appController.generateClickTestMacro(center.x, center.y)
                            }
                        }

                        ActionButton {
                            width: parent.width
                            text: "Generate Scroll Test Macro"
                            accentColor: Theme.primaryBlue
                            onClicked: {
                                scrollUpCount = 0
                                scrollDownCount = 0
                                const center = scrollTestPad.mapToGlobal(scrollTestPad.width / 2, scrollTestPad.height / 2)
                                appController.generateScrollTestMacro(center.x, center.y)
                            }
                        }

                        Rectangle {
                            id: scrollTestPad
                            width: parent.width
                            implicitHeight: 170
                            radius: 20
                            color: Theme.cardBackgroundAlt
                            border.width: 1
                            border.color: Theme.outlineStrong

                            Rectangle {
                                width: 110
                                height: 110
                                anchors.centerIn: parent
                                radius: 55
                                color: Qt.rgba(Theme.primaryPink.r, Theme.primaryPink.g, Theme.primaryPink.b, Theme.darkMode ? 0.16 : 0.12)
                                border.width: 1
                                border.color: Qt.tint(Theme.primaryPink, Theme.outlineStrong)
                            }

                            Column {
                                anchors.centerIn: parent
                                spacing: 6

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Scroll test pad"
                                    color: Theme.textPrimary
                                    font.pixelSize: 18
                                    font.bold: true
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Wheel up received: " + scrollUpCount
                                    color: Theme.textSecondary
                                    font.pixelSize: 14
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Wheel down received: " + scrollDownCount
                                    color: Theme.textSecondary
                                    font.pixelSize: 14
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                                onWheel: function(wheel) {
                                    if (wheel.angleDelta.y > 0) {
                                        scrollUpCount += 1
                                    } else if (wheel.angleDelta.y < 0) {
                                        scrollDownCount += 1
                                    }
                                    wheel.accepted = true
                                }
                            }
                        }

                        Rectangle {
                            id: clickTestPad
                            width: parent.width
                            implicitHeight: 170
                            radius: 20
                            color: Theme.cardBackgroundAlt
                            border.width: 1
                            border.color: Theme.outlineStrong

                            Rectangle {
                                width: 96
                                height: 96
                                anchors.centerIn: parent
                                radius: 48
                                color: Qt.rgba(Theme.primaryBlue.r, Theme.primaryBlue.g, Theme.primaryBlue.b, Theme.darkMode ? 0.18 : 0.12)
                                border.width: 1
                                border.color: Qt.tint(Theme.primaryBlue, Theme.outlineStrong)
                            }

                            Column {
                                anchors.centerIn: parent
                                spacing: 6

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Click test pad"
                                    color: Theme.textPrimary
                                    font.pixelSize: 18
                                    font.bold: true
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Clicks received: " + clickTestCount
                                    color: Theme.textSecondary
                                    font.pixelSize: 14
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: clickTestCount += 1
                            }
                        }
                    }
                }
            }

            Loader {
                Layout.fillWidth: true
                active: appController.showDeveloperTools
                sourceComponent: Component {
                    InfoCard {
                        Layout.fillWidth: true

                        Text {
                            text: "Pointer debug view"
                            color: Theme.textSecondary
                            font.pixelSize: 14
                        }

                        Rectangle {
                            width: parent.width
                            implicitHeight: 180
                            radius: 18
                            color: Theme.panelBackground
                            border.width: 1
                            border.color: Theme.outline

                            TextArea {
                                anchors.fill: parent
                                anchors.margins: 12
                                readOnly: true
                                text: appController.macroDebugDump()
                                color: Theme.textPrimary
                                wrapMode: Text.WrapAnywhere
                                background: null
                                font.family: "monospace"
                            }
                        }
                    }
                }
            }
        }
    }
}
