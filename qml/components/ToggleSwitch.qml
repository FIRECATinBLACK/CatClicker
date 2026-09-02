import QtQuick
import CatClicker

Item {
    id: root
    property bool checked: false
    signal toggledByUser(bool checked)

    implicitWidth: 58
    implicitHeight: 34
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: !root.enabled ? Theme.buttonDisabled :
               root.checked ? Qt.rgba(Theme.primaryBlue.r, Theme.primaryBlue.g, Theme.primaryBlue.b, Theme.darkMode ? 0.64 : 0.78)
                            : Qt.rgba(Theme.primaryLavender.r, Theme.primaryLavender.g, Theme.primaryLavender.b, Theme.darkMode ? 0.22 : 0.30)
        border.width: 1
        border.color: !root.enabled ? Theme.outline :
                      root.checked ? Qt.tint(Theme.primaryBlue, Theme.outlineStrong) : Theme.outlineStrong
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height * 0.52
        radius: height / 2
        color: Qt.rgba(1, 1, 1, Theme.darkMode ? 0.12 : 0.24)
    }

    Rectangle {
        width: 26
        height: 26
        radius: 13
        x: root.checked ? root.width - width - 4 : 4
        y: 4
        color: root.enabled ? Theme.cardHighlight : Theme.cardBackgroundAlt
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, Theme.darkMode ? 0.10 : 0.22)
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.toggledByUser(!root.checked)
    }
}
