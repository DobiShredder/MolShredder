import QtQuick
import QtQuick.Controls.Basic

// Right-hand object list: one row per object with an enable dot, the name and the A S H L C menu buttons.
Rectangle {
    id: panel
    property var viewport
    property var rows: viewport ? viewport.objectTree : []
    property string mouseMode: "viewing"
    signal command(string text)
    // Runs `text` after making `owner` the active object (selections belong to one object).
    signal commandIn(string owner, string text)
    signal formRequested(string name, var preset)
    signal renameRequested(string name)
    signal mouseModeRequested(string mode)
    color: NextTheme.panel

    readonly property var mouseModes: [
        {id: "viewing", label: qsTr("Viewing"), tip: qsTr("Left rotate, middle or Shift+left move, right zoom, wheel clip. A click picks an atom.")},
        {id: "select-atoms", label: qsTr("Atoms"), tip: qsTr("A click selects an atom into (sele). Ctrl-click adds to it.")},
        {id: "select-residues", label: qsTr("Residues"), tip: qsTr("A click selects the whole residue into (sele). Ctrl-click adds to it.")},
        {id: "select-chains", label: qsTr("Chains"), tip: qsTr("A click selects the whole chain into (sele). Ctrl-click adds to it.")},
        {id: "select-objects", label: qsTr("Objects"), tip: qsTr("A click selects the whole object into (sele).")}
    ]
    function modeLabel(id) {
        for (const mode of mouseModes) if (mode.id === id) return mode.id === "viewing" ? qsTr("3-button viewing") : qsTr("selecting %1").arg(mode.label.toLowerCase())
        return id
    }
    readonly property var reps: ["lines", "sticks", "spheres", "ribbon", "cartoon"]

    function menuFor(kind, name, rowKind) {
        const sel = name
        if (rowKind === "group") {
            return [
                {text: qsTr("open"), command: "group open --name " + name},
                {text: qsTr("close"), command: "group close --name " + name},
                {separator: true},
                {text: qsTr("show all members"), command: "enable " + name},
                {text: qsTr("hide all members"), command: "disable " + name},
                {separator: true},
                {text: qsTr("ungroup"), command: "group ungroup --name " + name}
            ]
        }
        if (kind === "A" && rowKind === "selection") {
            return [
                {text: qsTr("zoom"), command: "zoom " + sel},
                {text: qsTr("center"), command: "center " + sel},
                {text: qsTr("orient"), command: "orient " + sel},
                {separator: true},
                {text: qsTr("create object…"), form: "object create", preset: {"selection": "@" + name}},
                {text: qsTr("extract to object…"), form: "object extract", preset: {"selection": "@" + name}},
                {separator: true},
                {text: qsTr("delete selection"), command: "delete " + sel}
            ]
        }
        if (kind === "A") {
            return [
                {text: qsTr("zoom"), command: "zoom " + sel},
                {text: qsTr("center"), command: "center " + sel},
                {text: qsTr("orient"), command: "orient " + sel},
                {separator: true},
                {text: qsTr("rename…"), action: "rename", name: name},
                {text: qsTr("duplicate…"), form: "object copy", preset: {"object": name}},
                {text: qsTr("delete"), command: "delete " + sel},
                {separator: true},
                {text: qsTr("align to…"), form: "object align", preset: {"mobile-object": name}},
                {text: qsTr("split states"), command: "object split-states --object " + name},
                {text: qsTr("fix"), command: "object fix --object " + name},
                {text: qsTr("free"), command: "object free --object " + name},
                {separator: true},
                {text: qsTr("analyze…"), form: "analyze center", preset: {"selection": sel}}
            ]
        }
        if (kind === "S" || kind === "H") {
            const verb = kind === "S" ? "show " : "hide "
            const items = reps.map(function(r) { return {text: r, command: verb + r + ", " + sel} })
            items.push({separator: true})
            if (kind === "S") {
                items.push({text: qsTr("surface"), command: "surface show"})
                items.push({text: qsTr("only lines"), command: "as lines, " + sel})
                items.push({text: qsTr("only cartoon"), command: "as cartoon, " + sel})
            } else {
                items.push({text: qsTr("surface"), command: "surface hide"})
                items.push({text: qsTr("everything"), command: "hide everything, " + sel})
            }
            return items
        }
        if (kind === "L") {
            return [
                {text: qsTr("atom name"), command: "label " + sel + ", name"},
                {text: qsTr("residue name"), command: "label " + sel + ", resn"},
                {text: qsTr("element"), command: "label " + sel + ", elem"},
                {text: qsTr("B-factor"), command: "label " + sel + ", b"},
                {separator: true},
                {text: qsTr("clear"), command: "label " + sel + ", \"\""}
            ]
        }
        const colors = ["red", "orange", "yellow", "green", "cyan", "blue", "magenta", "white", "grey"]
        const entries = [
            {text: qsTr("by element"), command: "recolor"},
            {text: qsTr("spectrum by index"), command: "spectrum count, rainbow, " + sel},
            {text: qsTr("spectrum by B-factor"), command: "spectrum b, rainbow, " + sel},
            {separator: true}
        ]
        colors.forEach(function(c) { entries.push({text: c, command: "color " + c + ", " + sel}) })
        entries.push({separator: true})
        entries.push({text: qsTr("other…"), form: "color", preset: {"selection": sel}})
        return entries
    }

    // Opens the menu of the first object row (used by the `ui:objectmenu` test hook).
    function openMenu(kind) {
        if (!rows || rows.length === 0) return
        let row = rows[0]
        for (const candidate of rows) if (candidate.kind === (kind === "G" ? "group" : "object")) { row = candidate; break }
        popup.entries = menuFor(kind === "G" ? "A" : kind, row.name, row.kind)
        popup.popup(list, list.width - 150, 30)
    }

    NextMenu {
        id: popup
        property string owner: ""
        onPicked: function(entry) {
            if (entry.command && owner.length > 0) panel.commandIn(owner, entry.command)
            else if (entry.command) panel.command(entry.command)
            else if (entry.form) panel.formRequested(entry.form, entry.preset || ({}))
            else if (entry.action === "rename") panel.renameRequested(entry.name)
        }
    }

    Rectangle {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 30
        color: NextTheme.panel
        Text {
            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
            text: qsTr("OBJECTS")
            font.pixelSize: NextTheme.smallFont
            font.letterSpacing: 1.1
            color: NextTheme.muted
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: NextTheme.line }
    }

    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: mouseBox.top }
        clip: true
        model: panel.rows
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }
        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property bool isObject: modelData.kind === "object"
            readonly property bool isGroup: modelData.kind === "group"
            readonly property bool isSelection: modelData.kind === "selection"
            width: list.width
            height: NextTheme.rowHeight
            color: modelData.active === true ? NextTheme.activeSoft : (hover.hovered ? NextTheme.chrome : "transparent")
            HoverHandler { id: hover }

            Text {
                id: fold
                x: 6 + modelData.depth * 14
                width: 12
                anchors.verticalCenter: parent.verticalCenter
                text: isGroup ? (modelData.open ? "▾" : "▸") : ""
                color: NextTheme.muted
                font.pixelSize: 11
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    enabled: isGroup
                    onClicked: panel.command("group toggle --name " + modelData.name)
                }
            }
            Rectangle {
                id: dot
                visible: !isSelection
                anchors { left: fold.right; leftMargin: 4; verticalCenter: parent.verticalCenter }
                width: 12; height: 12; radius: 6
                color: modelData.visible ? NextTheme.active : "transparent"
                border.width: 2
                border.color: NextTheme.active
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -5
                    onClicked: panel.command((modelData.visible ? "disable " : "enable ") + modelData.name)
                }
            }
            Text {
                anchors { left: isSelection ? fold.right : dot.right; leftMargin: 8; right: buttons.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
                text: isSelection ? "(" + modelData.name + ")" : modelData.name
                elide: Text.ElideRight
                color: isSelection ? NextTheme.accent : (modelData.visible ? NextTheme.text : NextTheme.muted)
                font.pixelSize: NextTheme.fontSize
                font.italic: isSelection
                font.weight: modelData.active === true || isGroup ? Font.DemiBold : Font.Normal
                ToolTip.visible: isSelection && nameArea.containsMouse
                ToolTip.text: isSelection ? modelData.expression + (modelData.dynamic ? "  (dynamic)" : "") : ""
                MouseArea {
                    id: nameArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (isObject) panel.command("object activate --object " + modelData.name)
                        else if (isGroup) panel.command("group toggle --name " + modelData.name)
                    }
                    onDoubleClicked: if (isObject) panel.renameRequested(modelData.name)
                }
            }
            Row {
                id: buttons
                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                spacing: 2
                Repeater {
                    model: isGroup ? ["A"] : ["A", "S", "H", "L", "C"]
                    delegate: NextButton {
                        required property string modelData
                        text: modelData
                        width: 22; height: 22
                        implicitWidth: 22
                        pixelSize: NextTheme.smallFont
                        ToolTip.text: ({A: qsTr("Actions"), S: qsTr("Show"), H: qsTr("Hide"), L: qsTr("Label"), C: qsTr("Color")})[modelData]
                        onClicked: {
                            popup.owner = row.isSelection ? row.modelData.owner : ""
                            popup.entries = panel.menuFor(modelData, row.modelData.name, row.modelData.kind)
                            popup.popup(this, 0, height + 2)
                        }
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: list.count === 0
            text: qsTr("No objects yet.\nFile ▸ Open structure…\nor drop a file here.")
            horizontalAlignment: Text.AlignHCenter
            color: NextTheme.muted
            font.pixelSize: NextTheme.fontSize
            lineHeight: 1.4
        }
    }

    Rectangle {
        id: mouseBox
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 96
        color: NextTheme.panel
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: NextTheme.line }
        Text {
            id: mouseTitle
            x: 10; y: 8
            text: qsTr("MOUSE") + "  ·  " + panel.modeLabel(panel.mouseMode)
            font.pixelSize: NextTheme.smallFont
            font.letterSpacing: 1.1
            color: NextTheme.muted
        }
        Flow {
            anchors { left: parent.left; right: parent.right; top: mouseTitle.bottom; margins: 8 }
            spacing: 4
            Repeater {
                model: panel.mouseModes
                delegate: NextButton {
                    required property var modelData
                    text: modelData.label
                    height: 24
                    pixelSize: 12
                    checked: panel.mouseMode === modelData.id
                    ToolTip.text: modelData.tip
                    onClicked: panel.mouseModeRequested(modelData.id)
                }
            }
        }
    }
}
