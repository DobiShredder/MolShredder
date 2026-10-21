import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Chemistry (SEQ-330 G1): what the active object says about its chemistry (`object chemistry`) and what the
// conservative perception rules would add (`object perceive-chemistry`). Analyzing never changes the object;
// Apply runs the same command with --apply; Edit ▸ Undo does not revert it (it is not a molecular edit).
Popup {
    id: panel
    property var viewport
    property var semantics: ({})
    property var proposal: ({})
    property bool analyzed: false
    property string failure: ""
    signal run(string text)

    readonly property var groups: [
        {title: qsTr("Charges and isotopes"), items: ["formal_charge_present", "partial_charge_present", "isotope_atom", "radical_atom", "atom_stereo"]},
        {title: qsTr("Bonds by order"), items: ["bond_single", "bond_double", "bond_triple", "bond_aromatic", "bond_amide", "bond_zero", "bond_query", "bond_unknown", "bond_stereo"]},
        {title: qsTr("Where the information came from"), items: ["explicit_atom_annotation", "inferred_atom_annotation", "user_atom_annotation", "explicit_bond_annotation", "inferred_bond_annotation", "user_bond_annotation", "explicit_residue_annotation", "inferred_residue_annotation", "user_residue_annotation"]},
        {title: qsTr("Residue kinds"), items: ["residue_amino_acid", "residue_nucleic_acid", "residue_carbohydrate", "residue_solvent", "residue_ion", "residue_ligand", "residue_unknown"]},
        {title: qsTr("Polymers"), items: ["polymer_protein", "polymer_dna", "polymer_rna", "polymer_carbohydrate", "polymer_other"]}
    ]

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(820, Overlay.overlay ? Overlay.overlay.width - 40 : 820)
    height: Math.min(600, Overlay.overlay ? Overlay.overlay.height - 60 : 600)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function openPanel() {
        refresh()
        open()
    }
    function counts() {
        const map = ({})
        const table = semantics.table
        if (table) for (const row of table.rows) map[row[0]] = row[1]
        return map
    }
    function label(key) { return key.replace(/_/g, " ") }
    function refresh() {
        failure = ""
        semantics = viewport ? viewport.commandData("object chemistry", ({})) : ({})
        if (!semantics.table) failure = qsTr("No active object.")
        analyzed = false
        proposal = ({})
    }
    function options() {
        return {"infer-connectivity": inferBox.checked ? "true" : "false", "radius-scale": scaleField.text.trim(), "pair-budget": budgetField.text.trim()}
    }
    function analyze() {
        proposal = viewport.commandData("object perceive-chemistry", options())
        analyzed = proposal.chemical_perception_schema_version !== undefined
        failure = analyzed ? "" : qsTr("The rules could not run with these options.")
    }
    function apply() {
        const o = options()
        run("object perceive-chemistry --infer-connectivity " + o["infer-connectivity"] + " --radius-scale " + o["radius-scale"]
            + " --pair-budget " + o["pair-budget"] + " --apply true")
        refresh()
        analyze()
    }
    function proposalRows() {
        const table = proposal.table
        if (!table) return []
        return table.rows
    }
    function validOptions() {
        const s = parseFloat(scaleField.text), b = parseInt(budgetField.text, 10)
        return !isNaN(s) && s > 0 && !isNaN(b) && b > 0
    }

    component Field: TextField {
        // Said by screen readers; give each field a `label:` where no placeholder tells what it is.
        property string label: ""
        property string derivedLabel: ""
        Accessible.name: label.length > 0 ? label : (placeholderText.length > 0 ? placeholderText : derivedLabel)
        Item { visible: false; width: 0; height: 0; Component.onCompleted: parent.derivedLabel = NextTheme.captionBefore(parent) }
        implicitHeight: 26
        implicitWidth: 90
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
    }
    component Check: CheckBox {
        id: check
        Accessible.name: text
        font.pixelSize: NextTheme.fontSize
        implicitHeight: 24
        indicator: Rectangle {
            y: (check.height - height) / 2
            width: 16; height: 16; radius: 3
            color: check.checked ? NextTheme.activeSoft : NextTheme.chrome
            border.color: check.activeFocus ? NextTheme.active : NextTheme.line
            Text { anchors.centerIn: parent; visible: check.checked; text: "✓"; color: NextTheme.active; font.pixelSize: 12 }
        }
        contentItem: Text { leftPadding: 22; text: check.text; color: NextTheme.text; font: check.font; verticalAlignment: Text.AlignVCenter }
    }
    component Caption: Text { color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; wrapMode: Text.Wrap }
    component Heading: Text { color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("CHEMISTRY"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1 }
            Text { Layout.fillWidth: true; text: panel.semantics.object_name !== undefined ? panel.semantics.object_name : ""; color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold; elide: Text.ElideRight }
            NextButton { text: qsTr("Refresh"); onClicked: panel.refresh() }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            visible: panel.failure.length > 0
            text: panel.failure
            color: NextTheme.error
            font.pixelSize: NextTheme.fontSize
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- what the object says ------------------------------------------------------------------------
            Flickable {
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                clip: true
                contentHeight: semanticsBody.implicitHeight + 16
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: semanticsBody
                    x: 14; y: 4
                    width: parent.width - 28
                    spacing: 6
                    Heading { text: qsTr("What the object says") }
                    Caption {
                        width: parent.width
                        text: qsTr("%1 atoms · %2 bonds · %3 residues").arg(panel.semantics.atom_count !== undefined ? panel.semantics.atom_count : 0)
                                  .arg(panel.semantics.bond_count !== undefined ? panel.semantics.bond_count : 0)
                                  .arg(panel.semantics.residue_count !== undefined ? panel.semantics.residue_count : 0)
                    }
                    Check { id: zerosBox; text: qsTr("Show zero counts") }
                    Repeater {
                        model: panel.groups
                        delegate: Column {
                            id: groupColumn
                            required property var modelData
                            readonly property var shown: {
                                const counts = panel.counts()
                                return modelData.items.filter(function(key) { return zerosBox.checked || (counts[key] !== undefined && counts[key] > 0) })
                            }
                            visible: shown.length > 0
                            width: semanticsBody.width
                            spacing: 2
                            Caption { text: groupColumn.modelData.title; font.weight: Font.DemiBold }
                            Repeater {
                                model: groupColumn.shown
                                delegate: Row {
                                    required property string modelData
                                    spacing: 8
                                    Text { width: 220; text: panel.label(modelData); color: NextTheme.text; font.pixelSize: NextTheme.fontSize; elide: Text.ElideRight }
                                    Text { text: panel.counts()[modelData]; color: NextTheme.text; font.family: NextTheme.mono; font.pixelSize: 12 }
                                }
                            }
                        }
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: NextTheme.line }

            // ---- what the rules would add --------------------------------------------------------------------
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: proposalBody.implicitHeight + 16
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: proposalBody
                    x: 14; y: 4
                    width: parent.width - 28
                    spacing: 8
                    Heading { text: qsTr("What the rules would add") }
                    Caption { width: parent.width; text: qsTr("Conservative rules propose missing bonds from distances, ring and aromatic marks and residue kinds. Nothing changes until you apply.") }
                    Check { id: inferBox; text: qsTr("Infer missing bonds from distances"); checked: true }
                    Row {
                        spacing: 8
                        Caption { text: qsTr("radius scale"); height: 26; verticalAlignment: Text.AlignVCenter }
                        Field { id: scaleField; text: "1.2"; implicitWidth: 60 }
                        Caption { text: qsTr("pair budget"); height: 26; verticalAlignment: Text.AlignVCenter }
                        Field { id: budgetField; text: "2000000"; implicitWidth: 100 }
                    }
                    Row {
                        spacing: 8
                        NextButton { text: qsTr("Analyze"); checked: true; enabled: panel.validOptions() && panel.semantics.table !== undefined; onClicked: panel.analyze() }
                        NextButton {
                            text: qsTr("Apply proposal")
                            enabled: panel.analyzed && (panel.proposal.proposed_bond_count > 0 || panel.proposal.proposed_bond_order_change_count > 0 || panel.proposal.proposed_residue_count > 0)
                            onClicked: panel.apply()
                        }
                    }
                    Caption { width: parent.width; visible: panel.analyzed; text: qsTr("Applying cannot be undone with Undo; reload the structure to start over.") }
                    Column {
                        visible: panel.analyzed
                        width: parent.width
                        spacing: 3
                        Repeater {
                            model: [
                                {key: "proposed_bond_count", text: qsTr("proposed new bonds")},
                                {key: "proposed_bond_order_change_count", text: qsTr("proposed bond-order changes")},
                                {key: "proposed_residue_count", text: qsTr("residues classified")},
                                {key: "ring_atom_count", text: qsTr("ring atoms")},
                                {key: "ring_bond_count", text: qsTr("ring bonds")},
                                {key: "aromatic_ring_edge_count", text: qsTr("aromatic ring edges")},
                                {key: "implicit_hydrogen_count", text: qsTr("implicit hydrogens")},
                                {key: "unsupported_valence_atom_count", text: qsTr("atoms with unsupported valence")},
                                {key: "evaluated_pair_count", text: qsTr("atom pairs examined")}
                            ]
                            delegate: Row {
                                required property var modelData
                                spacing: 8
                                Text { width: 230; text: modelData.text; color: NextTheme.text; font.pixelSize: NextTheme.fontSize }
                                Text {
                                    text: panel.proposal[modelData.key] !== undefined ? String(panel.proposal[modelData.key]) : ""
                                    color: modelData.key === "unsupported_valence_atom_count" && panel.proposal[modelData.key] > 0 ? NextTheme.accent : NextTheme.text
                                    font.family: NextTheme.mono
                                    font.pixelSize: 12
                                }
                            }
                        }
                        Repeater {
                            model: panel.proposal.warnings ? panel.proposal.warnings : []
                            delegate: Text { required property string modelData; width: proposalBody.width; text: "⚠ " + modelData; wrapMode: Text.Wrap; color: NextTheme.accent; font.pixelSize: NextTheme.smallFont }
                        }
                        Caption {
                            visible: panel.proposalRows().length > 0
                            text: qsTr("Proposed bonds") + (panel.proposalRows().length > 100 ? " · " + qsTr("first %1 of %2").arg(100).arg(panel.proposalRows().length) : "")
                            font.weight: Font.DemiBold
                        }
                        Repeater {
                            model: panel.proposalRows().slice(0, 100)
                            delegate: Text {
                                required property var modelData
                                text: modelData[0] + " – " + modelData[1] + "  " + modelData[2] + "  " + Number(modelData[3]).toFixed(2) + " Å  (" + modelData[4] + ")"
                                color: NextTheme.text
                                font.family: NextTheme.mono
                                font.pixelSize: 12
                            }
                        }
                        Caption { text: qsTr("Assumptions"); font.weight: Font.DemiBold }
                        Repeater {
                            model: panel.proposal.assumptions ? panel.proposal.assumptions : []
                            delegate: Text { required property string modelData; width: proposalBody.width; text: "• " + modelData; wrapMode: Text.Wrap; color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
                        }
                    }
                }
            }
        }
    }
}
