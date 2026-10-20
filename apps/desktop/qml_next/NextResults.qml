import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Analysis results (SEQ-330 U5): the persistent results of the workspace with their values, table and a plot.
// Show/hide/delete/export run the ordinary `result …` commands; nothing here computes anything.
Popup {
    id: panel
    property var viewport
    property int selectedId: -1
    property var record: null
    signal run(string text)
    signal exportRequested(int id, string format)
    signal info(string text)

    readonly property var items: viewport ? viewport.analysisItems : []
    readonly property int maxRows: 300

    objectName: "analysisOverlay"
    modal: false
    focus: false
    padding: 0
    closePolicy: Popup.CloseOnEscape
    x: Overlay.overlay ? Overlay.overlay.width - width - 12 : 0
    y: Overlay.overlay ? Overlay.overlay.height - height - 110 : 0
    width: Math.min(820, Overlay.overlay ? Overlay.overlay.width - 24 : 820)
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 110 : 640)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    onItemsChanged: reload()

    // ---- running new analyses (background tasks with progress and cancel) ------------------------------------
    readonly property var kinds: [
        {id: "center", label: qsTr("Center"), fields: [{key: "selection", label: qsTr("selection"), value: "all"}, {key: "mode", label: qsTr("mode"), value: "com", choices: ["com", "centroid"]}]},
        {id: "distance", label: qsTr("Distance"), fields: [{key: "from", label: qsTr("from"), value: "index 1"}, {key: "to", label: qsTr("to"), value: "index 2"}, {key: "pbc", label: qsTr("pbc"), value: "raw", choices: ["raw", "minimum-image"]}]},
        {id: "angle", label: qsTr("Angle"), fields: [{key: "first", label: qsTr("first"), value: "index 1"}, {key: "vertex", label: qsTr("vertex"), value: "index 2"}, {key: "third", label: qsTr("third"), value: "index 3"}, {key: "pbc", label: qsTr("pbc"), value: "raw", choices: ["raw", "minimum-image"]}]},
        {id: "dihedral", label: qsTr("Dihedral"), fields: [{key: "first", label: qsTr("first"), value: "index 1"}, {key: "second", label: qsTr("second"), value: "index 2"}, {key: "third", label: qsTr("third"), value: "index 3"}, {key: "fourth", label: qsTr("fourth"), value: "index 4"}, {key: "pbc", label: qsTr("pbc"), value: "raw", choices: ["raw", "minimum-image"]}]},
        {id: "sasa", label: qsTr("Surface area"), fields: [{key: "selection", label: qsTr("selection"), value: "all"}, {key: "probe", label: qsTr("probe (Å)"), value: "1.4"}, {key: "samples", label: qsTr("samples"), value: "960"}, {key: "budget", label: qsTr("budget"), value: "100000000"}]},
        {id: "rdf", label: qsTr("Pair distribution"), fields: [{key: "first", label: qsTr("first"), value: "all"}, {key: "second", label: qsTr("second"), value: ""}, {key: "radius", label: qsTr("max radius (Å)"), value: "10"}, {key: "bin", label: qsTr("bin width (Å)"), value: "0.1"}, {key: "normalization", label: qsTr("normalization"), value: "count", choices: ["count", "g-r"]}, {key: "pbc", label: qsTr("pbc"), value: "raw", choices: ["raw", "minimum-image"]}, {key: "budget", label: qsTr("budget"), value: "100000000"}]},
        {id: "contacts", label: qsTr("Contacts"), fields: [{key: "first", label: qsTr("first"), value: "all"}, {key: "second", label: qsTr("second"), value: ""}, {key: "cutoff", label: qsTr("cutoff (Å)"), value: "4"}, {key: "pbc", label: qsTr("pbc"), value: "raw", choices: ["raw", "minimum-image"]}]},
        {id: "rmsd", label: qsTr("Trajectory RMSD"), fields: [{key: "selection", label: qsTr("selection"), value: "all"}, {key: "reference", label: qsTr("reference frame"), value: "0"}]},
        {id: "rmsd-matrix", label: qsTr("RMSD matrix"), fields: [{key: "selection", label: qsTr("selection"), value: "all"}, {key: "budget", label: qsTr("frame-pair budget"), value: "1000000"}]}
    ]
    property int kindIndex: 0
    property var fieldValues: ({})
    readonly property bool taskRunning: viewport ? viewport.analysisTaskRunning : false

    function currentKind() { return kinds[kindIndex] }
    function resetFields() {
        const values = ({})
        for (const field of currentKind().fields) values[field.key] = field.value
        fieldValues = values
    }
    function setField(key, value) {
        const values = fieldValues
        values[key] = value
        fieldValues = values
    }
    function value(key) { return fieldValues[key] !== undefined ? String(fieldValues[key]).trim() : "" }
    function num(key, fallback) { const v = parseFloat(value(key)); return isNaN(v) ? fallback : v }
    function whole(key, fallback) { const v = parseInt(value(key), 10); return isNaN(v) || v < 0 ? fallback : v }
    function launch() {
        if (!viewport) return false
        const name = resultName.text.trim()
        const kind = currentKind().id
        let started = false
        switch (kind) {
        case "center": started = viewport.analyzeCenter(value("selection"), value("mode"), name); break
        case "distance": started = viewport.analyzeDistance(value("from"), value("to"), value("pbc"), name); break
        case "angle": started = viewport.analyzeAngle(value("first"), value("vertex"), value("third"), value("pbc"), name); break
        case "dihedral": started = viewport.analyzeDihedral(value("first"), value("second"), value("third"), value("fourth"), value("pbc"), name); break
        case "sasa": started = viewport.analyzeSasa(value("selection"), num("probe", 1.4), whole("samples", 960), whole("budget", 100000000), name); break
        case "rdf": started = viewport.analyzeRdf(value("first"), value("second"), num("radius", 10), num("bin", 0.1), value("normalization"), value("pbc"), whole("budget", 100000000), name); break
        case "contacts": started = viewport.analyzeContacts(value("first"), value("second"), num("cutoff", 4), value("pbc"), name); break
        case "rmsd": started = viewport.analyzeTrajectoryRmsd(value("selection"), whole("reference", 0), name); break
        case "rmsd-matrix": started = viewport.analyzeTrajectoryRmsdMatrix(value("selection"), whole("budget", 1000000), name); break
        }
        info((started ? qsTr("Analysis started: %1") : qsTr("Analysis could not start: %1")).arg(currentKind().label))
        return started
    }

    function openResults() {
        open()
        if (selectedId < 0 && items.length > 0) select(items[items.length - 1].id)
        else reload()
    }
    function select(id) {
        selectedId = id
        reload()
    }
    function itemFor(id) {
        for (const item of items) if (item.id === id) return item
        return null
    }
    function reload() {
        if (selectedId >= 0 && itemFor(selectedId) === null) selectedId = items.length > 0 ? items[items.length - 1].id : -1
        if (selectedId < 0 || !viewport) { record = null; return }
        try {
            const parsed = JSON.parse(viewport.analysisResultJson(selectedId))
            record = parsed.data !== undefined ? parsed.data : parsed
        } catch (error) {
            record = null
        }
    }
    function text(value) {
        if (value === null || value === undefined) return ""
        if (Array.isArray(value)) return value.map(function(v) { return text(v) }).join(", ")
        if (typeof value === "object") return JSON.stringify(value)
        return String(value)
    }
    function fieldRows() {
        if (!record || !record.original_fields) return []
        const rows = []
        for (const key of Object.keys(record.original_fields)) {
            if (key === "result_id" || key === "object_id") continue
            rows.push({key: key, value: text(record.original_fields[key])})
        }
        return rows
    }
    function tableOf() { return record && record.table && record.table.columns ? record.table : null }
    function numericColumns() {
        const table = tableOf()
        if (!table || table.rows.length < 2) return []
        const columns = []
        for (let c = 0; c < table.columns.length; ++c) {
            let numeric = true
            for (let r = 0; r < Math.min(5, table.rows.length) && numeric; ++r) numeric = typeof table.rows[r][c] === "number"
            if (numeric) columns.push(table.columns[c])
        }
        return columns
    }
    function sourceNote(status) {
        return status === "current" ? "" : qsTr("stale: %1").arg(status)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("RESULTS"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }
        // ---- run a new analysis ----------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            implicitHeight: launcher.implicitHeight + 16
            radius: NextTheme.radius
            color: NextTheme.chrome
            border.color: NextTheme.line
            Column {
                id: launcher
                x: 10; y: 8
                width: parent.width - 20
                spacing: 6
                Row {
                    spacing: 8
                    ComboBox {
                        id: kindBox
                        model: panel.kinds.map(function(k) { return k.label })
                        implicitWidth: 170
                        implicitHeight: 26
                        font.pixelSize: NextTheme.fontSize
                        background: Rectangle { radius: NextTheme.radius; color: NextTheme.panel; border.color: NextTheme.line }
                        contentItem: Text { leftPadding: 8; text: kindBox.displayText; color: NextTheme.text; font: kindBox.font; verticalAlignment: Text.AlignVCenter }
                        onActivated: { panel.kindIndex = currentIndex; panel.resetFields() }
                        Component.onCompleted: panel.resetFields()
                    }
                    TextField {
                        id: resultName
                        implicitWidth: 150
                        implicitHeight: 26
                        placeholderText: qsTr("result name (optional)")
                        placeholderTextColor: NextTheme.muted
                        color: NextTheme.text
                        font.pixelSize: NextTheme.fontSize
                        selectByMouse: true
                        background: Rectangle { radius: NextTheme.radius; color: NextTheme.panel; border.color: resultName.activeFocus ? NextTheme.active : NextTheme.line }
                    }
                    NextButton { text: qsTr("Run"); checked: true; enabled: !panel.taskRunning; onClicked: panel.launch() }
                    NextButton { id: cancelButton; objectName: "analysisTaskCancelButton"; text: qsTr("Cancel"); enabled: panel.taskRunning; onClicked: viewport.cancelAnalysisTask() }
                }
                Flow {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: panel.kinds[panel.kindIndex].fields
                        delegate: Row {
                            id: fieldRow
                            required property var modelData
                            spacing: 4
                            Text { text: fieldRow.modelData.label; height: 26; verticalAlignment: Text.AlignVCenter; color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
                            Loader {
                                sourceComponent: fieldRow.modelData.choices !== undefined ? choiceField : textField
                            }
                            Component {
                                id: textField
                                TextField {
                                    implicitWidth: fieldRow.modelData.key === "selection" || fieldRow.modelData.key === "first" || fieldRow.modelData.key === "second" || fieldRow.modelData.key === "from" || fieldRow.modelData.key === "to" ? 120 : 76
                                    implicitHeight: 26
                                    text: panel.fieldValues[fieldRow.modelData.key] !== undefined ? panel.fieldValues[fieldRow.modelData.key] : ""
                                    color: NextTheme.text
                                    font.pixelSize: NextTheme.fontSize
                                    selectByMouse: true
                                    background: Rectangle { radius: NextTheme.radius; color: NextTheme.panel; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
                                    onTextEdited: panel.setField(fieldRow.modelData.key, text)
                                }
                            }
                            Component {
                                id: choiceField
                                ComboBox {
                                    model: fieldRow.modelData.choices
                                    implicitWidth: 112
                                    implicitHeight: 26
                                    font.pixelSize: NextTheme.fontSize
                                    background: Rectangle { radius: NextTheme.radius; color: NextTheme.panel; border.color: NextTheme.line }
                                    contentItem: Text { leftPadding: 8; text: parent.displayText; color: NextTheme.text; font: parent.font; verticalAlignment: Text.AlignVCenter }
                                    currentIndex: Math.max(0, fieldRow.modelData.choices.indexOf(panel.fieldValues[fieldRow.modelData.key]))
                                    onActivated: panel.setField(fieldRow.modelData.key, currentText)
                                }
                            }
                        }
                    }
                }
                Row {
                    spacing: 8
                    ProgressBar {
                        objectName: "analysisTaskProgress"
                        width: 220
                        value: viewport ? viewport.analysisTaskProgress : 0
                        visible: panel.taskRunning
                    }
                    Text {
                        height: 18
                        verticalAlignment: Text.AlignVCenter
                        visible: panel.taskRunning
                        text: viewport ? viewport.analysisTaskStage : ""
                        color: NextTheme.muted
                        font.pixelSize: NextTheme.smallFont
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- list ----------------------------------------------------------------------------------------
            ListView {
                id: list
                Layout.preferredWidth: 210
                Layout.fillHeight: true
                clip: true
                model: panel.items
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: list.width
                    height: 44
                    color: modelData.id === panel.selectedId ? NextTheme.activeSoft : (rowHover.hovered ? NextTheme.chrome : "transparent")
                    HoverHandler { id: rowHover }
                    TapHandler { onTapped: panel.select(row.modelData.id) }
                    Rectangle {
                        id: dot
                        visible: row.modelData.hasOverlay
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        width: 12; height: 12; radius: 6
                        color: row.modelData.visible ? NextTheme.active : "transparent"
                        border.color: row.modelData.visible ? NextTheme.active : NextTheme.muted
                        Accessible.name: qsTr("Show in viewport")
                        TapHandler { onTapped: panel.run((row.modelData.visible ? "result hide --id " : "result show --id ") + row.modelData.id) }
                    }
                    Column {
                        anchors { left: parent.left; leftMargin: 30; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        Text { width: parent.width; text: row.modelData.name; elide: Text.ElideRight; color: NextTheme.text; font.pixelSize: NextTheme.fontSize }
                        Text {
                            width: parent.width
                            text: row.modelData.kind + (panel.sourceNote(row.modelData.sourceStatus).length > 0 ? " · " + panel.sourceNote(row.modelData.sourceStatus) : "")
                            elide: Text.ElideRight
                            color: row.modelData.sourceStatus === "current" ? NextTheme.muted : NextTheme.accent
                            font.pixelSize: NextTheme.smallFont
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    visible: list.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("No results yet.\nRun something from the Analyze menu.")
                    color: NextTheme.muted
                    font.pixelSize: NextTheme.fontSize
                }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: NextTheme.line }

            // ---- detail --------------------------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 12
                spacing: 8
                visible: panel.record !== null
                Text {
                    Layout.fillWidth: true
                    text: panel.record ? panel.record.name : ""
                    color: NextTheme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: panel.record ? (panel.record.canonical_command + " · " + panel.record.object_name_snapshot + " · " + panel.record.created_at_utc) : ""
                    color: NextTheme.muted
                    font.pixelSize: NextTheme.smallFont
                    elide: Text.ElideRight
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    NextButton {
                        visible: panel.itemFor(panel.selectedId) !== null && panel.itemFor(panel.selectedId).hasOverlay
                        text: panel.itemFor(panel.selectedId) !== null && panel.itemFor(panel.selectedId).visible ? qsTr("Hide in viewport") : qsTr("Show in viewport")
                        onClicked: panel.run((panel.itemFor(panel.selectedId).visible ? "result hide --id " : "result show --id ") + panel.selectedId)
                    }
                    NextButton { text: qsTr("Export CSV…"); visible: panel.tableOf() !== null; onClicked: panel.exportRequested(panel.selectedId, "csv") }
                    NextButton { text: qsTr("Export JSON…"); onClicked: panel.exportRequested(panel.selectedId, "json") }
                    NextButton { text: qsTr("Delete"); onClicked: panel.run("result delete --id " + panel.selectedId) }
                }
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: detail.implicitHeight + 8
                    ScrollBar.vertical: ScrollBar { }
                    Column {
                        id: detail
                        width: parent.width - 12
                        spacing: 10

                        // values
                        Repeater {
                            model: panel.fieldRows()
                            delegate: Row {
                                required property var modelData
                                spacing: 12
                                Text { width: 190; text: modelData.key; color: NextTheme.muted; font.pixelSize: NextTheme.smallFont + 1; elide: Text.ElideRight }
                                Text { width: detail.width - 202; text: modelData.value; color: NextTheme.text; font.family: NextTheme.mono; font.pixelSize: 12; wrapMode: Text.WrapAnywhere }
                            }
                        }

                        // plot of one numeric column against the first column
                        Column {
                            id: plotBox
                            visible: panel.numericColumns().length >= 2
                            width: parent.width
                            spacing: 4
                            Row {
                                spacing: 8
                                Text { text: qsTr("plot"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; anchors.verticalCenter: parent.verticalCenter }
                                ComboBox {
                                    id: yColumn
                                    model: panel.numericColumns().slice(1)
                                    implicitHeight: 24
                                    implicitWidth: 150
                                    font.pixelSize: NextTheme.fontSize
                                    background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: NextTheme.line }
                                    contentItem: Text { leftPadding: 8; text: yColumn.displayText; color: NextTheme.text; font: yColumn.font; verticalAlignment: Text.AlignVCenter }
                                    onCurrentTextChanged: plot.requestPaint()
                                }
                            }
                            Canvas {
                                id: plot
                                objectName: "analysisPlotCanvas"
                                width: parent.width
                                height: 130
                                onWidthChanged: requestPaint()
                                Connections { target: panel; function onRecordChanged() { plot.requestPaint() } }
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.fillStyle = NextTheme.chrome
                                    ctx.fillRect(0, 0, width, height)
                                    const table = panel.tableOf()
                                    if (!table || yColumn.currentText.length === 0) return
                                    const xi = 0
                                    const yi = table.columns.indexOf(yColumn.currentText)
                                    if (yi < 0) return
                                    let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity
                                    for (const r of table.rows) {
                                        x0 = Math.min(x0, r[xi]); x1 = Math.max(x1, r[xi])
                                        y0 = Math.min(y0, r[yi]); y1 = Math.max(y1, r[yi])
                                    }
                                    if (!(x1 > x0)) x1 = x0 + 1
                                    if (!(y1 > y0)) { y0 -= 0.5; y1 += 0.5 }
                                    const pad = 6
                                    ctx.strokeStyle = NextTheme.active
                                    ctx.lineWidth = 1.5
                                    ctx.beginPath()
                                    const step = Math.max(1, Math.floor(table.rows.length / (width * 2)))
                                    for (let i = 0; i < table.rows.length; i += step) {
                                        const r = table.rows[i]
                                        const px = pad + (width - 2 * pad) * (r[xi] - x0) / (x1 - x0)
                                        const py = height - pad - (height - 2 * pad) * (r[yi] - y0) / (y1 - y0)
                                        if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                                    }
                                    ctx.stroke()
                                    ctx.fillStyle = NextTheme.muted
                                    ctx.font = "10px sans-serif"
                                    ctx.fillText(y1.toPrecision(5), pad + 2, 12)
                                    ctx.fillText(y0.toPrecision(5), pad + 2, height - pad - 2)
                                }
                            }
                        }

                        // table
                        Column {
                            visible: panel.tableOf() !== null
                            width: parent.width
                            spacing: 2
                            Text {
                                text: panel.tableOf() ? qsTr("%1 rows").arg(panel.tableOf().rows.length) + (panel.tableOf().rows.length > panel.maxRows ? " · " + qsTr("first %1 shown").arg(panel.maxRows) : "") : ""
                                color: NextTheme.muted
                                font.pixelSize: NextTheme.smallFont
                            }
                            Row {
                                spacing: 0
                                Repeater {
                                    model: panel.tableOf() ? panel.tableOf().columns : []
                                    delegate: Text {
                                        required property string modelData
                                        width: Math.max(70, (detail.width) / Math.max(1, panel.tableOf().columns.length))
                                        text: modelData
                                        color: NextTheme.muted
                                        font.pixelSize: NextTheme.smallFont
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            Repeater {
                                model: panel.tableOf() ? panel.tableOf().rows.slice(0, panel.maxRows) : []
                                delegate: Row {
                                    required property var modelData
                                    Repeater {
                                        model: modelData
                                        delegate: Text {
                                            required property var modelData
                                            width: Math.max(70, (detail.width) / Math.max(1, panel.tableOf().columns.length))
                                            text: typeof modelData === "number" ? Number(modelData.toPrecision(7)).toString() : panel.text(modelData)
                                            color: NextTheme.text
                                            font.family: NextTheme.mono
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true; Layout.fillHeight: true; visible: panel.record === null }
        }
    }
}
