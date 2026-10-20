import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Trajectory import (SEQ-330 G1): the options of `traj load` for the active object. `--mapping` has no default
// in the command, so a bare "open trajectory" cannot work; this panel chooses it explicitly and explains it.
Popup {
    id: panel
    objectName: "trajectoryImportOverlay"
    property var viewport
    signal run(string text)
    signal browseRequested()

    readonly property var formats: ["auto", "dcd", "trr", "xtc", "rst7", "mdcrd", "crd", "crdbox", "netcdf", "nc", "ncdf", "ncrst", "lammps", "lammpstrj", "dump", "binpos", "h5md"]
    readonly property var units: ["auto", "angstrom", "nanometer"]
    readonly property var mappings: ["index", "exact", "explicit"]
    readonly property var activeObject: {
        const items = viewport ? viewport.objectItems : []
        for (const item of items) if (item.active) return item.name
        return ""
    }
    readonly property bool ready: pathField.text.trim().length > 0 && (mappingBox.currentText !== "explicit" || atomMap.text.trim().length > 0)

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, Overlay.overlay ? Overlay.overlay.width - 40 : 560)
    height: Math.min(520, Overlay.overlay ? Overlay.overlay.height - 60 : 520)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    function openPanel(path) {
        if (path !== undefined) pathField.text = path
        open()
        pathField.forceActiveFocus()
    }
    function setPath(path) { pathField.text = path }
    function mappingHelp(mapping) {
        if (mapping === "exact") return qsTr("Atoms are matched by their stable IDs; the file must carry the same IDs as the loaded structure.")
        if (mapping === "explicit") return qsTr("You list the stable IDs, in file order, that the trajectory atoms correspond to (for example 3,2,1).")
        return qsTr("Atom number n of the file is atom n of the structure. Use this for most MD files.")
    }
    function commandText() {
        let text = "traj load --path " + quote(pathField.text.trim()) + " --mapping " + mappingBox.currentText
        if (formatBox.currentText !== "auto") text += " --file-format " + formatBox.currentText
        if (unitBox.currentText !== "auto") text += " --coordinate-unit " + unitBox.currentText
        if (mappingBox.currentText === "explicit") text += " --atom-map " + quote(atomMap.text.trim())
        if (providerField.text.trim().length > 0) text += " --provider " + quote(providerField.text.trim())
        if (pluginField.text.trim().length > 0) text += " --plugin-path " + quote(pluginField.text.trim())
        if (groupField.text.trim().length > 0) text += " --particle-group " + quote(groupField.text.trim())
        if (cacheField.text.trim().length > 0 && cacheField.text.trim() !== "256") text += " --cache-mib " + cacheField.text.trim()
        if (prefetchField.text.trim().length > 0 && prefetchField.text.trim() !== "4") text += " --prefetch-frames " + prefetchField.text.trim()
        return text
    }

    component Field: TextField {
        implicitHeight: 28
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        placeholderTextColor: NextTheme.muted
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
    }
    component Choice: ComboBox {
        implicitHeight: 28
        implicitWidth: 150
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
            Text { text: qsTr("OPEN TRAJECTORY"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1 }
            Text { Layout.fillWidth: true; text: qsTr("for %1").arg(panel.activeObject); color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold; elide: Text.ElideRight }
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
                Caption { text: qsTr("File"); width: parent.width }
                RowLayout {
                    width: parent.width
                    spacing: 6
                    Field { id: pathField; objectName: "trajectoryPath"; Layout.fillWidth: true; placeholderText: qsTr("path of the trajectory file") }
                    NextButton { text: qsTr("Browse…"); onClicked: panel.browseRequested() }
                }
                Row {
                    spacing: 16
                    Column {
                        spacing: 3
                        Caption { text: qsTr("Format") }
                        Choice { id: formatBox; model: panel.formats }
                    }
                    Column {
                        spacing: 3
                        Caption { text: qsTr("Coordinate unit") }
                        Choice { id: unitBox; model: panel.units }
                    }
                }
                Column {
                    width: parent.width
                    spacing: 3
                    Caption { text: qsTr("Atom mapping") }
                    Choice { id: mappingBox; objectName: "trajectoryMappingSelector"; model: panel.mappings }
                    Caption { width: parent.width; text: panel.mappingHelp(mappingBox.currentText) }
                    Field {
                        id: atomMap
                        width: parent.width
                        visible: mappingBox.currentText === "explicit"
                        placeholderText: qsTr("Stable IDs, for example: 3,2,1")
                    }
                }
                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                NextButton {
                    id: advancedToggle
                    text: (checked ? "▾ " : "▸ ") + qsTr("Advanced")
                    outlined: false
                    checkable: true
                }
                Grid {
                    visible: advancedToggle.checked
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    Caption { text: qsTr("Provider"); height: 28; verticalAlignment: Text.AlignVCenter }
                    Field { id: providerField; width: 280; placeholderText: "auto" }
                    Caption { text: qsTr("Plugin path"); height: 28; verticalAlignment: Text.AlignVCenter }
                    Field { id: pluginField; width: 280 }
                    Caption { text: qsTr("Particle group"); height: 28; verticalAlignment: Text.AlignVCenter }
                    Field { id: groupField; width: 280 }
                    Caption { text: qsTr("Cache (MiB)"); height: 28; verticalAlignment: Text.AlignVCenter }
                    Field { id: cacheField; width: 90; text: "256" }
                    Caption { text: qsTr("Prefetch frames"); height: 28; verticalAlignment: Text.AlignVCenter }
                    Field { id: prefetchField; width: 90; text: "4" }
                }
                Text {
                    width: parent.width
                    text: panel.ready ? panel.commandText() : ""
                    wrapMode: Text.WrapAnywhere
                    font.family: NextTheme.mono
                    font.pixelSize: 12
                    color: NextTheme.muted
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Item { Layout.fillWidth: true }
            NextButton { text: qsTr("Cancel"); onClicked: panel.close() }
            NextButton {
                text: qsTr("Open")
                checked: true
                enabled: panel.ready
                onClicked: { panel.run(panel.commandText()); panel.close() }
            }
        }
    }
}
