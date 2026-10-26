import QtQuick
import QtQuick.Controls.Basic

// Popup menu built from a plain array: [{text, hint, command, action, form, separator, enabled, submenu: [entries]}].
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
    // Items that open a nested menu (made by addMenu) take this look; plain items use `itemComponent` below.
    delegate: MenuItem {
        id: nestedRow
        implicitHeight: 28
        contentItem: Row {
            spacing: 16
            Text {
                text: nestedRow.text
                color: NextTheme.text
                font.pixelSize: NextTheme.fontSize
                width: Math.max(80, nestedRow.width - 56)
                elide: Text.ElideRight
                anchors.verticalCenter: parent.verticalCenter
            }
            Text { text: "▸"; color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; anchors.verticalCenter: parent.verticalCenter }
        }
        arrow: Item { }
        background: Rectangle { radius: 4; color: nestedRow.highlighted ? NextTheme.activeSoft : "transparent" }
    }
    // Items are rebuilt whenever the entries change.
    function rebuild() {
        while (menu.count > 0)
            menu.removeItem(menu.itemAt(0))
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            if (entry.separator) {
                menu.addItem(separatorComponent.createObject(null))
            } else if (entry.submenu) {
                // A nested menu: an item that opens the entries listed under `submenu`.
                if (menu.subMenuComponent === null) menu.subMenuComponent = Qt.createComponent("NextMenu.qml")
                const nested = menu.subMenuComponent.createObject(menu, {title: entry.text, entries: entry.submenu})
                nested.picked.connect(function(picked) { menu.picked(picked) })
                menu.addMenu(nested)
            } else {
                const item = itemComponent.createObject(null, {entry: entry})
                menu.addItem(item)
            }
        }
    }
    onEntriesChanged: rebuild()
    Component.onCompleted: rebuild()

    // A QML file cannot name its own type, so nested menus are created from the file by URL.
    property var subMenuComponent: null
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
