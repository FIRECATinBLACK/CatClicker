import QtQuick
import QtQuick.Controls
import CatClicker

Button {
    id: root
    required property string iconSource
    required property string toolTipText
    property color accentColor: Theme.primaryBlue

    implicitWidth: 44
    implicitHeight: 44
    padding: 10
    Accessible.name: toolTipText

    ToolTip {
        visible: root.hovered
        delay: 450
        timeout: 2500
        leftPadding: 10
        rightPadding: 10
        topPadding: 6
        bottomPadding: 6

        contentItem: Text {
            text: root.toolTipText
            color: Theme.textPrimary
            font.pixelSize: 12
        }

        background: Rectangle {
            radius: 9
            color: Theme.cardBackground
            border.width: 1
            border.color: Theme.outlineStrong
        }
    }

    background: Rectangle {
        radius: 14
        color: root.down ? Qt.darker(root.accentColor, 1.15)
                         : root.hovered ? Qt.lighter(root.accentColor, 1.08)
                                        : root.accentColor
        border.width: 1
        border.color: root.enabled ? Theme.outlineStrong : Theme.outline
        opacity: root.enabled ? 1.0 : 0.45
    }

    contentItem: Image {
        source: root.iconSource
        sourceSize.width: 22
        sourceSize.height: 22
        fillMode: Image.PreserveAspectFit
        opacity: root.enabled ? 1.0 : 0.65
    }
}
