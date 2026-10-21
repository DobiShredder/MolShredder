import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Selection by expression (SEQ-330 G1): a named selection of the active object with live validation and an atom
// count preview, the selection keywords, and the selections that already exist. Defining runs `select`.
Popup {
    id: editor
    objectName: "selectionExpressionOverlay"
    property var viewport
    property var keywords: []
    property var preview: ({ok: false, message: ""})
    signal run(string text)

    readonly property var objectRows: viewport ? viewport.objectTree : []
    readonly property string activeObject: {
        for (const row of objectRows) if (row.kind === "object" && row.active) return row.name
        return ""
    }
    readonly property var existing: {
        const list = []
        for (const row of objectRows) if (row.kind === "selection" && row.owner === activeObject) list.push(row)
        return list
    }

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(700, Overlay.overlay ? Overlay.overlay.width - 40 : 700)
    height: Math.min(460, Overlay.overlay ? Overlay.overlay.height - 60 : 460)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    function openEditor(name, expression) {
        if (keywords.length === 0 && viewport) keywords = viewport.selectionKeywords()
        if (name !== undefined) nameField.text = name
        if (expression !== undefined) expressionField.text = expression
        keywordSearch.text = ""
        refreshPreview()
        open()
        expressionField.forceActiveFocus()
    }
    function refreshPreview() {
        preview = viewport ? viewport.selectionPreview(expressionField.text) : ({ok: false, message: ""})
    }
    function define() {
        const name = nameField.text.trim().length > 0 ? nameField.text.trim() : "sele"
        let text = "select --name " + quote(name) + " --expression " + quote(expressionField.text.trim())
        if (dynamicBox.checked) text += " --update true"
        if (mergeBox.checked) text += " --merge true"
        run(text)
    }
    function insert(text) {
        const position = expressionField.cursorPosition
        const before = expressionField.text.substring(0, position)
        const after = expressionField.text.substring(position)
        const glue = before.length > 0 && !/\s$/.test(before) && before[before.length - 1] !== "(" ? " " : ""
        expressionField.text = before + glue + text + after
        expressionField.cursorPosition = (before + glue + text).length
        expressionField.forceActiveFocus()
        refreshPreview()
    }
    function filteredKeywords() {
        const q = keywordSearch.text.trim().toLowerCase()
        if (q.length === 0) return keywords
        return keywords.filter(function(k) { return k.keyword.toLowerCase().indexOf(q) >= 0 || String(k.aliases).toLowerCase().indexOf(q) >= 0 })
    }

    component Field: TextField {
        // Said by screen readers; give each field a `label:` where no placeholder tells what it is.
        property string label: ""
        property string derivedLabel: ""
        Accessible.name: label.length > 0 ? label : (placeholderText.length > 0 ? placeholderText : derivedLabel)
        Item { visible: false; width: 0; height: 0; Component.onCompleted: parent.derivedLabel = NextTheme.captionBefore(parent) }
        implicitHeight: 28
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        placeholderTextColor: NextTheme.muted
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

    Timer { id: debounce; interval: 200; onTriggered: editor.refreshPreview() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("SELECT BY EXPRESSION"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1 }
            Text { Layout.fillWidth: true; text: editor.activeObject; color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold; elide: Text.ElideRight }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: editor.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 12
                Layout.topMargin: 0
                spacing: 8
                Text { text: qsTr("Name"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
                Field { id: nameField; objectName: "selectionName"; Layout.fillWidth: true; text: "sele" }
                Text { text: qsTr("Expression"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
                Field {
                    id: expressionField
                    objectName: "selectionExpression"
                    Layout.fillWidth: true
                    font.family: NextTheme.mono
                    placeholderText: qsTr("polymer.protein and b < 30")
                    onTextChanged: debounce.restart()
                    onAccepted: if (editor.preview.ok) editor.define()
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: NextTheme.fontSize
                    color: editor.preview.ok ? NextTheme.ok : NextTheme.error
                    text: editor.preview.ok
                          ? qsTr("%1 of %2 atoms").arg(editor.preview.atomCount).arg(editor.preview.objectAtomCount)
                          : editor.preview.message
                }
                Check { id: dynamicBox; text: qsTr("Update with trajectory frame") }
                Check { id: mergeBox; text: qsTr("Add to the selection if it exists") }
                RowLayout {
                    spacing: 8
                    NextButton { text: qsTr("Define"); checked: true; enabled: editor.preview.ok; onClicked: editor.define() }
                    NextButton { text: qsTr("Close"); onClicked: editor.close() }
                }
                Text { text: qsTr("Existing selections"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
                ListView {
                    id: existingList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: editor.existing
                    ScrollBar.vertical: ScrollBar { }
                    delegate: Rectangle {
                        required property var modelData
                        width: existingList.width
                        height: 26
                        color: rowHover.hovered ? NextTheme.chrome : "transparent"
                        HoverHandler { id: rowHover }
                        Text {
                            anchors { left: parent.left; leftMargin: 6; right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                            text: modelData.name + "  ·  " + modelData.expression + (modelData.dynamic ? "  (dynamic)" : "")
                            color: NextTheme.text
                            font.family: NextTheme.mono
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        TapHandler { onTapped: { nameField.text = modelData.name; expressionField.text = modelData.expression; dynamicBox.checked = modelData.dynamic; editor.refreshPreview() } }
                        ToolTip.visible: rowHover.hovered
                        ToolTip.text: qsTr("Load into the editor")
                        ToolTip.delay: 500
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: existingList.count === 0
                        text: qsTr("none yet")
                        color: NextTheme.muted
                        font.pixelSize: NextTheme.smallFont
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: NextTheme.line }

            ColumnLayout {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                Layout.margins: 12
                Layout.topMargin: 0
                spacing: 6
                Field {
                    id: keywordSearch
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search keywords")
                    Accessible.name: qsTr("Search keywords")
                    onTextChanged: keywordList.model = editor.filteredKeywords()
                }
                ListView {
                    id: keywordList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: editor.keywords
                    ScrollBar.vertical: ScrollBar { }
                    delegate: Rectangle {
                        required property var modelData
                        width: keywordList.width
                        height: 40
                        color: keyHover.hovered ? NextTheme.chrome : "transparent"
                        HoverHandler { id: keyHover }
                        Column {
                            anchors { left: parent.left; leftMargin: 6; right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                            Text { width: parent.width; text: modelData.keyword + "  · " + modelData.kind; color: NextTheme.text; font.pixelSize: NextTheme.fontSize; elide: Text.ElideRight }
                            Text { width: parent.width; text: modelData.example; color: NextTheme.muted; font.family: NextTheme.mono; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                        TapHandler { onTapped: editor.insert(modelData.keyword) }
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: editor.insert(modelData.example) }
                        ToolTip.visible: keyHover.hovered
                        ToolTip.text: qsTr("Click inserts the keyword, right-click inserts the example") + (String(modelData.aliases).length > 0 ? "\n" + qsTr("aliases: %1").arg(modelData.aliases) : "")
                        ToolTip.delay: 500
                    }
                }
            }
        }
    }
}
