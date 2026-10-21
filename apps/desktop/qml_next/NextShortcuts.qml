import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Keyboard shortcuts and the mouse mapping on one page (Help ▸ Keyboard shortcuts…, F1).
Popup {
    id: panel
    objectName: "shortcutsOverlay"

    readonly property var groups: [
        {title: qsTr("Anywhere"), items: [
            [qsTr("Open structure"), "Ctrl+O"], [qsTr("All commands"), "Ctrl+K"], [qsTr("Command line"), "Ctrl+L"],
            [qsTr("Undo / Redo"), "Ctrl+Z / Ctrl+Shift+Z"], [qsTr("Output window"), "Ctrl+`"], [qsTr("Sequence viewer"), "Ctrl+Shift+S"],
            [qsTr("This list"), "F1"], [qsTr("Quit"), "Ctrl+Q"]]},
        {title: qsTr("Panels"), items: [
            [qsTr("Inspector"), "Ctrl+I"], [qsTr("Movie editor"), "Ctrl+E"], [qsTr("Results"), "Ctrl+R"], [qsTr("Select by expression"), "Ctrl+Shift+F"],
            [qsTr("Edit atoms and bonds"), "Ctrl+Shift+E"], [qsTr("Camera"), "Ctrl+Shift+J"], [qsTr("Views and scenes"), "Ctrl+Shift+N"],
            [qsTr("Volumes"), "Ctrl+Shift+U"]]},
        {title: qsTr("Viewport (click it or press Tab to focus it)"), items: [
            [qsTr("Rotate"), "← → ↑ ↓"], [qsTr("Move sideways"), "Shift + ← → ↑ ↓"], [qsTr("Zoom"), "+  /  −"],
            [qsTr("Move the clipping planes"), "Page Up / Page Down"], [qsTr("Reset the view"), "Home"]]},
        {title: qsTr("Object list (Tab to focus it)"), items: [
            [qsTr("Move"), "↑ ↓"], [qsTr("Activate object / open or close group"), "Enter"], [qsTr("Show or hide"), "Space"],
            [qsTr("Rename"), "F2"], [qsTr("Delete"), "Delete"], [qsTr("Actions menu"), "Menu / Shift+F10"]]},
        {title: qsTr("Mouse in the viewport"), items: [
            [qsTr("Rotate"), qsTr("left drag")], [qsTr("Move sideways"), qsTr("middle drag, or Shift + left drag")], [qsTr("Zoom"), qsTr("right drag, or Ctrl + wheel")],
            [qsTr("Move the clipping planes"), qsTr("wheel")], [qsTr("Select / pick"), qsTr("click (Ctrl-click adds)")], [qsTr("Reset the view"), qsTr("double-click")]]}
    ]

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(620, Overlay.overlay ? Overlay.overlay.width - 40 : 620)
    height: Math.min(600, Overlay.overlay ? Overlay.overlay.height - 60 : 600)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function openPanel() { open() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("KEYBOARD SHORTCUTS"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 6
            clip: true
            model: panel.groups
            spacing: 6
            ScrollBar.vertical: ScrollBar { }
            delegate: Column {
                id: group
                required property var modelData
                width: list.width
                Text { x: 6; text: group.modelData.title; color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold; bottomPadding: 3 }
                Repeater {
                    model: group.modelData.items
                    delegate: Item {
                        required property var modelData
                        width: group.width
                        height: 22
                        Text { x: 14; width: parent.width * 0.55; anchors.verticalCenter: parent.verticalCenter; text: modelData[0]; color: NextTheme.muted; font.pixelSize: NextTheme.fontSize; elide: Text.ElideRight }
                        Text { x: parent.width * 0.58; anchors.verticalCenter: parent.verticalCenter; text: modelData[1]; color: NextTheme.text; font.family: NextTheme.mono; font.pixelSize: 12 }
                    }
                }
            }
        }
    }
}
