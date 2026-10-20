import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Molecule builder (SEQ-330 G1): type atoms (element, position, charge) and bonds (two atom numbers and an order)
// into tables, or start from a template, and build one validated residue as a new object (`build molecule`).
Popup {
    id: builder
    objectName: "moleculeBuilderOverlay"
    property var viewport
    signal run(string text)

    readonly property var orders: ["single", "double", "triple", "aromatic", "amide"]
    readonly property var units: ["angstrom", "nanometer"]
    readonly property var symbols: ["H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca",
                                    "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr", "I"]
    readonly property var numbers: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                    21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 53]
    readonly property var templates: [
        {label: qsTr("Water"), name: "water", residue: "HOH",
         atoms: [["O", "O", 0, 0, 0], ["H1", "H", 0.757, 0.586, 0], ["H2", "H", -0.757, 0.586, 0]],
         bonds: [[1, 2, "single"], [1, 3, "single"]]},
        {label: qsTr("Methane"), name: "methane", residue: "MET",
         atoms: [["C", "C", 0, 0, 0], ["H1", "H", 0.629, 0.629, 0.629], ["H2", "H", -0.629, -0.629, 0.629], ["H3", "H", -0.629, 0.629, -0.629], ["H4", "H", 0.629, -0.629, -0.629]],
         bonds: [[1, 2, "single"], [1, 3, "single"], [1, 4, "single"], [1, 5, "single"]]},
        {label: qsTr("Carbon dioxide"), name: "co2", residue: "CO2",
         atoms: [["C", "C", 0, 0, 0], ["O1", "O", 1.16, 0, 0], ["O2", "O", -1.16, 0, 0]],
         bonds: [[1, 2, "double"], [1, 3, "double"]]},
        {label: qsTr("Ethene"), name: "ethene", residue: "ETH",
         atoms: [["C1", "C", -0.667, 0, 0], ["C2", "C", 0.667, 0, 0], ["H1", "H", -1.237, 0.923, 0], ["H2", "H", -1.237, -0.923, 0], ["H3", "H", 1.237, 0.923, 0], ["H4", "H", 1.237, -0.923, 0]],
         bonds: [[1, 2, "double"], [1, 3, "single"], [1, 4, "single"], [2, 5, "single"], [2, 6, "single"]]},
        {label: qsTr("Benzene"), name: "benzene", residue: "BNZ",
         atoms: [["C1", "C", 1.39, 0, 0], ["C2", "C", 0.695, 1.204, 0], ["C3", "C", -0.695, 1.204, 0], ["C4", "C", -1.39, 0, 0], ["C5", "C", -0.695, -1.204, 0], ["C6", "C", 0.695, -1.204, 0],
                 ["H1", "H", 2.47, 0, 0], ["H2", "H", 1.235, 2.139, 0], ["H3", "H", -1.235, 2.139, 0], ["H4", "H", -2.47, 0, 0], ["H5", "H", -1.235, -2.139, 0], ["H6", "H", 1.235, -2.139, 0]],
         bonds: [[1, 2, "aromatic"], [2, 3, "aromatic"], [3, 4, "aromatic"], [4, 5, "aromatic"], [5, 6, "aromatic"], [6, 1, "aromatic"],
                 [1, 7, "single"], [2, 8, "single"], [3, 9, "single"], [4, 10, "single"], [5, 11, "single"], [6, 12, "single"]]}
    ]

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(820, Overlay.overlay ? Overlay.overlay.width - 40 : 820)
    height: Math.min(620, Overlay.overlay ? Overlay.overlay.height - 60 : 620)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    ListModel { id: atomModel }
    ListModel { id: bondModel }
    property int revision: 0

    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    function numberOf(element) {
        const text = String(element).trim()
        if (/^\d+$/.test(text)) return parseInt(text, 10) > 0 && parseInt(text, 10) < 119 ? parseInt(text, 10) : -1
        const index = symbols.map(function(s) { return s.toLowerCase() }).indexOf(text.toLowerCase())
        return index >= 0 ? numbers[index] : -1
    }
    function openBuilder() {
        if (atomModel.count === 0) loadTemplate(0)
        open()
    }
    function loadTemplate(index) {
        const t = templates[index]
        atomModel.clear()
        bondModel.clear()
        nameField.text = t.name
        residueField.text = t.residue
        for (const a of t.atoms) atomModel.append({name: a[0], element: a[1], px: String(a[2]), py: String(a[3]), pz: String(a[4]), charge: "0"})
        for (const b of t.bonds) bondModel.append({first: String(b[0]), second: String(b[1]), order: b[2]})
        revision += 1
    }
    function touch() { revision += 1 }
    function isNumber(text) { return String(text).trim().length > 0 && isFinite(Number(text)) }
    // First problem found, or an empty string when the tables can be built.
    function problem() {
        revision
        if (nameField.text.trim().length === 0) return qsTr("Give the molecule a name.")
        if (atomModel.count === 0) return qsTr("Add at least one atom.")
        for (let i = 0; i < atomModel.count; ++i) {
            const a = atomModel.get(i)
            if (a.name.trim().length === 0) return qsTr("Atom %1 needs a name.").arg(i + 1)
            if (numberOf(a.element) < 0) return qsTr("Atom %1: unknown element.").arg(i + 1)
            if (!isNumber(a.px) || !isNumber(a.py) || !isNumber(a.pz) || !isNumber(a.charge)) return qsTr("Atom %1: position and charge must be numbers.").arg(i + 1)
        }
        for (let j = 0; j < bondModel.count; ++j) {
            const b = bondModel.get(j)
            const first = parseInt(b.first, 10), second = parseInt(b.second, 10)
            if (isNaN(first) || isNaN(second) || first < 1 || second < 1 || first > atomModel.count || second > atomModel.count)
                return qsTr("Bond %1: atom numbers must be between 1 and %2.").arg(j + 1).arg(atomModel.count)
            if (first === second) return qsTr("Bond %1 joins an atom to itself.").arg(j + 1)
        }
        if (!isNumber(residueNumberField.text)) return qsTr("The residue number must be a number.")
        return ""
    }
    function commandText() {
        const atoms = []
        for (let i = 0; i < atomModel.count; ++i) {
            const a = atomModel.get(i)
            atoms.push([a.name.trim(), numberOf(a.element), Number(a.px), Number(a.py), Number(a.pz), Number(a.charge)].join(","))
        }
        const bonds = []
        for (let j = 0; j < bondModel.count; ++j) {
            const b = bondModel.get(j)
            bonds.push([parseInt(b.first, 10), parseInt(b.second, 10), b.order].join(","))
        }
        let text = "build molecule --name " + quote(nameField.text.trim()) + " --atoms " + quote(atoms.join(";"))
        if (bonds.length > 0) text += " --bonds " + quote(bonds.join(";"))
        text += " --residue-name " + quote(residueField.text.trim().length > 0 ? residueField.text.trim() : "LIG")
            + " --chain " + quote(chainField.text.trim().length > 0 ? chainField.text.trim() : "A")
            + " --residue-number " + Math.round(Number(residueNumberField.text)) + " --unit " + unitBox.currentText
        return text
    }

    component Field: TextField {
        implicitHeight: 26
        implicitWidth: 70
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        placeholderTextColor: NextTheme.muted
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
    }
    component Caption: Text { color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; wrapMode: Text.Wrap }
    component Heading: Text { color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("MOLECULE BUILDER"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: builder.close() }
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
                    spacing: 6
                    Caption { text: qsTr("Template"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Repeater {
                        model: builder.templates
                        delegate: NextButton { required property var modelData; required property int index; text: modelData.label; onClicked: builder.loadTemplate(index) }
                    }
                }
                Flow {
                    width: parent.width
                    spacing: 8
                    Caption { text: qsTr("name"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: nameField; objectName: "builderName"; implicitWidth: 120; onTextChanged: builder.touch() }
                    Caption { text: qsTr("residue"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: residueField; implicitWidth: 56; text: "LIG" }
                    Caption { text: qsTr("chain"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: chainField; implicitWidth: 40; text: "A" }
                    Caption { text: qsTr("number"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: residueNumberField; implicitWidth: 50; text: "1"; onTextChanged: builder.touch() }
                    ComboBox {
                        id: unitBox
                        model: builder.units
                        implicitWidth: 120
                        implicitHeight: 26
                        font.pixelSize: NextTheme.fontSize
                        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: NextTheme.line }
                        contentItem: Text { leftPadding: 8; text: unitBox.displayText; color: NextTheme.text; font: unitBox.font; verticalAlignment: Text.AlignVCenter }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("Atoms") }
                Row {
                    spacing: 6
                    Caption { width: 28; text: "#" }
                    Caption { width: 70; text: qsTr("name") }
                    Caption { width: 70; text: qsTr("element") }
                    Caption { width: 70; text: "x" }
                    Caption { width: 70; text: "y" }
                    Caption { width: 70; text: "z" }
                    Caption { width: 56; text: qsTr("charge") }
                }
                Repeater {
                    model: atomModel
                    delegate: Row {
                        id: atomRow
                        required property int index
                        required property string name
                        required property string element
                        required property string px
                        required property string py
                        required property string pz
                        required property string charge
                        spacing: 6
                        Text { width: 28; height: 26; verticalAlignment: Text.AlignVCenter; text: atomRow.index + 1; color: NextTheme.muted; font.family: NextTheme.mono; font.pixelSize: 12 }
                        Field { text: atomRow.name; onTextEdited: { atomModel.setProperty(atomRow.index, "name", text); builder.touch() } }
                        Field { text: atomRow.element; onTextEdited: { atomModel.setProperty(atomRow.index, "element", text); builder.touch() } }
                        Field { text: atomRow.px; onTextEdited: { atomModel.setProperty(atomRow.index, "px", text); builder.touch() } }
                        Field { text: atomRow.py; onTextEdited: { atomModel.setProperty(atomRow.index, "py", text); builder.touch() } }
                        Field { text: atomRow.pz; onTextEdited: { atomModel.setProperty(atomRow.index, "pz", text); builder.touch() } }
                        Field { implicitWidth: 56; text: atomRow.charge; onTextEdited: { atomModel.setProperty(atomRow.index, "charge", text); builder.touch() } }
                        NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Remove atom"); onClicked: { atomModel.remove(atomRow.index); builder.touch() } }
                    }
                }
                Row {
                    spacing: 8
                    NextButton {
                        text: qsTr("Add atom")
                        onClicked: { atomModel.append({name: "C" + (atomModel.count + 1), element: "C", px: "0", py: "0", pz: "0", charge: "0"}); builder.touch() }
                    }
                    Caption { text: qsTr("element: symbol (C, N, Cl…) or atomic number"); height: 26; verticalAlignment: Text.AlignVCenter }
                }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("Bonds") }
                Repeater {
                    model: bondModel
                    delegate: Row {
                        id: bondRow
                        required property int index
                        required property string first
                        required property string second
                        required property string order
                        spacing: 6
                        Text { width: 28; height: 26; verticalAlignment: Text.AlignVCenter; text: bondRow.index + 1; color: NextTheme.muted; font.family: NextTheme.mono; font.pixelSize: 12 }
                        Field { implicitWidth: 56; text: bondRow.first; onTextEdited: { bondModel.setProperty(bondRow.index, "first", text); builder.touch() } }
                        Text { height: 26; verticalAlignment: Text.AlignVCenter; text: "–"; color: NextTheme.muted }
                        Field { implicitWidth: 56; text: bondRow.second; onTextEdited: { bondModel.setProperty(bondRow.index, "second", text); builder.touch() } }
                        ComboBox {
                            id: orderBox
                            model: builder.orders
                            currentIndex: Math.max(0, builder.orders.indexOf(bondRow.order))
                            implicitWidth: 110
                            implicitHeight: 26
                            font.pixelSize: NextTheme.fontSize
                            background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: NextTheme.line }
                            contentItem: Text { leftPadding: 8; text: orderBox.displayText; color: NextTheme.text; font: orderBox.font; verticalAlignment: Text.AlignVCenter }
                            onActivated: { bondModel.setProperty(bondRow.index, "order", currentText); builder.touch() }
                        }
                        NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Remove bond"); onClicked: { bondModel.remove(bondRow.index); builder.touch() } }
                    }
                }
                Row {
                    spacing: 8
                    NextButton { text: qsTr("Add bond"); onClicked: { bondModel.append({first: "1", second: "2", order: "single"}); builder.touch() } }
                    Caption { text: qsTr("atom numbers refer to the # column above"); height: 26; verticalAlignment: Text.AlignVCenter }
                }
                Text {
                    width: parent.width
                    text: builder.problem().length > 0 ? builder.problem() : builder.commandText()
                    wrapMode: Text.WrapAnywhere
                    font.family: builder.problem().length > 0 ? Qt.application.font.family : NextTheme.mono
                    font.pixelSize: builder.problem().length > 0 ? NextTheme.fontSize : 11
                    color: builder.problem().length > 0 ? NextTheme.error : NextTheme.muted
                    maximumLineCount: 4
                    elide: Text.ElideRight
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Item { Layout.fillWidth: true }
            NextButton { text: qsTr("Close"); onClicked: builder.close() }
            NextButton {
                objectName: "moleculeBuilderApplyButton"
                text: qsTr("Build molecule")
                checked: true
                enabled: builder.problem().length === 0
                onClicked: builder.run(builder.commandText())
            }
        }
    }
}
