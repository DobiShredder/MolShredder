import QtQuick
import QtQuick.Controls.Basic

// Sequence viewer: one row per molecular object, the chains of an object one after another in its row
// (separated by a gap, each chain start marked with its id), with a residue-number ruler. Clicking a residue
// selects it into that object's `sele` (the object becomes active); dragging selects a range, Shift-click
// extends from the last click, Ctrl/Cmd-click adds or removes. Selected residues are highlighted here and
// drawn as squares over the atoms in the viewport.
Rectangle {
    id: bar
    objectName: "sequenceBar"
    property var viewport
    property var rows: []
    signal command(string text)
    // Anchor of Shift-click, the drag in progress and the residue under the pointer: row and index in the row.
    property int anchorRow: -1
    property int anchorIndex: -1
    property int dragRow: -1
    property int dragStart: -1
    property int dragEnd: -1
    property int hoverRow: -1
    property int hoverIndex: -1
    property real scrollX: 0
    readonly property int cell: 14
    readonly property int gapCells: 3
    readonly property int rowHeight: 38
    readonly property int labelWidth: 184
    readonly property int visibleRows: Math.max(1, Math.min(rows.length, 4))
    readonly property real stripWidth: Math.max(1, width - labelWidth - 8)
    readonly property real maxContent: {
        let widest = 0
        for (const row of rows) widest = Math.max(widest, rowWidth(row))
        return widest
    }
    // The height the bar wants while it is shown; the shell sets `height` from it.
    readonly property int contentHeight: rows.length === 0 ? 34 : visibleRows * rowHeight + 8
    color: NextTheme.panel
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Sequence")
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: NextTheme.line }

    function refresh() { rows = viewport ? viewport.sequenceRows() : [] }
    onVisibleChanged: if (visible) refresh()
    Connections {
        target: bar.viewport
        function onObjectsChanged() { if (bar.visible) bar.refresh() }
        function onSelectionMarkersChanged() { if (bar.visible && bar.selectionStale()) bar.refresh() }
    }
    // The selection changed without the objects changing (a click in the viewport): reload the highlight.
    property real lastRevision: -1
    function selectionStale() {
        const revision = viewport ? viewport.selectionRevision() : 0
        const stale = revision !== lastRevision
        lastRevision = revision
        return stale
    }

    function rowWidth(row) {
        let width = row.items.length * cell
        for (let i = 1; i < row.items.length; ++i) if (row.items[i].chain !== row.items[i - 1].chain) width += gapCells * cell
        return width
    }
    function selectedIn(row) {
        let n = 0
        for (const item of row.items) if (item.selected) n += 1
        return n
    }
    // "/object/segment/chain/residue", the way PyMOL names a residue.
    function identifier(row, item) {
        return "/" + row.object + "/" + item.segment + "/" + item.chain + "/" + item.resi
    }

    // A selection expression for the residues of `row` whose flag is set: one clause per run of neighbors in one chain.
    function expressionFor(row, flags) {
        const items = row.items
        const clauses = []
        let i = 0
        while (i < flags.length) {
            if (!flags[i]) { i += 1; continue }
            let last = i
            while (last + 1 < flags.length && flags[last + 1] && items[last + 1].chain === items[i].chain) last += 1
            const chain = items[i].chain.length > 0 ? "chain " + items[i].chain + " and " : ""
            let simple = true
            for (let j = i; j <= last; ++j) if (!/^-?[0-9]+$/.test(items[j].resi)) simple = false
            let resi
            if (simple) {
                resi = items[i].resi === items[last].resi ? items[i].resi : items[i].resi + "-" + items[last].resi
            } else {
                const list = []
                for (let j = i; j <= last; ++j) list.push(items[j].resi)
                resi = list.join("+")
            }
            clauses.push("(" + chain + "resi " + resi + ")")
            i = last + 1
        }
        return clauses.join(" or ")
    }
    // mode "replace": exactly the range. mode "add" (Ctrl/Cmd): adds the range, or removes it when every residue
    // of it is already selected. The new selection is written out from the residue flags, so `sele` never refers
    // to itself. The row's object is activated first (selections belong to the active object).
    function selectRange(rowIndex, first, last, mode) {
        if (rowIndex < 0 || rowIndex >= rows.length) return
        const row = rows[rowIndex]
        const items = row.items
        if (items.length === 0) return
        first = Math.max(0, Math.min(first, items.length - 1))
        last = Math.max(0, Math.min(last, items.length - 1))
        if (first > last) { const t = first; first = last; last = t }
        const flags = []
        for (let i = 0; i < items.length; ++i) flags.push(mode === "add" ? items[i].selected === true : false)
        let allSelected = true
        for (let i = first; i <= last; ++i) if (!items[i].selected) allSelected = false
        const remove = mode === "add" && allSelected
        for (let i = first; i <= last; ++i) flags[i] = !remove
        const expression = expressionFor(row, flags)
        if (!row.active) command("object activate --id " + row.objectId)
        command(expression.length > 0 ? "select sele, " + expression : "delete sele")
        anchorRow = rowIndex
        anchorIndex = first
    }
    // `ui:seqselect ROW FIRST LAST [add]` (zero-based row and residue indices) for the smoke tests.
    function selectByHook(text) {
        const parts = text.trim().split(/\s+/)
        selectRange(parseInt(parts[0], 10), parseInt(parts[1], 10), parseInt(parts.length > 2 ? parts[2] : parts[1], 10),
                    parts[3] === "add" ? "add" : "replace")
    }

    Flickable {
        id: rowsView
        anchors { fill: parent; bottomMargin: 6 }
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: bar.rows.length > 4 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        Column {
            id: column
            width: rowsView.width
            Repeater {
                model: bar.rows
                delegate: Item {
                    id: rowItem
                    required property var modelData
                    required property int index
                    width: column.width
                    height: bar.rowHeight
                    // Left column: the object (and what is under the pointer in this row).
                    Rectangle {
                        x: 0; y: 0; width: bar.labelWidth - 6; height: parent.height
                        color: rowItem.modelData.active ? NextTheme.activeSoft : "transparent"
                        radius: 3
                    }
                    Text {
                        x: 8
                        y: 4
                        width: bar.labelWidth - 22
                        elide: Text.ElideRight
                        font.family: NextTheme.mono
                        font.pixelSize: 11
                        font.bold: rowItem.modelData.active
                        color: rowItem.modelData.active ? NextTheme.text : NextTheme.muted
                        text: rowItem.modelData.object
                    }
                    Text {
                        x: 8
                        y: 20
                        width: bar.labelWidth - 22
                        elide: Text.ElideRight
                        font.family: NextTheme.mono
                        font.pixelSize: 10
                        color: NextTheme.muted
                        text: bar.hoverRow === rowItem.index && bar.hoverIndex >= 0 && bar.hoverIndex < rowItem.modelData.items.length
                              ? bar.identifier(rowItem.modelData, rowItem.modelData.items[bar.hoverIndex])
                              : (bar.selectedIn(rowItem.modelData) > 0 ? qsTr("%1 residues selected").arg(bar.selectedIn(rowItem.modelData)) : "")
                    }
                    ListView {
                        id: strip
                        x: bar.labelWidth
                        width: bar.stripWidth
                        height: parent.height
                        orientation: ListView.Horizontal
                        clip: true
                        interactive: false
                        contentX: bar.scrollX
                        model: rowItem.modelData.items
                        delegate: Item {
                            id: cellItem
                            required property var modelData
                            required property int index
                            readonly property bool chainStart: index === 0 || rowItem.modelData.items[index - 1].chain !== modelData.chain
                            readonly property int lead: chainStart && index > 0 ? bar.gapCells * bar.cell : 0
                            readonly property bool dragging: bar.dragRow === rowItem.index && bar.dragStart >= 0 &&
                                index >= Math.min(bar.dragStart, bar.dragEnd) && index <= Math.max(bar.dragStart, bar.dragEnd)
                            width: bar.cell + lead
                            height: strip.height
                            // ruler: the residue number of every fifth residue; a chain start also shows its id
                            Text {
                                x: cellItem.lead
                                y: 2
                                visible: cellItem.index % 5 === 0 || cellItem.chainStart
                                text: (cellItem.chainStart && modelData.chain.length > 0 ? modelData.chain + ":" : "") + modelData.resi
                                font.family: NextTheme.mono
                                font.pixelSize: 9
                                font.bold: cellItem.chainStart
                                color: cellItem.chainStart ? NextTheme.text : NextTheme.muted
                            }
                            Rectangle {
                                x: cellItem.lead
                                y: 15
                                width: bar.cell
                                height: 20
                                color: cellItem.dragging ? NextTheme.accentSoft
                                       : (modelData.selected ? Qt.rgba(0.94, 0.64, 0.23, 0.4)
                                          : (bar.hoverRow === rowItem.index && bar.hoverIndex === cellItem.index ? NextTheme.accentSoft : "transparent"))
                                radius: 2
                            }
                            Text {
                                x: cellItem.lead
                                y: 15
                                width: bar.cell
                                height: 20
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.letter
                                font.family: NextTheme.mono
                                font.pixelSize: 12
                                font.bold: modelData.selected
                                color: NextTheme.text
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: strip
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        function indexAt(mouse) { return strip.indexAt(mouse.x + strip.contentX, 20) }
                        onPositionChanged: function(mouse) {
                            const index = indexAt(mouse)
                            bar.hoverRow = rowItem.index
                            bar.hoverIndex = index
                            if (pressed && bar.dragRow === rowItem.index && bar.dragStart >= 0 && index >= 0) bar.dragEnd = index
                        }
                        onPressed: function(mouse) {
                            const index = indexAt(mouse)
                            bar.dragRow = rowItem.index
                            bar.dragStart = index
                            bar.dragEnd = index
                        }
                        onReleased: function(mouse) {
                            const index = indexAt(mouse)
                            const start = bar.dragStart
                            bar.dragStart = -1
                            if (start < 0 || index < 0) return
                            const additive = (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
                            if ((mouse.modifiers & Qt.ShiftModifier) !== 0 && bar.anchorRow === rowItem.index && bar.anchorIndex >= 0) {
                                const anchor = bar.anchorIndex
                                bar.selectRange(rowItem.index, anchor, index, additive ? "add" : "replace")
                                bar.anchorIndex = anchor
                            } else {
                                bar.selectRange(rowItem.index, start, index, additive ? "add" : "replace")
                            }
                        }
                        onExited: { if (bar.hoverRow === rowItem.index) { bar.hoverRow = -1; bar.hoverIndex = -1 } }
                        onWheel: function(wheel) {
                            const delta = wheel.angleDelta.x !== 0 ? wheel.angleDelta.x : wheel.angleDelta.y
                            bar.scrollX = Math.max(0, Math.min(Math.max(0, bar.maxContent - bar.stripWidth), bar.scrollX - delta))
                        }
                    }
                }
            }
        }
    }
    ScrollBar {
        id: horizontalBar
        orientation: Qt.Horizontal
        anchors { left: parent.left; leftMargin: bar.labelWidth; right: parent.right; rightMargin: 8; bottom: parent.bottom }
        height: 6
        policy: bar.maxContent > bar.stripWidth ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        size: bar.maxContent > 0 ? Math.min(1, bar.stripWidth / bar.maxContent) : 1
        position: bar.maxContent > 0 ? bar.scrollX / bar.maxContent : 0
        onPositionChanged: if (pressed) bar.scrollX = position * bar.maxContent
        Accessible.name: qsTr("Sequence scroll")
    }
    Text {
        anchors.centerIn: parent
        visible: bar.rows.length === 0
        text: qsTr("no residues")
        color: NextTheme.muted
        font.pixelSize: NextTheme.fontSize
    }
}
