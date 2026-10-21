pragma Singleton
import QtQuick

// Design tokens of the new Desktop shell (SEQ-330). Colors follow docs/UI_NEXT.md; nothing here is taken from
// another molecular viewer. `mode` is "system", "light" or "dark".
QtObject {
    id: theme
    property string mode: "system"
    readonly property bool dark: mode === "dark"
        || (mode === "system" && Qt.styleHints.colorScheme === Qt.ColorScheme.Dark)

    readonly property color page: dark ? "#0d1015" : "#e9ecf1"
    readonly property color chrome: dark ? "#1a2029" : "#f4f6f9"
    readonly property color panel: dark ? "#202833" : "#ffffff"
    readonly property color line: dark ? "#323d4b" : "#cfd5de"
    readonly property color text: dark ? "#e4e9f1" : "#1d2430"
    readonly property color muted: dark ? "#97a3b5" : "#5d6878"
    readonly property color view: dark ? "#080a0e" : "#12161d"
    readonly property color viewText: "#b7c3d6"
    readonly property color accent: dark ? "#f0a43a" : "#c8780a"
    readonly property color accentSoft: dark ? "#4a3615" : "#f6e4c8"
    readonly property color active: dark ? "#4cc0cf" : "#1b6f7a"
    readonly property color activeSoft: dark ? "#173a40" : "#d8eef0"
    readonly property color ok: dark ? "#5cc58a" : "#2f7d4f"
    readonly property color error: dark ? "#ee7b72" : "#b3322b"

    readonly property string mono: Qt.platform.os === "osx" ? "Menlo"
        : (Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono")
    readonly property int fontSize: 13
    readonly property int smallFont: 11
    readonly property int radius: 5
    readonly property int rowHeight: 28

    // The text of the closest preceding sibling label (Caption/Text): what a screen reader should call a control
    // that sits right after it. Empty when there is none.
    function captionBefore(item) {
        const siblings = item && item.parent ? item.parent.children : []
        let found = ""
        for (let i = 0; i < siblings.length; ++i) {
            const sibling = siblings[i]
            if (sibling === item) break
            // a label is text that is not itself a control (controls expose one of these properties)
            if (typeof sibling.text === "string" && sibling.text.length > 0 && sibling.checkable === undefined
                && sibling.placeholderText === undefined && sibling.currentIndex === undefined && sibling.down === undefined)
                found = sibling.text
        }
        return found
    }
}
