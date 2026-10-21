import QtQuick
import QtQuick.Controls.Basic

// Flat button used by the new shell. `checked` shows the active state (toggle buttons).
AbstractButton {
    id: control
    property bool outlined: true
    property int pixelSize: NextTheme.fontSize
    property bool mono: false
    // Drawn over the dark 3D viewport: light text and a translucent fill regardless of the theme.
    property bool hud: false
    implicitHeight: 24
    implicitWidth: Math.max(24, contentItem.implicitWidth + 14)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    // Screen readers announce the visible text unless a panel gives a better name (icon-only buttons do).
    Accessible.name: text
    contentItem: Text {
        text: control.text
        font.pixelSize: control.pixelSize
        font.family: control.mono ? NextTheme.mono : Qt.application.font.family
        font.weight: control.checked ? Font.DemiBold : Font.Normal
        color: control.hud ? NextTheme.viewText : (control.enabled ? NextTheme.text : NextTheme.muted)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: NextTheme.radius
        color: control.hud ? (control.checked ? Qt.rgba(0.94, 0.64, 0.23, 0.25) : Qt.rgba(1, 1, 1, control.hovered ? 0.14 : 0.07))
             : control.checked || control.down ? NextTheme.activeSoft
             : control.hovered ? NextTheme.chrome : "transparent"
        border.width: control.outlined || control.checked || control.activeFocus ? 1 : 0
        border.color: control.hud ? (control.checked ? NextTheme.accent : Qt.rgba(1, 1, 1, 0.22))
                    : control.checked || control.activeFocus ? NextTheme.active : NextTheme.line
    }
    ToolTip.visible: hovered && ToolTip.text.length > 0
    ToolTip.delay: 600
}
