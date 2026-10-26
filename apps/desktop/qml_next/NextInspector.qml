import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Inspector (SEQ-330 U5): representation, color and render-setting editing for a selection or the active object.
// Every button runs an ordinary command through `run`, so the output window and undo history show what happened;
// the Settings tab is generated from the typed render-setting catalog.
Popup {
    id: inspector
    objectName: "renderSettingsOverlay"
    property var viewport
    property string targetObject: ""
    // Bumped after each command so visible setting rows re-read their value.
    property int revision: 0
    property var catalog: []
    signal run(string text)

    modal: false
    focus: false
    padding: 0
    closePolicy: Popup.CloseOnEscape
    x: Overlay.overlay ? Overlay.overlay.width - width - 12 : 0
    y: 52
    width: 380
    height: Overlay.overlay ? Math.max(360, Overlay.overlay.height - 130) : 560
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    readonly property var representations: ["lines", "sticks", "spheres", "cartoon", "ribbon", "labels", "surface"]
    readonly property var swatches: ["red", "orange", "yellow", "green", "cyan", "blue", "magenta", "white", "grey"]
    readonly property var scopes: ["global", "object", "state"]

    function selection() {
        const text = target.text.trim()
        return text.length === 0 ? "all" : text
    }
    function quote(text) {
        return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\""
    }
    function command(text) {
        run(text)
        revision += 1
    }
    function openInspector(tab) {
        if (catalog.length === 0 && viewport) catalog = viewport.settingCatalog()
        if (tab !== undefined) tabs.currentIndex = tab
        filter.text = ""
        open()
    }
    function setSetting(name, value) {
        command("setting set --name " + name + " --value " + quote(value) + " --scope " + scopeBox.currentText)
    }
    function unsetSetting(name) {
        command("setting unset --name " + name + " --scope " + scopeBox.currentText)
    }
    function visibleSettings() {
        const q = filter.text.trim().toLowerCase()
        if (q.length === 0) return catalog
        return catalog.filter(function(entry) { return entry.name.toLowerCase().indexOf(q) >= 0 })
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text {
                text: qsTr("INSPECTOR")
                color: NextTheme.muted
                font.pixelSize: NextTheme.smallFont
                font.letterSpacing: 1.1
            }
            Text {
                Layout.fillWidth: true
                text: inspector.targetObject
                color: NextTheme.text
                font.pixelSize: NextTheme.fontSize
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: inspector.close() }
        }

        Text {
            objectName: "renderSettingResult"
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 4
            wrapMode: Text.Wrap
            color: NextTheme.muted
            font.pixelSize: NextTheme.smallFont
            text: qsTr("Settings tab: changes apply at the scope chosen there; the output window shows each command.")
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 8
            Text { text: qsTr("Selection"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
            TextField {
                id: target
                objectName: "inspectorTarget"
                Layout.fillWidth: true
                placeholderText: "all"
                placeholderTextColor: NextTheme.muted
                color: NextTheme.text
                font.pixelSize: NextTheme.fontSize
                selectByMouse: true
                implicitHeight: 26
                background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: target.activeFocus ? NextTheme.active : NextTheme.line }
                Accessible.name: qsTr("Selection")
            }
            NextButton { text: "sele"; onClicked: target.text = "sele"; ToolTip.text: qsTr("Use the selection (sele)") }
            NextButton { text: qsTr("all"); onClicked: target.text = ""; ToolTip.text: qsTr("Use every atom") }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            Layout.topMargin: 8
            background: Rectangle { color: "transparent" }
            Repeater {
                model: [qsTr("Representation"), qsTr("Color"), qsTr("Settings")]
                delegate: TabButton {
                    required property string modelData
                    required property int index
                    text: modelData
                    Accessible.name: modelData
                    font.pixelSize: NextTheme.fontSize
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: parent.checked ? NextTheme.text : NextTheme.muted
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 2; color: parent.parent.checked ? NextTheme.active : NextTheme.line }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---- Representation -------------------------------------------------------------------------------
            ListView {
                id: repList
                clip: true
                model: inspector.representations
                ScrollBar.vertical: ScrollBar { }
                delegate: Item {
                    id: repRow
                    required property string modelData
                    width: repList.width
                    height: 34
                    Text {
                        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                        text: repRow.modelData
                        color: NextTheme.text
                        font.pixelSize: NextTheme.fontSize
                    }
                    Row {
                        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        spacing: 6
                        NextButton {
                            text: qsTr("Show")
                            onClicked: inspector.command(repRow.modelData === "surface" ? "surface show" : "show " + repRow.modelData + ", " + inspector.selection())
                        }
                        NextButton {
                            text: qsTr("Hide")
                            onClicked: inspector.command(repRow.modelData === "surface" ? "surface hide" : "hide " + repRow.modelData + ", " + inspector.selection())
                        }
                        NextButton {
                            visible: repRow.modelData !== "surface"
                            text: qsTr("Only")
                            ToolTip.text: qsTr("Show only this representation")
                            onClicked: inspector.command("as " + repRow.modelData + ", " + inspector.selection())
                        }
                    }
                }
                footer: Item {
                    width: repList.width
                    height: 44
                    NextButton {
                        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                        text: qsTr("Hide everything")
                        onClicked: inspector.command("hide everything, " + inspector.selection())
                    }
                }
            }

            // ---- Color ----------------------------------------------------------------------------------------
            Flickable {
                clip: true
                contentHeight: colorColumn.implicitHeight + 20
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: colorColumn
                    x: 12
                    y: 10
                    width: parent.width - 24
                    spacing: 10
                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: inspector.swatches
                            delegate: NextButton {
                                required property string modelData
                                text: modelData
                                onClicked: inspector.command("color " + modelData + ", " + inspector.selection())
                            }
                        }
                    }
                    Flow {
                        width: parent.width
                        spacing: 6
                        NextButton { text: qsTr("by element (keep carbon)"); onClicked: inspector.command("color atomic, (" + inspector.selection() + ") and not elem C") }
                        NextButton { text: qsTr("by element (all atoms)"); onClicked: inspector.command("color atomic, " + inspector.selection()) }
                        NextButton { text: qsTr("spectrum by index"); onClicked: inspector.command("spectrum count, rainbow, " + inspector.selection()) }
                        NextButton { text: qsTr("spectrum by B-factor"); onClicked: inspector.command("spectrum b, rainbow, " + inspector.selection()) }
                    }
                    RowLayout {
                        width: parent.width
                        TextField {
                            id: customColor
                            Accessible.name: qsTr("name or 0xRRGGBB")
                            Layout.fillWidth: true
                            placeholderText: qsTr("name or 0xRRGGBB")
                            placeholderTextColor: NextTheme.muted
                            color: NextTheme.text
                            font.pixelSize: NextTheme.fontSize
                            selectByMouse: true
                            implicitHeight: 26
                            background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: customColor.activeFocus ? NextTheme.active : NextTheme.line }
                            onAccepted: applyButton.clicked()
                        }
                        NextButton {
                            id: applyButton
                            text: qsTr("Apply")
                            enabled: customColor.text.trim().length > 0
                            onClicked: inspector.command("color " + customColor.text.trim() + ", " + inspector.selection())
                        }
                    }
                }
            }

            // ---- Settings -------------------------------------------------------------------------------------
            ColumnLayout {
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    Layout.bottomMargin: 0
                    TextField {
                        id: filter
                        Layout.fillWidth: true
                        placeholderText: qsTr("Search settings")
                        placeholderTextColor: NextTheme.muted
                        color: NextTheme.text
                        font.pixelSize: NextTheme.fontSize
                        selectByMouse: true
                        implicitHeight: 26
                        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: filter.activeFocus ? NextTheme.active : NextTheme.line }
                        Accessible.name: qsTr("Search settings")
                        onTextChanged: settingList.model = inspector.visibleSettings()
                    }
                    ComboBox {
                        id: scopeBox
                        Accessible.name: qsTr("Scope")
                        model: inspector.scopes
                        currentIndex: 1
                        implicitWidth: 92
                        implicitHeight: 26
                        font.pixelSize: NextTheme.fontSize
                        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: scopeBox.activeFocus ? NextTheme.active : NextTheme.line }
                        contentItem: Text {
                            leftPadding: 8
                            text: scopeBox.displayText
                            color: NextTheme.text
                            font: scopeBox.font
                            verticalAlignment: Text.AlignVCenter
                        }
                        ToolTip.text: qsTr("Scope the change applies to")
                        ToolTip.visible: hovered
                        onActivated: inspector.revision += 1
                    }
                }
                ListView {
                    id: settingList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: inspector.catalog
                    ScrollBar.vertical: ScrollBar { }
                    delegate: Item {
                        id: settingRow
                        required property var modelData
                        readonly property var state: { inspector.revision; scopeBox.currentText; return inspector.viewport ? inspector.viewport.settingState(modelData.name, scopeBox.currentText) : ({}) }
                        readonly property bool overridden: state.overridden === true
                        width: settingList.width
                        height: 36
                        Text {
                            anchors { left: parent.left; leftMargin: 12; right: editor.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                            text: modelData.name + (modelData.unit ? "  (" + modelData.unit + ")" : "")
                            color: settingRow.overridden ? NextTheme.accent : NextTheme.text
                            font.pixelSize: NextTheme.fontSize
                            elide: Text.ElideRight
                            ToolTip.text: modelData.name + " · " + modelData.type + (modelData.minimum !== null ? " · min " + modelData.minimum : "") + (modelData.maximum !== null ? " · max " + modelData.maximum : "")
                            ToolTip.visible: nameHover.hovered
                            ToolTip.delay: 600
                            HoverHandler { id: nameHover }
                        }
                        Row {
                            id: editor
                            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                            spacing: 6
                            Loader {
                                anchors.verticalCenter: parent.verticalCenter
                                sourceComponent: settingRow.modelData.type === "boolean" ? boolEditor : textEditor
                            }
                            NextButton {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "↺"
                                outlined: false
                                width: 24
                                opacity: settingRow.overridden ? 1 : 0
                                enabled: settingRow.overridden
                                ToolTip.text: qsTr("Remove this override")
                                onClicked: inspector.unsetSetting(settingRow.modelData.name)
                            }
                        }
                        Component {
                            id: boolEditor
                            Switch {
                                Accessible.name: settingRow.modelData.name
                                checked: settingRow.state.value === true
                                onToggled: inspector.setSetting(settingRow.modelData.name, checked ? "true" : "false")
                            }
                        }
                        Component {
                            id: textEditor
                            TextField {
                                Accessible.name: settingRow.modelData.name
                                width: 110
                                implicitHeight: 24
                                text: {
                                    const v = settingRow.state.value
                                    if (v === undefined || v === null) return ""
                                    return Array.isArray(v) ? v.join(", ") : String(v)
                                }
                                color: NextTheme.text
                                font.pixelSize: NextTheme.fontSize
                                selectByMouse: true
                                background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
                                onEditingFinished: {
                                    const current = settingRow.state.value
                                    const before = current === undefined || current === null ? "" : (Array.isArray(current) ? current.join(", ") : String(current))
                                    if (text.trim().length > 0 && text.trim() !== before) inspector.setSetting(settingRow.modelData.name, text.trim())
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
