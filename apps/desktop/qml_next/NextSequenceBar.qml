import QtQuick
import QtQuick.Controls.Basic

// One-letter residue strip of the active object. Clicking a residue selects it (`select sele, ...`).
Rectangle {
    id: bar
    property var viewport
    property var items: []
    signal command(string text)
    height: 34
    color: NextTheme.panel
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: NextTheme.line }

    function refresh() { items = viewport ? viewport.sequenceItems() : [] }
    onVisibleChanged: if (visible) refresh()
    Connections {
        target: bar.viewport
        function onObjectsChanged() { if (bar.visible) bar.refresh() }
    }

    ListView {
        id: strip
        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
        orientation: ListView.Horizontal
        clip: true
        model: bar.items
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar { height: 6; policy: ScrollBar.AsNeeded }
        delegate: Rectangle {
            required property var modelData
            required property int index
            width: 14; height: strip.height
            color: hover.hovered ? NextTheme.accentSoft : "transparent"
            HoverHandler { id: hover }
            Text {
                anchors.centerIn: parent
                text: modelData.letter
                font.family: NextTheme.mono
                font.pixelSize: 12
                color: NextTheme.text
            }
            ToolTip.visible: hover.hovered
            ToolTip.delay: 300
            ToolTip.text: modelData.name + " " + modelData.resi + (modelData.chain ? " (" + modelData.chain + ")" : "")
            TapHandler {
                onTapped: {
                    const chain = modelData.chain.length > 0 ? "chain " + modelData.chain + " and " : ""
                    bar.command("select sele, " + chain + "resi " + modelData.resi)
                }
            }
        }
    }
    Text {
        anchors.centerIn: parent
        visible: bar.items.length === 0
        text: qsTr("no residues")
        color: NextTheme.muted
        font.pixelSize: NextTheme.fontSize
    }
}
