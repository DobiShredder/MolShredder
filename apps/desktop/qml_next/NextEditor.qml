import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Atom and bond editing (SEQ-330 G1): pick atoms with a selection, then change one atom's position, chemistry
// or residue identity, a bond's order, or move the whole selection. The edit commands need stable atom IDs and
// the two revisions of the object; this panel supplies them. Every change is an `edit …` command, so it is
// logged in the output window and undone with Ctrl+Z.
Popup {
    id: panel
    objectName: "coordinateEditOverlay"
    property var viewport
    property var atoms: []
    property var bonds: []
    property var current: null
    property var preview: ({ok: false, message: ""})
    property var revisions: ({})
    signal run(string text)

    readonly property int atomLimit: 300
    readonly property var orders: ["single", "double", "triple", "aromatic", "amide", "zero"]
    readonly property bool trajectoryAttached: revisions.trajectory === true

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(900, Overlay.overlay ? Overlay.overlay.width - 40 : 900)
    height: Math.min(600, Overlay.overlay ? Overlay.overlay.height - 60 : 600)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    function expression() { return selectionField.text.trim().length > 0 ? selectionField.text.trim() : "@sele" }
    function openEditor(selection) {
        if (selection !== undefined && selection.length > 0) selectionField.text = selection
        refresh()
        open()
    }
    function rowsOf(data, columns) {
        const table = data && data.table ? data.table : null
        if (!table) return []
        const result = []
        for (const row of table.rows) {
            const item = ({})
            for (let c = 0; c < table.columns.length; ++c) item[table.columns[c]] = row[c]
            result.push(item)
        }
        return result
    }
    function refresh() {
        revisions = viewport ? viewport.editRevisions() : ({})
        preview = viewport ? viewport.selectionPreview(expression()) : ({ok: false, message: ""})
        if (!preview.ok) { atoms = []; bonds = []; current = null; return }
        atoms = rowsOf(viewport.commandData("object atoms", {
            selection: expression(), limit: atomLimit,
            properties: "index,id,name,element,resn,resi,chain,x,y,z,formal_charge,atomic_number"}))
        bonds = rowsOf(viewport.commandData("object bonds", {selection: expression(), limit: atomLimit}))
        let keep = null
        if (current) for (const atom of atoms) if (atom.id === current.id) keep = atom
        current = keep
        loadFields()
    }
    function loadFields() {
        if (!current) return
        xField.text = String(current.x); yField.text = String(current.y); zField.text = String(current.z)
        nameField.text = current.name
        numberField.text = String(Math.round(current.atomic_number))
        chargeField.text = String(Math.round(current.formal_charge))
        resnField.text = current.resn
        chainField.text = current.chain
        resiField.text = String(current.resi)
    }
    function select(atom) { current = atom; loadFields() }
    function guard() {
        return " --expected-topology-version " + revisions.topologyVersion
             + " --expected-coordinate-source-revision " + revisions.coordinateSourceRevision
    }
    function edit(text) {
        run(text)
        refresh()
    }
    function finite(text) { return text.trim().length > 0 && isFinite(Number(text)) }
    function changedProperties() {
        let text = ""
        if (current && nameField.text.trim() !== current.name && nameField.text.trim().length > 0) text += " --name " + quote(nameField.text.trim())
        if (current && numberField.text.trim() !== String(Math.round(current.atomic_number)) && finite(numberField.text)) text += " --atomic-number " + Math.round(Number(numberField.text))
        if (current && chargeField.text.trim() !== String(Math.round(current.formal_charge)) && finite(chargeField.text)) text += " --formal-charge " + Math.round(Number(chargeField.text))
        return text
    }
    function changedResidue() {
        let text = ""
        if (current && resnField.text.trim() !== current.resn && resnField.text.trim().length > 0) text += " --name " + quote(resnField.text.trim())
        if (current && chainField.text.trim() !== current.chain) text += " --chain " + quote(chainField.text.trim())
        if (current && resiField.text.trim() !== String(current.resi) && finite(resiField.text)) text += " --residue-number " + Math.round(Number(resiField.text))
        return text
    }

    component Field: TextField {
        // Said by screen readers; give each field a `label:` where no placeholder tells what it is.
        property string label: ""
        property string derivedLabel: ""
        Accessible.name: label.length > 0 ? label : (placeholderText.length > 0 ? placeholderText : derivedLabel)
        Item { visible: false; width: 0; height: 0; Component.onCompleted: parent.derivedLabel = NextTheme.captionBefore(parent) }
        implicitHeight: 26
        implicitWidth: 80
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        placeholderTextColor: NextTheme.muted
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
    }
    component Caption: Text { color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; wrapMode: Text.Wrap }
    component Heading: Text { color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold }

    Timer { id: debounce; interval: 250; onTriggered: panel.refresh() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("EDIT ATOMS"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { objectName: "coordinateUndoButton"; text: qsTr("Undo"); onClicked: panel.edit("edit undo") }
            NextButton { objectName: "coordinateRedoButton"; text: qsTr("Redo"); onClicked: panel.edit("edit redo") }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 8
            Caption { text: qsTr("Atoms"); Layout.alignment: Qt.AlignVCenter }
            Field {
                id: selectionField
                objectName: "editSelection"
                Layout.fillWidth: true
                text: "@sele"
                font.family: NextTheme.mono
                onTextChanged: debounce.restart()
                Accessible.name: qsTr("Selection")
            }
            NextButton { text: "sele"; onClicked: selectionField.text = "@sele" }
            NextButton { text: "picked"; onClicked: selectionField.text = "@picked" }
            NextButton { text: qsTr("all"); onClicked: selectionField.text = "all" }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 4
            wrapMode: Text.Wrap
            font.pixelSize: NextTheme.smallFont
            color: panel.preview.ok ? NextTheme.muted : NextTheme.error
            text: panel.preview.ok
                  ? (qsTr("%1 atoms selected").arg(panel.preview.atomCount) + (panel.preview.atomCount > panel.atomLimit ? " · " + qsTr("first %1 listed").arg(panel.atomLimit) : ""))
                  : panel.preview.message
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            visible: panel.trajectoryAttached
            wrapMode: Text.Wrap
            font.pixelSize: NextTheme.smallFont
            color: NextTheme.accent
            text: qsTr("A trajectory is attached to this object; coordinate edits are blocked.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 6
            spacing: 0

            // ---- atom list -----------------------------------------------------------------------------------
            ListView {
                id: atomList
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                clip: true
                model: panel.atoms
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    id: atomRow
                    required property var modelData
                    width: atomList.width
                    height: 26
                    color: panel.current && panel.current.id === modelData.id ? NextTheme.activeSoft : (rowHover.hovered ? NextTheme.chrome : "transparent")
                    HoverHandler { id: rowHover }
                    TapHandler { onTapped: panel.select(atomRow.modelData) }
                    Text {
                        anchors { left: parent.left; leftMargin: 10; right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                        text: "#" + atomRow.modelData.id + "  " + atomRow.modelData.name + "  " + atomRow.modelData.element + "  "
                              + atomRow.modelData.resn + " " + atomRow.modelData.resi + (atomRow.modelData.chain.length > 0 ? ":" + atomRow.modelData.chain : "")
                        color: NextTheme.text
                        font.family: NextTheme.mono
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    visible: atomList.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Select atoms first: click in the viewport in an atom mode, or type an expression.")
                    color: NextTheme.muted
                    font.pixelSize: NextTheme.smallFont
                }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: NextTheme.line }

            // ---- editors -------------------------------------------------------------------------------------
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: editors.implicitHeight + 16
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: editors
                    x: 14; y: 6
                    width: parent.width - 28
                    spacing: 8

                    Heading { text: panel.current ? qsTr("Atom #%1 · %2").arg(panel.current.id).arg(panel.current.name) : qsTr("Atom") }
                    Caption { visible: !panel.current; width: parent.width; text: qsTr("Pick an atom in the list to edit it.") }
                    Column {
                        visible: panel.current !== null
                        width: parent.width
                        spacing: 8

                        Caption { text: qsTr("Position (Å)") }
                        Row {
                            spacing: 6
                            Field { id: xField; label: qsTr("Position x"); objectName: "editX" }
                            Field { id: yField; label: qsTr("Position y") }
                            Field { id: zField; label: qsTr("Position z") }
                            NextButton {
                                objectName: "coordinateApplyButton"
                                text: qsTr("Move atom")
                                enabled: panel.finite(xField.text) && panel.finite(yField.text) && panel.finite(zField.text) && !panel.trajectoryAttached
                                onClicked: panel.edit("edit atom-position --atom-id " + panel.current.id + " --x " + Number(xField.text) + " --y " + Number(yField.text)
                                                       + " --z " + Number(zField.text) + panel.guard())
                            }
                        }
                        Caption { text: qsTr("Chemistry") }
                        Row {
                            spacing: 6
                            Column { spacing: 2; Caption { text: qsTr("name") } Field { id: nameField; implicitWidth: 80 } }
                            Column { spacing: 2; Caption { text: qsTr("atomic number") } Field { id: numberField; implicitWidth: 80 } }
                            Column { spacing: 2; Caption { text: qsTr("formal charge") } Field { id: chargeField; implicitWidth: 80 } }
                            NextButton {
                                anchors.bottom: parent.bottom
                                text: qsTr("Apply")
                                enabled: panel.changedProperties().length > 0
                                onClicked: panel.edit("edit atom-properties --atom-id " + panel.current.id + panel.changedProperties() + panel.guard())
                            }
                        }
                        Caption { text: qsTr("Residue") }
                        Row {
                            spacing: 6
                            Column { spacing: 2; Caption { text: qsTr("name") } Field { id: resnField; implicitWidth: 80 } }
                            Column { spacing: 2; Caption { text: qsTr("chain") } Field { id: chainField; implicitWidth: 60 } }
                            Column { spacing: 2; Caption { text: qsTr("number") } Field { id: resiField; implicitWidth: 80 } }
                            NextButton {
                                anchors.bottom: parent.bottom
                                text: qsTr("Apply")
                                enabled: panel.changedResidue().length > 0
                                onClicked: panel.edit("edit residue-properties --atom-id " + panel.current.id + panel.changedResidue() + panel.guard())
                            }
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                    Heading { text: qsTr("Bonds of the selection") }
                    Caption { visible: panel.bonds.length === 0; text: qsTr("none") }
                    Repeater {
                        model: panel.bonds
                        delegate: Row {
                            id: bondRow
                            required property var modelData
                            spacing: 8
                            Text {
                                width: 150
                                anchors.verticalCenter: parent.verticalCenter
                                text: "#" + bondRow.modelData.id1 + " " + bondRow.modelData.name1 + " – #" + bondRow.modelData.id2 + " " + bondRow.modelData.name2
                                color: NextTheme.text
                                font.family: NextTheme.mono
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            ComboBox {
                                id: orderBox
                                Accessible.name: qsTr("Bond order")
                                model: panel.orders
                                currentIndex: Math.max(0, panel.orders.indexOf(bondRow.modelData.order))
                                implicitWidth: 110
                                implicitHeight: 26
                                font.pixelSize: NextTheme.fontSize
                                background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: NextTheme.line }
                                contentItem: Text { leftPadding: 8; text: orderBox.displayText; color: NextTheme.text; font: orderBox.font; verticalAlignment: Text.AlignVCenter }
                            }
                            NextButton {
                                text: qsTr("Set order")
                                enabled: orderBox.currentText !== bondRow.modelData.order && bondRow.modelData.bond_id > 0
                                onClicked: panel.edit("edit bond-order --bond-id " + bondRow.modelData.bond_id + " --order " + orderBox.currentText + panel.guard())
                            }
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                    Heading { text: qsTr("Move the whole selection") }
                    Row {
                        spacing: 6
                        Field { id: dxField; label: qsTr("Shift x"); text: "0"; objectName: "moveX" }
                        Field { id: dyField; label: qsTr("Shift y"); text: "0" }
                        Field { id: dzField; label: qsTr("Shift z"); text: "0" }
                        NextButton {
                            text: qsTr("Shift by")
                            enabled: panel.finite(dxField.text) && panel.finite(dyField.text) && panel.finite(dzField.text) && panel.preview.ok && !panel.trajectoryAttached
                            onClicked: panel.edit("edit move-atoms --selection " + panel.quote(panel.expression()) + " --mode add --values "
                                                   + panel.quote(Number(dxField.text) + " " + Number(dyField.text) + " " + Number(dzField.text)))
                        }
                    }
                    Caption { width: parent.width; text: qsTr("The shift is in the unit of the object's coordinates (Å for most files).") }
                }
            }
        }
    }
}
