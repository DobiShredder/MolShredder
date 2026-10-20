import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Molecular surface (SEQ-330 G1): the options of `surface show` — kind, selection, probe radius, grid spacing,
// color, opacity and the computation budgets. Show/Update and Hide run the ordinary `surface …` commands.
Popup {
    id: panel
    objectName: "surfacePanel"
    property var viewport
    signal run(string text)

    readonly property var kinds: ["sas", "vdw"]
    readonly property var colors: ["cyan", "blue", "green", "magenta", "orange", "red", "white", "yellow"]
    readonly property var spacings: [{label: qsTr("coarse"), value: "1.2"}, {label: qsTr("normal"), value: "0.7"}, {label: qsTr("fine"), value: "0.4"}]
    readonly property bool valid: positive(probeField.text, true) && positive(spacingField.text, false)
                                   && positive(voxelField.text, false) && positive(memoryField.text, false)

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, Overlay.overlay ? Overlay.overlay.width - 40 : 460)
    height: Math.min(520, Overlay.overlay ? Overlay.overlay.height - 60 : 520)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function positive(text, allowZero) {
        const value = parseFloat(String(text).trim())
        return !isNaN(value) && (allowZero ? value >= 0 : value > 0)
    }
    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    function commandText() {
        const selection = selectionField.text.trim().length > 0 ? selectionField.text.trim() : "all"
        let text = "surface show --kind " + kindBox.currentText + " --selection " + quote(selection)
            + " --probe-radius " + probeField.text.trim() + " --grid-spacing " + spacingField.text.trim()
            + " --color " + colorBox.currentText + " --opacity " + opacitySlider.value.toFixed(2)
        if (voxelField.text.trim() !== "8388608") text += " --voxel-budget " + voxelField.text.trim()
        if (memoryField.text.trim() !== "536870912") text += " --memory-budget-bytes " + memoryField.text.trim()
        return text
    }
    function openPanel() { open() }

    component Field: TextField {
        implicitHeight: 28
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        placeholderTextColor: NextTheme.muted
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? (panel.valid || parent === selectionField ? NextTheme.active : NextTheme.error) : NextTheme.line }
    }
    component Choice: ComboBox {
        implicitHeight: 28
        implicitWidth: 120
        font.pixelSize: NextTheme.fontSize
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
        contentItem: Text { leftPadding: 8; text: parent.displayText; color: NextTheme.text; font: parent.font; verticalAlignment: Text.AlignVCenter }
    }
    component Caption: Text { color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; wrapMode: Text.Wrap }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("MOLECULAR SURFACE"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: body.implicitHeight + 12
            ScrollBar.vertical: ScrollBar { }
            Column {
                id: body
                x: 14
                width: parent.width - 28
                spacing: 8

                Row {
                    spacing: 16
                    Column {
                        spacing: 3
                        Caption { text: qsTr("Kind") }
                        Choice { id: kindBox; model: panel.kinds }
                    }
                    Column {
                        spacing: 3
                        Caption { text: qsTr("Color") }
                        Choice { id: colorBox; model: panel.colors }
                    }
                }
                Caption {
                    width: parent.width
                    text: kindBox.currentText === "sas"
                          ? qsTr("Solvent-accessible surface: the path of a probe sphere's center rolling over the atoms.")
                          : qsTr("Van der Waals surface: the union of the atom spheres.")
                }
                Caption { text: qsTr("Selection") }
                RowLayout {
                    width: parent.width
                    spacing: 6
                    Field { id: selectionField; objectName: "surfaceSelection"; Layout.fillWidth: true; placeholderText: "all" }
                    NextButton { text: "sele"; onClicked: selectionField.text = "@sele" }
                    NextButton { text: qsTr("all"); onClicked: selectionField.text = "" }
                }
                Row {
                    spacing: 16
                    Column {
                        spacing: 3
                        Caption { text: qsTr("Probe radius (Å)") }
                        Field { id: probeField; width: 110; text: "1.4"; enabled: kindBox.currentText === "sas"; opacity: enabled ? 1 : 0.5 }
                    }
                    Column {
                        spacing: 3
                        Caption { text: qsTr("Grid spacing (Å)") }
                        Field { id: spacingField; objectName: "surfaceSpacing"; width: 110; text: "0.7" }
                    }
                }
                Row {
                    spacing: 6
                    Repeater {
                        model: panel.spacings
                        delegate: NextButton {
                            required property var modelData
                            text: modelData.label + " " + modelData.value
                            checked: spacingField.text.trim() === modelData.value
                            onClicked: spacingField.text = modelData.value
                        }
                    }
                }
                Caption { width: parent.width; text: qsTr("A smaller spacing is smoother but needs roughly (old/new)³ times the memory and time.") }
                Caption { text: qsTr("Opacity") + "  " + opacitySlider.value.toFixed(2) }
                Slider {
                    id: opacitySlider
                    width: parent.width
                    from: 0.05
                    to: 1
                    value: 0.72
                    stepSize: 0.01
                }
                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                NextButton { id: advancedToggle; text: (checked ? "▾ " : "▸ ") + qsTr("Budgets"); outlined: false; checkable: true }
                Grid {
                    visible: advancedToggle.checked
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    Caption { text: qsTr("Voxel budget"); height: 28; verticalAlignment: Text.AlignVCenter }
                    Field { id: voxelField; width: 180; text: "8388608" }
                    Caption { text: qsTr("Memory budget (bytes)"); height: 28; verticalAlignment: Text.AlignVCenter }
                    Field { id: memoryField; width: 180; text: "536870912" }
                }
                Text {
                    width: parent.width
                    text: panel.valid ? panel.commandText() : qsTr("Probe radius must be ≥ 0; spacing and budgets must be > 0.")
                    wrapMode: Text.WrapAnywhere
                    font.family: panel.valid ? NextTheme.mono : Qt.application.font.family
                    font.pixelSize: panel.valid ? 12 : NextTheme.smallFont
                    color: panel.valid ? NextTheme.muted : NextTheme.error
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            NextButton { text: qsTr("Hide surface"); onClicked: panel.run("surface hide") }
            Item { Layout.fillWidth: true }
            NextButton { text: qsTr("Close"); onClicked: panel.close() }
            NextButton { text: qsTr("Show surface"); checked: true; enabled: panel.valid; onClicked: panel.run(panel.commandText()) }
        }
    }
}
