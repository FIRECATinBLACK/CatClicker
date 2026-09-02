import QtQuick
import CatClicker

Rectangle {
    id: root
    property string text: ""
    property color accentColor: Theme.primaryBlue

    radius: height / 2
    gradient: Gradient {
        GradientStop { position: 0.0; color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, Theme.darkMode ? 0.26 : 0.18) }
        GradientStop { position: 1.0; color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, Theme.darkMode ? 0.12 : 0.08) }
    }
    border.width: 1
    border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, Theme.darkMode ? 0.56 : 0.38)
    implicitHeight: 36
    implicitWidth: label.implicitWidth + 32

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, Theme.darkMode ? 0.08 : 0.2)
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: Theme.textPrimary
        font.pixelSize: 13
        font.bold: true
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }
}
