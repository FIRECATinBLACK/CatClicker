pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import CatClicker

ComboBox {
    id: control

    implicitWidth: 132
    implicitHeight: 42
    leftPadding: 14
    rightPadding: 38

    delegate: ItemDelegate {
        id: optionDelegate
        required property string modelData
        required property int index
        width: control.popup.width - 12
        height: 38
        leftPadding: 12
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: optionDelegate.modelData
            color: optionDelegate.highlighted || control.currentIndex === optionDelegate.index
                   ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: 14
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: 10
            color: optionDelegate.highlighted
                   ? Qt.rgba(Theme.primaryBlue.r, Theme.primaryBlue.g, Theme.primaryBlue.b,
                             Theme.darkMode ? 0.34 : 0.24)
                   : control.currentIndex === optionDelegate.index ? Theme.cardBackgroundAlt : "transparent"
            border.width: control.currentIndex === optionDelegate.index ? 1 : 0
            border.color: Theme.outline
        }
    }

    indicator: Text {
        x: control.width - width - 12
        y: (control.height - height) / 2
        text: "\u2304"
        color: Theme.textPrimary
        font.pixelSize: 18
        font.bold: true
    }

    contentItem: Text {
        leftPadding: 0
        rightPadding: 0
        text: control.displayText
        color: control.enabled ? Theme.textPrimary : Theme.textMuted
        font.pixelSize: 14
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 13
        color: control.down ? Theme.cardHighlight : Theme.panelBackground
        border.width: 1
        border.color: control.activeFocus ? Theme.primaryBlue : Theme.outlineStrong
    }

    popup: Popup {
        y: control.height + 5
        width: control.width
        implicitHeight: contentItem.implicitHeight + 12
        padding: 6

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            spacing: 2
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: 14
            color: Theme.cardBackground
            border.width: 1
            border.color: Theme.outlineStrong
        }
    }
}
