import QtQuick
import QtQuick.Controls.Basic

// Popup menu built from a plain array: [{text, hint, command, action, form, separator, enabled}].
// Picking an item emits `picked(entry)`; the shell decides what to run.
Menu {
    id: menu
    property var entries: []
    signal picked(var entry)
    padding: 4
    implicitWidth: 240
    background: Rectangle {
        color: NextTheme.panel
        border.color: NextTheme.line
        radius: 8
    }
    // Items are rebuilt whenever the entries change.
    function rebuild() {
        while (menu.count > 0)
            menu.removeItem(menu.itemAt(0))
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            if (entry.separator) {
                menu.addItem(separatorComponent.createObject(null))
            } else {
                const item = itemComponent.createObject(null, {entry: entry})
                menu.addItem(item)
            }
        }
    }
    onEntriesChanged: rebuild()
    Component.onCompleted: rebuild()

    Component {
        id: separatorComponent
        MenuSeparator {
            padding: 2
            contentItem: Rectangle { implicitHeight: 1; color: NextTheme.line }
        }
    }
    Component {
        id: itemComponent
        MenuItem {
            id: row
            property var entry
            text: entry ? entry.text : ""
            enabled: entry ? entry.enabled !== false : true
            implicitHeight: 28
            contentItem: Row {
                spacing: 16
                Text {
                    text: row.text
                    color: row.enabled ? NextTheme.text : NextTheme.muted
                    font.pixelSize: NextTheme.fontSize
                    width: Math.max(80, row.width - hint.width - 40)
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    id: hint
                    text: row.entry && row.entry.hint ? row.entry.hint : ""
                    color: NextTheme.muted
                    font.pixelSize: NextTheme.smallFont
                    font.family: NextTheme.mono
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            background: Rectangle {
                radius: 4
                color: row.highlighted && row.enabled ? NextTheme.activeSoft : "transparent"
            }
            onTriggered: menu.picked(entry)
        }
    }
}
