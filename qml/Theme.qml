pragma Singleton
import QtQuick

QtObject {
    property bool darkMode: true

    readonly property color catDark: "#3E3376"
    readonly property color catDarkFoundation: "#362D6C"
    readonly property color pinkHighlight: "#FDAACB"
    readonly property color pinkMain: "#FC8BB2"
    readonly property color pinkShadow: "#DD87BB"
    readonly property color blueHighlight: "#84A1EF"
    readonly property color blueMain: "#7289EC"
    readonly property color blueShadow: "#878ADF"
    readonly property color backgroundAccentPink: darkMode ? pinkShadow : pinkHighlight
    readonly property color backgroundAccentBlue: darkMode ? blueShadow : blueHighlight

    readonly property color windowBackground: darkMode ? "#171924" : "#f8f2fb"
    readonly property color backgroundStart: darkMode ? catDarkFoundation : "#fbe7ef"
    readonly property color backgroundMid: darkMode ? "#292441" : "#ece2f3"
    readonly property color backgroundEnd: darkMode ? "#202b45" : "#e4edff"
    readonly property color backgroundGlowPink: backgroundAccentPink
    readonly property color backgroundGlowBlue: backgroundAccentBlue
    readonly property color backgroundGlowLavender: darkMode ? "#a88fce" : "#c7b1d4"

    readonly property color cardBackground: darkMode ? catDarkFoundation : "#fffafd"
    readonly property color cardBackgroundAlt: darkMode ? "#30285D" : "#f8f1fb"
    readonly property color cardHighlight: darkMode ? "#493D7F" : "#ffffff"
    readonly property color panelBackground: darkMode ? "#211D38" : "#fffdfd"

    readonly property color primaryPink: pinkMain
    readonly property color primaryBlue: blueMain
    readonly property color primaryLavender: darkMode ? "#C7B1D4" : "#b397ca"
    readonly property color stopAccent: darkMode ? "#dca6c4" : "#cf8fae"

    readonly property color textPrimary: darkMode ? "#f6f1ff" : catDark
    readonly property color textSecondary: darkMode ? "#c8bfd9" : catDark
    readonly property color textMuted: darkMode ? "#9b93b2" : catDark

    readonly property color success: darkMode ? "#7fcaa8" : "#5e9d83"
    readonly property color warning: darkMode ? "#ddb2a0" : "#c28672"
    readonly property color danger: darkMode ? "#d98fa7" : "#c56b88"

    readonly property color outline: darkMode ? "#454b64" : "#ddcfe4"
    readonly property color outlineStrong: darkMode ? "#666d88" : "#cdb7da"
    readonly property color shadowColor: darkMode ? "#50071018" : "#221b2d30"

    readonly property color buttonText: darkMode ? "#fff7fd" : catDark
    readonly property color buttonDisabled: darkMode ? "#30285D" : "#eadfea"
}
