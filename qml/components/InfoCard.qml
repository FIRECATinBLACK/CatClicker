import QtQuick
import CatClicker

Rectangle {
    id: root
    default property alias content: container.data
    implicitHeight: container.implicitHeight + 44

    radius: 24
    gradient: Gradient {
        GradientStop { position: 0.0; color: Qt.tint(Theme.cardBackground, Theme.darkMode ? "#16ffffff" : "#3affffff") }
        GradientStop { position: 1.0; color: Theme.cardBackground }
    }
    border.width: 1
    border.color: Theme.outline

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.leftMargin: 4
        anchors.rightMargin: -4
        anchors.bottomMargin: -8
        radius: root.radius + 2
        color: Theme.shadowColor
        z: -1
    }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, Theme.darkMode ? 0.06 : 0.3)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 64
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.darkMode ? "#18ffffff" : "#6affffff" }
            GradientStop { position: 0.35; color: Qt.rgba(Theme.primaryLavender.r, Theme.primaryLavender.g, Theme.primaryLavender.b, Theme.darkMode ? 0.08 : 0.06) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Column {
        id: container
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 22
        spacing: 14
    }
}
