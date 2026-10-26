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
    signal inspectRequested(string name)
    signal deleteRequested(string name)
    signal selectionEditRequested(string name, string owner)
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
    // Cartoon art styles (`cartoon style --name … --selection …`).
    readonly property var cartoonStyles: [
        {id: "automatic", text: qsTr("automatic (default)")},
        {id: "round_ribbon", text: qsTr("round ribbon")},
        {id: "flat_ribbon", text: qsTr("flat ribbon")},
        {id: "oval_ribbon", text: qsTr("oval ribbon")},
        {id: "tube_helices", text: qsTr("tube helices, arrow sheets")},
        {id: "dumbbell_helices", text: qsTr("dumbbell helices (fancy)")},
        {id: "barbell", text: qsTr("barbell")},
        {id: "edged_ribbon", text: qsTr("edged ribbon")},
        {id: "square_ribbon", text: qsTr("square ribbon")},
        {id: "strip", text: qsTr("strip")},
        {id: "putty", text: qsTr("putty (B-factor tube)")},
        {id: "tube", text: qsTr("tube")},
        {id: "thin_tube", text: qsTr("thin tube")}
    ]
    // Named looks (`preset --name … --selection …`).
    readonly property var presets: [
        {id: "default", text: qsTr("default look")},
        {id: "simple", text: qsTr("simple")},
        {id: "simple_no_solv", text: qsTr("simple (no solvent)")},
        {id: "ball_and_stick", text: qsTr("ball and stick")},
        {id: "ligands", text: qsTr("ligands and their surroundings")}
    ]

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
                {text: qsTr("ungroup"), command: "group excise --name " + name}
            ]
        }
        // Entries below run against the row's own object: the object is made active first (the camera, show, hide
        // and edit commands read the active object). A named selection row already carries its owner.
        const own = rowKind === "object" ? name : ""
        const run = function(text, command) { const entry = {text: text, command: command}; if (own.length > 0) entry.owner = own; return entry }
        // Subsets of atoms an entry can be limited to.
        const subsets = [
            {text: qsTr("all atoms"), filter: ""},
            {text: qsTr("main chain"), filter: " and bb."},
            {text: qsTr("side chains"), filter: " and sc."},
            {text: qsTr("ligands"), filter: " and organic"},
            {text: qsTr("solvent"), filter: " and solvent"},
            {text: qsTr("hydrogens"), filter: " and hydro"}
        ]
        if (kind === "A" && rowKind === "selection") {
            return [
                {text: qsTr("zoom"), command: "zoom " + sel},
                {text: qsTr("center"), command: "center " + sel},
                {text: qsTr("orient"), command: "orient " + sel},
                {separator: true},
                {text: qsTr("edit expression…"), action: "editSelection", name: name},
                {text: qsTr("create object…"), form: "object create", preset: {"selection": "@" + name}},
                {text: qsTr("extract to object…"), form: "object extract", preset: {"selection": "@" + name}},
                {separator: true},
                {text: qsTr("delete selection"), command: "delete " + sel}
            ]
        }
        if (kind === "A") {
            const presetItems = panel.presets.map(function(p) { return run(p.text, "preset --name " + p.id + " --selection " + sel) })
            const polarItems = [
                run(qsTr("hydrogen bonds within the object…"), "analyze hbonds --donors \"" + sel + "\" --acceptors \"" + sel + "\""),
                run(qsTr("contacts within the object…"), "analyze contacts --first \"" + sel + "\" --second \"" + sel + "\"")
            ]
            const makeSelection = function(label, suffix, filter) { return run(label, "select " + name + "_" + suffix + ", (" + sel + ")" + filter) }
            return [
                run(qsTr("zoom"), "zoom " + sel),
                run(qsTr("orient"), "orient " + sel),
                run(qsTr("center"), "center " + sel),
                run(qsTr("set rotation origin"), "origin " + sel),
                {separator: true},
                {text: qsTr("preset"), submenu: presetItems},
                {text: qsTr("cartoon style"), submenu: panel.cartoonStyles.map(function(c) { return run(c.text, "cartoon style --name " + c.id + " --selection " + sel) })},
                {text: qsTr("find"), submenu: polarItems},
                {text: qsTr("align"), submenu: [
                    {text: qsTr("this object to another…"), form: "object align", preset: {"mobile-object": name}},
                    {text: qsTr("another object to this…"), form: "object align", preset: {"target-object": name}}
                ]},
                {text: qsTr("make selection"), submenu: [
                    makeSelection(qsTr("polymer"), "polymer", " and polymer"),
                    makeSelection(qsTr("ligands"), "ligands", " and organic"),
                    makeSelection(qsTr("solvent"), "solvent", " and solvent"),
                    makeSelection(qsTr("C-alpha atoms"), "calpha", " and name CA and polymer.protein"),
                    makeSelection(qsTr("main chain"), "mainchain", " and bb."),
                    makeSelection(qsTr("side chains"), "sidechain", " and sc."),
                    makeSelection(qsTr("polar hydrogens"), "polar_h", " and (hydro and neighbor (elem N+O+S))"),
                    makeSelection(qsTr("non-polar hydrogens"), "nonpolar_h", " and (hydro and not neighbor (elem N+O+S))")
                ]},
                {separator: true},
                {text: qsTr("rename…"), action: "rename", name: name},
                {text: qsTr("duplicate…"), form: "object copy", preset: {"object": name}},
                {text: qsTr("group…"), form: "group add", preset: {"members": name}},
                {text: qsTr("delete…"), action: "confirmDelete", name: name},
                {separator: true},
                run(qsTr("remove waters"), "object remove-atoms --selection \"(" + sel + ") and solvent\""),
                {text: qsTr("hydrogens"), submenu: [
                    run(qsTr("remove all hydrogens"), "object remove-atoms --selection \"(" + sel + ") and hydro\""),
                    run(qsTr("remove non-polar hydrogens"), "object remove-atoms --selection \"(" + sel + ") and hydro and not neighbor (elem N+O+S)\"")
                ]},
                {text: qsTr("states"), submenu: [
                    {text: qsTr("split into objects"), command: "object split-states --object " + name},
                    {text: qsTr("split by chain"), command: "object split --object " + name}
                ]},
                {text: qsTr("placement"), submenu: [
                    {text: qsTr("fix"), command: "object fix --object " + name},
                    {text: qsTr("free"), command: "object free --object " + name}
                ]},
                {separator: true},
                {text: qsTr("inspect…"), action: "inspect", name: name},
                {text: qsTr("analyze…"), form: "analyze center", preset: {"selection": sel}}
            ]
        }
        if (kind === "S" || kind === "H") {
            const show = kind === "S"
            const verb = show ? "show " : "hide "
            const items = reps.map(function(r) {
                return {text: r, submenu: subsets.map(function(subset) { return run(subset.text, verb + r + ", (" + sel + ")" + subset.filter) }) }
            })
            items.push({separator: true})
            if (show) {
                items.push({text: qsTr("surface"), submenu: [
                    run(qsTr("show surface"), "surface show"),
                    {text: qsTr("surface options…"), action: "surfaceOptions"}
                ]})
                items.push({text: qsTr("only (hide the rest)"), submenu: reps.map(function(r) { return run(r, "as " + r + ", (" + sel + ")") })})
            } else {
                items.push(run(qsTr("surface"), "surface hide"))
                items.push(run(qsTr("labels"), "hide labels, (" + sel + ")"))
                items.push(run(qsTr("everything"), "hide everything, (" + sel + ")"))
                items.push(run(qsTr("waters"), "hide everything, (" + sel + ") and solvent"))
                items.push(run(qsTr("hydrogens"), "hide everything, (" + sel + ") and hydro"))
            }
            return items
        }
        if (kind === "L") {
            const at = function(filter) { return "(" + sel + ")" + filter }
            return [
                run(qsTr("residue (name and number)"), "label " + at(" and name CA") + ", resn+resi"),
                run(qsTr("residue name"), "label " + at(" and name CA") + ", resn"),
                run(qsTr("residue number"), "label " + at(" and name CA") + ", resi"),
                run(qsTr("chain"), "label " + at(" and name CA") + ", chain"),
                {separator: true},
                run(qsTr("atom name"), "label " + at("") + ", name"),
                run(qsTr("element"), "label " + at("") + ", elem"),
                run(qsTr("atom number"), "label " + at("") + ", ID"),
                run(qsTr("B-factor"), "label " + at("") + ", b"),
                run(qsTr("occupancy"), "label " + at("") + ", q"),
                run(qsTr("van der Waals radius"), "label " + at("") + ", vdw"),
                run(qsTr("secondary structure"), "label " + at(" and name CA") + ", ss"),
                {separator: true},
                run(qsTr("clear"), "label " + at("") + ", \"\"")
            ]
        }
        // Color: element and structure schemes, then named colors grouped by family.
        const families = [
            {text: qsTr("reds"), colors: ["red", "tv_red", "raspberry", "firebrick", "ruby", "salmon", "darksalmon", "deepsalmon", "warmpink", "chocolate", "brown"]},
            {text: qsTr("greens"), colors: ["green", "tv_green", "chartreuse", "limegreen", "lime", "palegreen", "splitpea", "smudge", "forest", "olive", "deepolive"]},
            {text: qsTr("blues"), colors: ["blue", "tv_blue", "marine", "slate", "skyblue", "lightblue", "purpleblue", "deepblue", "density"]},
            {text: qsTr("yellows"), colors: ["yellow", "tv_yellow", "paleyellow", "limon", "yelloworange", "sand", "wheat"]},
            {text: qsTr("magentas"), colors: ["magenta", "lightmagenta", "hotpink", "pink", "lightpink", "violet", "violetpurple", "purple", "deeppurple"]},
            {text: qsTr("cyans"), colors: ["cyan", "palecyan", "aquamarine", "greencyan", "teal", "deepteal", "lightteal"]},
            {text: qsTr("oranges"), colors: ["orange", "tv_orange", "brightorange", "lightorange"]},
            {text: qsTr("grays"), colors: ["white", "grey90", "grey80", "grey70", "grey60", "grey50", "grey40", "grey30", "grey20", "grey10", "black"]}
        ]
        const entries = [
            run(qsTr("by element (keep carbon)"), "color atomic, (" + sel + ") and not elem C"),
            run(qsTr("by element (all atoms)"), "color atomic, " + sel),
            {text: qsTr("by element, carbons colored…"), submenu: [
                ["green", "g"], ["cyan", "c"], ["magenta", "m"], ["yellow", "y"], ["salmon", "s"],
                ["white", "w"], ["slate", "b"], ["orange", "o"], ["purple", "p"], ["pink", "k"]
            ].map(function(pair) { return run(pair[0], "util.cba" + pair[1] + " " + sel) })},
            run(qsTr("by chain"), "util.cbc " + sel),
            run(qsTr("by secondary structure"), "color red, byres ((" + sel + ") and ss H); color yellow, byres ((" + sel + ") and ss S); color green, (" + sel + ") and not byres (ss H+S)"),
            run(qsTr("spectrum by index"), "spectrum count, rainbow, " + sel),
            run(qsTr("spectrum by B-factor"), "spectrum b, rainbow, " + sel),
            {separator: true}
        ]
        families.forEach(function(family) {
            entries.push({text: family.text, submenu: family.colors.map(function(c) { return run(c, "color " + c + ", " + sel) })})
        })
        entries.push({separator: true})
        entries.push({text: qsTr("other…"), form: "color", preset: {"selection": sel}})
        return entries
    }

    // The row under the keyboard cursor (null when the list is empty).
    function currentRow() { return rows && list.currentIndex >= 0 && list.currentIndex < rows.length ? rows[list.currentIndex] : null }
    // Returns true when the key was a list key.
    function keyAction(key, modifiers) {
        const row = currentRow()
        if (!row) return false
        const isObject = row.kind === "object", isGroup = row.kind === "group", isSelection = row.kind === "selection"
        if (key === Qt.Key_Return || key === Qt.Key_Enter) {
            if (isObject) command("object activate --id " + row.id)
            else if (isGroup) command("group toggle --name " + row.name)
            return true
        }
        if (key === Qt.Key_Space) {
            if (!isSelection) command((row.visible ? "disable " : "enable ") + row.name)
            return true
        }
        if (key === Qt.Key_F2) { if (isObject) renameRequested(row.name); return true }
        if (key === Qt.Key_Delete || key === Qt.Key_Backspace) {
            if (isObject) deleteRequested(row.name)
            else if (isSelection) commandIn(row.owner, "delete " + row.name)
            return true
        }
        if (key === Qt.Key_Menu || (key === Qt.Key_F10 && (modifiers & Qt.ShiftModifier) !== 0)) {
            popup.owner = isSelection ? row.owner : ""
            popup.entries = menuFor("A", row.name, row.kind)
            popup.popup(list, 40, (list.currentIndex - 0) * NextTheme.rowHeight + NextTheme.rowHeight)
            return true
        }
        return false
    }

    // Middle click on an object or selection row moves the camera to its middle. The camera commands
    // read the active object only, so the owning object is activated first; groups span objects and are skipped.
    function centerOn(row) {
        if (row.kind === "object") commandIn(row.name, "center " + row.name)
        else if (row.kind === "selection") commandIn(row.owner, "center " + row.name)
    }
    // Same path for an object row that is not the first (used by the `ui:objectcenter <name>` test hook).
    function centerNamed(name) {
        for (const row of rows) if (row.kind === "object" && row.name === name) { centerOn(row); return }
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
            if (entry.command && entry.owner) panel.commandIn(entry.owner, entry.command)
            else if (entry.command && owner.length > 0) panel.commandIn(owner, entry.command)
            else if (entry.command) panel.command(entry.command)
            else if (entry.form) panel.formRequested(entry.form, entry.preset || ({}))
            else if (entry.action === "rename") panel.renameRequested(entry.name)
            else if (entry.action === "inspect") panel.inspectRequested(entry.name)
            else if (entry.action === "confirmDelete") panel.deleteRequested(entry.name)
            else if (entry.action === "editSelection") panel.selectionEditRequested(entry.name, owner)
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
        NextButton {
            objectName: "workspaceScopeButton"
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            text: qsTr("All objects")
            pixelSize: NextTheme.smallFont
            checkable: true
            checked: panel.viewport ? panel.viewport.workspaceScope : false
            ToolTip.text: qsTr("When on, show, hide and color apply to every object, not only the active one.")
            onClicked: panel.viewport.workspaceScope = !panel.viewport.workspaceScope
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
        // Keyboard: Up/Down move, Enter activates (a group opens or closes), Space shows or hides, F2 renames,
        // Delete asks to delete, the Menu key or Shift+F10 opens the actions menu.
        activeFocusOnTab: true
        keyNavigationEnabled: true
        currentIndex: 0
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Objects")
        Keys.onPressed: function(event) { if (panel.keyAction(event.key, event.modifiers)) event.accepted = true }
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
            Accessible.role: Accessible.ListItem
            Accessible.name: (isSelection ? "(" + modelData.name + ")" : modelData.name) + (isObject && modelData.visible === false ? " " + qsTr("hidden") : "")
            // the row the keyboard is on
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                visible: list.activeFocus && row.ListView.isCurrentItem
                border.width: 2
                border.color: NextTheme.active
            }

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
                ToolTip.visible: (isSelection || isObject) && nameArea.containsMouse
                ToolTip.delay: 700
                ToolTip.text: isSelection ? modelData.expression + (modelData.dynamic ? "  (dynamic)" : "")
                                          : (isObject ? qsTr("Click: make active · Double-click: rename · Middle-click: center the view") : "")
                MouseArea {
                    id: nameArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.MiddleButton) panel.centerOn(modelData)
                        else if (isObject) panel.command("object activate --id " + modelData.id)
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
