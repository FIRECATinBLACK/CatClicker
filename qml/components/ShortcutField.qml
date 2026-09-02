import QtQuick
import QtQuick.Layouts
import CatClicker

Rectangle {
    id: root
    property string label: ""
    property string value: ""
    property alias listening: input.activeFocus
    signal shortcutEdited(string shortcut)

    radius: 16
    color: Theme.cardBackgroundAlt
    border.width: 1
    border.color: Theme.outline
    width: parent ? parent.width : implicitWidth
    implicitHeight: contentLayout.implicitHeight + 32

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: 1
        border.color: input.activeFocus ? Qt.tint(Theme.primaryBlue, Theme.primaryPink) : Qt.rgba(1, 1, 1, Theme.darkMode ? 0.04 : 0.18)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 24
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: input.activeFocus ? Qt.rgba(Theme.primaryBlue.r, Theme.primaryBlue.g, Theme.primaryBlue.b, Theme.darkMode ? 0.2 : 0.14) : (Theme.darkMode ? "#10ffffff" : "#30ffffff") }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    function keyName(key) {
        const functionKeys = {
            [Qt.Key_F1]: "F1", [Qt.Key_F2]: "F2", [Qt.Key_F3]: "F3", [Qt.Key_F4]: "F4",
            [Qt.Key_F5]: "F5", [Qt.Key_F6]: "F6", [Qt.Key_F7]: "F7", [Qt.Key_F8]: "F8",
            [Qt.Key_F9]: "F9", [Qt.Key_F10]: "F10", [Qt.Key_F11]: "F11", [Qt.Key_F12]: "F12"
        }

        if (functionKeys[key] !== undefined) {
            return functionKeys[key]
        }

        if (key >= Qt.Key_A && key <= Qt.Key_Z) {
            return String.fromCharCode(key)
        }

        if (key >= Qt.Key_0 && key <= Qt.Key_9) {
            return String.fromCharCode(key)
        }

        const namedKeys = {
            [Qt.Key_Tab]: "Tab",
            [Qt.Key_Space]: "Space",
            [Qt.Key_Escape]: "Esc",
            [Qt.Key_Return]: "Enter",
            [Qt.Key_Enter]: "Enter",
            [Qt.Key_Backspace]: "Backspace",
            [Qt.Key_Delete]: "Delete",
            [Qt.Key_Insert]: "Insert",
            [Qt.Key_Home]: "Home",
            [Qt.Key_End]: "End",
            [Qt.Key_PageUp]: "PageUp",
            [Qt.Key_PageDown]: "PageDown",
            [Qt.Key_Left]: "Left",
            [Qt.Key_Right]: "Right",
            [Qt.Key_Up]: "Up",
            [Qt.Key_Down]: "Down"
        }

        return namedKeys[key] !== undefined ? namedKeys[key] : ""
    }

    function isModifierKey(key) {
        return key === Qt.Key_Control
            || key === Qt.Key_Shift
            || key === Qt.Key_Alt
            || key === Qt.Key_Meta
            || key === Qt.Key_AltGr
    }

    RowLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        Column {
            Layout.alignment: Qt.AlignVCenter
            spacing: 6

            Text {
                text: root.label
                color: Theme.textSecondary
                font.pixelSize: 13
                width: Math.max(0, root.width - 32)
                wrapMode: Text.Wrap
            }

            Text {
                text: input.activeFocus ? "Listening for shortcut..." : (root.value || "Unset")
                color: Theme.textPrimary
                font.pixelSize: 17
                font.bold: true
                width: Math.max(0, root.width - 32)
                wrapMode: Text.Wrap
            }
        }

        Item { Layout.fillWidth: true }
    }

    TextInput {
        id: input
        anchors.fill: parent
        opacity: 0.0
        focus: false
        onActiveFocusChanged: if (activeFocus) selectAll()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_unknown) {
                return
            }

            if (root.isModifierKey(event.key)) {
                event.accepted = true
                return
            }

            const parts = []
            if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
            if (event.modifiers & Qt.AltModifier) parts.push("Alt")
            if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
            if (event.modifiers & Qt.MetaModifier) parts.push("Super")

            const keyText = root.keyName(event.key)

            if (keyText.length > 0) {
                parts.push(keyText)
            }

            const shortcut = parts.join("+")
            if (shortcut.length > 0) {
                root.shortcutEdited(shortcut)
            }
            focus = false
            event.accepted = true
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: input.forceActiveFocus()
    }
}
