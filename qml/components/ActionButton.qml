import QtQuick
import QtQuick.Controls
import CatClicker

Button {
    id: root
    property color accentColor: Theme.primaryBlue
    readonly property bool usesPinkBrandAccent: accentColor === Theme.primaryPink
    readonly property bool usesBlueBrandAccent: accentColor === Theme.primaryBlue
    readonly property bool usesBrandAccent: usesPinkBrandAccent || usesBlueBrandAccent
    readonly property color accentHighlight: usesPinkBrandAccent ? Theme.pinkHighlight
                                                                  : usesBlueBrandAccent ? Theme.blueHighlight
                                                                                        : Qt.lighter(accentColor, 1.12)
    readonly property color accentShadow: usesPinkBrandAccent ? Theme.pinkShadow
                                                               : usesBlueBrandAccent ? Theme.blueShadow
                                                                                     : Qt.darker(accentColor, 1.12)

    implicitWidth: 150
    implicitHeight: 56
    leftPadding: 20
    rightPadding: 20

    background: Rectangle {
        radius: 20
        border.width: 1
        border.color: root.enabled ? Qt.tint(root.accentColor, Theme.outlineStrong) : Theme.outline
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.enabled ? root.accentHighlight : Theme.buttonDisabled }
            GradientStop { position: 0.55; color: root.enabled ? root.accentColor : Theme.buttonDisabled }
            GradientStop { position: 1.0; color: root.enabled ? root.accentShadow : Theme.buttonDisabled }
        }
        opacity: root.enabled ? 1.0 : 0.6

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height * 0.5
            radius: parent.radius
            color: root.usesBrandAccent ? "transparent" : (Theme.darkMode ? "#24ffffff" : "#42ffffff")
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1
            border.color: root.enabled ? Qt.rgba(1, 1, 1, Theme.darkMode ? 0.08 : 0.22) : "transparent"
        }
    }

    contentItem: Text {
        text: root.text
        color: root.enabled ? Theme.buttonText : Theme.textMuted
        font.pixelSize: 16
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        wrapMode: Text.Wrap
    }
}
