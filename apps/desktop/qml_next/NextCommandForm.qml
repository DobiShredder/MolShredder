import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// "All commands" dialog: a searchable list of every registered command and a form generated from its
// parameter definitions (type, default, choices, required). Running it executes `name --option value ...`.
Popup {
    id: dialog
    property var viewport
    property var catalog: []
    property var current: null
    property var values: ({})
    signal run(string text)

    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(860, Overlay.overlay ? Overlay.overlay.width - 40 : 860)
    height: Math.min(560, Overlay.overlay ? Overlay.overlay.height - 40 : 560)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 10 }

    function openFor(name, preset) {
        if (catalog.length === 0 && viewport) catalog = viewport.commandCatalog()
        search.text = ""
        select(name || "", preset || ({}))
        open()
        if (!name) search.forceActiveFocus()
    }
    function select(name, preset) {
        current = null
        for (const entry of catalog) if (entry.name === name) current = entry
        const next = ({})
        if (current) {
            for (const p of current.parameters) next[p.name] = preset && preset[p.name] !== undefined ? String(preset[p.name]) : (p.default === null || p.default === undefined ? "" : String(p.default))
        }
        values = next
    }
    function quote(text) {
        return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\""
    }
    function commandText() {
        if (!current) return ""
        let text = current.name
        for (const p of current.parameters) {
            const v = values[p.name]
            if (v === undefined || v === "") continue
            if (!p.required && p.default !== null && p.default !== undefined && String(p.default) === v) continue
            text += " --" + p.name + " " + quote(v)
        }
        return text
    }
    function missingRequired() {
        if (!current) return ""
        for (const p of current.parameters) if (p.required && (values[p.name] === undefined || values[p.name] === "")) return p.name
        return ""
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.preferredWidth: 270
            Layout.fillHeight: true
            spacing: 0
            TextField {
                id: search
                Layout.fillWidth: true
                Layout.margins: 10
                placeholderText: qsTr("Search commands")
                placeholderTextColor: NextTheme.muted
                color: NextTheme.text
                font.pixelSize: NextTheme.fontSize
                background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: search.activeFocus ? NextTheme.active : NextTheme.line }
                Accessible.name: qsTr("Search commands")
                onTextChanged: list.model = dialog.filtered(text)
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: dialog.catalog
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    required property var modelData
                    width: list.width
                    height: 28
                    color: dialog.current && dialog.current.name === modelData.name ? NextTheme.activeSoft : (rowHover.hovered ? NextTheme.chrome : "transparent")
                    HoverHandler { id: rowHover }
                    Text {
                        anchors { left: parent.left; leftMargin: 12; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        text: modelData.name
                        elide: Text.ElideRight
                        color: NextTheme.text
                        font.pixelSize: NextTheme.fontSize
                    }
                    TapHandler { onTapped: dialog.select(modelData.name, ({})) }
                }
            }
        }
        Rectangle { Layout.fillHeight: true; width: 1; color: NextTheme.line }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14
            spacing: 8
            Text {
                text: dialog.current ? dialog.current.name : qsTr("Pick a command")
                color: NextTheme.text
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: dialog.current ? dialog.current.summary : qsTr("Every registered command is listed on the left. The form is generated from its parameter definitions.")
                wrapMode: Text.Wrap
                color: NextTheme.muted
                font.pixelSize: NextTheme.fontSize
            }
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: form.implicitHeight
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: form
                    width: parent.width - 12
                    spacing: 8
                    Repeater {
                        model: dialog.current ? dialog.current.parameters : []
                        delegate: Item {
                            id: field
                            required property var modelData
                            width: form.width
                            height: 46
                            Text {
                                id: caption
                                text: modelData.name + (modelData.required ? " *" : "")
                                color: NextTheme.text
                                font.pixelSize: NextTheme.smallFont
                                font.weight: Font.DemiBold
                            }
                            Loader {
                                anchors { top: caption.bottom; topMargin: 3; left: parent.left; right: parent.right }
                                sourceComponent: modelData.choices.length > 0 ? choiceField : (modelData.type === "boolean" ? boolField : textField)
                            }
                            Component {
                                id: textField
                                TextField {
                                    text: dialog.values[field.modelData.name] !== undefined ? dialog.values[field.modelData.name] : ""
                                    placeholderText: field.modelData.type === "text" ? "" : field.modelData.type
                                    placeholderTextColor: NextTheme.muted
                                    color: NextTheme.text
                                    font.pixelSize: NextTheme.fontSize
                                    selectByMouse: true
                                    height: 26
                                    background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
                                    onTextEdited: { const v = dialog.values; v[field.modelData.name] = text; dialog.values = v }
                                }
                            }
                            Component {
                                id: choiceField
                                ComboBox {
                                    model: field.modelData.choices
                                    height: 26
                                    currentIndex: Math.max(0, field.modelData.choices.indexOf(dialog.values[field.modelData.name]))
                                    font.pixelSize: NextTheme.fontSize
                                    onActivated: { const v = dialog.values; v[field.modelData.name] = currentText; dialog.values = v }
                                }
                            }
                            Component {
                                id: boolField
                                Switch {
                                    checked: dialog.values[field.modelData.name] === "true"
                                    onToggled: { const v = dialog.values; v[field.modelData.name] = checked ? "true" : "false"; dialog.values = v }
                                }
                            }
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: dialog.commandText()
                visible: dialog.current !== null
                wrapMode: Text.WrapAnywhere
                font.family: NextTheme.mono
                font.pixelSize: 12
                color: NextTheme.muted
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    visible: dialog.missingRequired() !== ""
                    text: qsTr("--%1 is required").arg(dialog.missingRequired())
                    color: NextTheme.error
                    font.pixelSize: NextTheme.smallFont
                }
                Item { Layout.fillWidth: true; visible: dialog.missingRequired() === "" }
                NextButton { text: qsTr("Close"); onClicked: dialog.close() }
                NextButton {
                    text: qsTr("Run")
                    checked: true
                    enabled: dialog.current !== null && dialog.missingRequired() === ""
                    onClicked: { dialog.run(dialog.commandText()); dialog.close() }
                }
            }
        }
    }

    function filtered(query) {
        const q = String(query).trim().toLowerCase()
        if (q.length === 0) return catalog
        return catalog.filter(function(entry) { return entry.name.toLowerCase().indexOf(q) >= 0 || entry.summary.toLowerCase().indexOf(q) >= 0 })
    }
}
