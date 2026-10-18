import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtCore
import MolShredder.Desktop

// PyMOL-style one-window shell (SEQ-330): menu bar, 3D viewport, object list on the right, movie strip,
// output window and command line below. Layout and behavior: docs/UI_NEXT.md.
Window {
    id: root
    width: 1360
    height: 860
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: qsTr("MolShredder")
    color: NextTheme.page

    property alias viewport: viewport
    property string pendingAction: ""
    // viewing | select-atoms | select-residues | select-chains | select-objects
    property string mouseMode: "viewing"
    // Number of failed commands since start; the smoke tests read it.
    property int errorCount: 0
    property int commandCount: 0

    Settings {
        id: prefs
        category: "UiNext"
        property bool outputVisible: true
        property int outputHeight: 140
        property bool sequenceVisible: false
        property string themeMode: "system"
        property int sideWidth: 300
        property bool wheelZoom: false
    }
    Component.onCompleted: {
        NextTheme.mode = prefs.themeMode
        output.append("dim", qsTr("MolShredder — type a command, use the menus, or drop a file on the window."))
        output.append("dim", qsTr("Commands: PyMOL syntax (show cartoon, chain A), name --option value, or help."))
    }

    // ---- helpers ------------------------------------------------------------------------------------------------
    function localPath(url) {
        let s = decodeURIComponent(url.toString())
        if (s.indexOf("file:///") === 0) s = s.substring(Qt.platform.os === "windows" ? 8 : 7)
        return s
    }
    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    // `ui:` lines drive the shell itself (used by --command for screenshots and smoke tests):
    //   ui:form <command> | ui:menu <Title> | ui:objectmenu <A|S|H|L|C> | ui:sequence on|off | ui:output on|off
    function uiCommand(text) {
        const parts = text.substring(3).trim().split(/\s+/)
        const rest = parts.slice(1).join(" ")
        switch (parts[0]) {
        case "form": forms.openFor(rest, ({})); break
        case "menu": menuPopup.entries = menus[rest]; menuPopup.popup(menubar, menuOrder.indexOf(rest) * 62 + 6, 34); break
        case "objectmenu": side.openMenu(rest); break
        case "sequence": prefs.sequenceVisible = rest === "on"; break
        case "output": prefs.outputVisible = rest === "on"; break
        case "theme": setTheme(rest); break
        case "mouse": setMouseMode(rest); break
        case "lang": setLanguage(rest); break
        }
    }
    function run(text) {
        if (text.indexOf("ui:") === 0) { uiCommand(text); return ({ok: true, lines: []}) }
        output.append("cmd", "MolShredder> " + text)
        const result = viewport.executeCommandLine(text)
        commandCount += 1
        if (!result.ok) errorCount += 1
        if (result.clear) { output.clear(); return result }
        const lines = result.lines || []
        for (let i = 0; i < lines.length; ++i) {
            const line = lines[i]
            const kind = line.indexOf("error:") === 0 ? "error" : (line.indexOf("hint:") === 0 ? "dim" : (i === 0 && result.ok ? "ok" : ""))
            output.append(kind, line)
        }
        return result
    }
    function setMouseMode(mode) {
        mouseMode = mode
        const granularity = mode === "select-residues" ? "residue" : (mode === "select-chains" ? "chain" : (mode === "select-objects" ? "object" : "atom"))
        viewport.setPickMode(granularity, mode === "viewing" ? "picked" : "sele", false)
        output.append("dim", qsTr("Mouse mode: %1").arg(side.modeLabel(mode)))
    }
    // Runs a command after making `owner` the active object (named selections belong to one object).
    function runIn(owner, text) {
        const items = viewport.objectItems
        let alreadyActive = false
        for (const item of items) if (item.name === owner && item.active) alreadyActive = true
        if (!alreadyActive) viewport.executeCommandLine("object activate --object " + quote(owner))
        return run(text)
    }
    function setLanguage(code) { if (localization.setLanguage(code)) output.append("dim", qsTr("Language: %1").arg(code === "ko" ? "한국어" : "English")) }
    function toggleOutput() { prefs.outputVisible = !prefs.outputVisible }
    function toggleSequence() { prefs.sequenceVisible = !prefs.sequenceVisible }
    function setTheme(mode) { prefs.themeMode = mode; NextTheme.mode = mode }
    function chooseFile(title, action, saving, filters) {
        pendingAction = action
        fileDialog.title = title
        fileDialog.fileMode = saving ? FileDialog.SaveFile : FileDialog.OpenFile
        fileDialog.nameFilters = filters || [qsTr("All files (*)")]
        fileDialog.open()
    }
    function onFileChosen(path) {
        const verbs = {
            openStructure: "load --path ", openTrajectory: "traj load --path ", openVolume: "volume load --path ",
            openSession: "session load --path ", saveStructure: "save --path ", saveSession: "session save --path ",
            saveImage: "image save --path ", runScript: "script run --path "
        }
        if (verbs[pendingAction]) run(verbs[pendingAction] + quote(path))
    }
    function perform(entry) {
        if (entry.command) { run(entry.command); return }
        if (entry.form) { forms.openFor(entry.form, entry.preset || ({})); return }
        switch (entry.action) {
        case "openStructure": chooseFile(qsTr("Open structure"), "openStructure", false, [qsTr("Structures (*.pdb *.ent *.cif *.mmcif *.bcif *.mmtf *.pdbqt *.mol *.mol2 *.sdf *.mae *.cml *.xyz *.gro *.pqr *.pse)"), qsTr("All files (*)")]); break
        case "openTrajectory": chooseFile(qsTr("Open trajectory"), "openTrajectory", false); break
        case "openVolume": chooseFile(qsTr("Open volume"), "openVolume", false, [qsTr("Maps (*.dx *.mrc *.map *.ccp4 *.xplor *.brix *.phi *.grd)"), qsTr("All files (*)")]); break
        case "openSession": chooseFile(qsTr("Open session"), "openSession", false); break
        case "saveStructure": chooseFile(qsTr("Save structure"), "saveStructure", true, [qsTr("PDB (*.pdb)"), qsTr("mmCIF (*.cif)"), qsTr("All files (*)")]); break
        case "saveSession": chooseFile(qsTr("Save session"), "saveSession", true); break
        case "saveImage": chooseFile(qsTr("Save image"), "saveImage", true, [qsTr("PNG (*.png)"), qsTr("PPM (*.ppm)")]); break
        case "runScript": chooseFile(qsTr("Run script"), "runScript", false, [qsTr("Python (*.py)"), qsTr("All files (*)")]); break
        case "toggleOutput": toggleOutput(); break
        case "toggleSequence": toggleSequence(); break
        case "allCommands": forms.openFor("", ({})); break
        case "theme": setTheme(entry.mode); break
        case "mouseMode": setMouseMode(entry.mode); break
        case "wheel": prefs.wheelZoom = entry.zoom; break
        case "language": setLanguage(entry.code); break
        case "quit": Qt.quit(); break
        }
    }

    // ---- menus ----------------------------------------------------------------------------------------------------
    readonly property var menus: ({
        "File": [
            {text: qsTr("Open structure…"), action: "openStructure", hint: "Ctrl+O"},
            {text: qsTr("Open trajectory…"), action: "openTrajectory"},
            {text: qsTr("Open volume…"), action: "openVolume"},
            {text: qsTr("Open session…"), action: "openSession"},
            {separator: true},
            {text: qsTr("Save structure…"), action: "saveStructure"},
            {text: qsTr("Save session…"), action: "saveSession"},
            {text: qsTr("Save image…"), action: "saveImage"},
            {text: qsTr("Export movie frames…"), form: "movie export"},
            {text: qsTr("Export video…"), form: "movie export-video"},
            {separator: true},
            {text: qsTr("Run Python script…"), action: "runScript"},
            {separator: true},
            {text: qsTr("Quit"), action: "quit", hint: "Ctrl+Q"}
        ],
        "Edit": [
            {text: qsTr("Undo"), command: "edit undo", hint: "Ctrl+Z"},
            {text: qsTr("Redo"), command: "edit redo", hint: "Ctrl+Shift+Z"},
            {text: qsTr("Edit history"), command: "edit history"},
            {separator: true},
            {text: qsTr("Select all"), command: "select sele, all"},
            {text: qsTr("Clear selection"), command: "select sele, none"}
        ],
        "Build": [
            {text: qsTr("Molecule builder…"), form: "build molecule"},
            {text: qsTr("Atom position…"), form: "edit atom-position"},
            {text: qsTr("Atom properties…"), form: "edit atom-properties"},
            {text: qsTr("Residue properties…"), form: "edit residue-properties"},
            {text: qsTr("Bond order…"), form: "edit bond-order"},
            {separator: true},
            {text: qsTr("Perceive chemistry"), command: "object perceive-chemistry"}
        ],
        "Movie": [
            {text: qsTr("Play"), command: "movie play"},
            {text: qsTr("Pause"), command: "movie pause"},
            {text: qsTr("Configure movie…"), form: "movie configure"},
            {text: qsTr("Map states…"), form: "movie map"},
            {text: qsTr("Keyframe…"), form: "movie keyframe"},
            {text: qsTr("Status"), command: "movie status"},
            {separator: true},
            {text: qsTr("Trajectory play"), command: "traj play"},
            {text: qsTr("Trajectory range…"), form: "traj range"}
        ],
        "Display": [
            {text: qsTr("Output window"), action: "toggleOutput", hint: "Ctrl+`"},
            {text: qsTr("Sequence viewer"), action: "toggleSequence", hint: "Ctrl+Shift+S"},
            {separator: true},
            {text: qsTr("Show…"), form: "show"},
            {text: qsTr("Hide…"), form: "hide"},
            {text: qsTr("Show only…"), form: "as"},
            {text: qsTr("Surface"), command: "surface show"},
            {text: qsTr("Cartoon type…"), form: "cartoon"},
            {separator: true},
            {text: qsTr("Background…"), form: "background set"},
            {text: qsTr("Stereo…"), form: "stereo set"},
            {separator: true},
            {text: qsTr("Theme: follow system"), action: "theme", mode: "system"},
            {text: qsTr("Theme: light"), action: "theme", mode: "light"},
            {text: qsTr("Theme: dark"), action: "theme", mode: "dark"}
        ],
        "Setting": [
            {text: qsTr("Set render setting…"), form: "setting set"},
            {text: qsTr("Show setting…"), form: "setting get"},
            {text: qsTr("List settings"), command: "setting list"},
            {text: qsTr("Reset setting…"), form: "setting reset"}
        ],
        "Scene": [
            {text: qsTr("Store scene…"), form: "scene store"},
            {text: qsTr("Recall scene…"), form: "scene recall"},
            {text: qsTr("List scenes"), command: "scene list"},
            {separator: true},
            {text: qsTr("Store view…"), form: "view store"},
            {text: qsTr("Recall view…"), form: "view recall"},
            {text: qsTr("Reset orientation"), command: "view reset"},
            {text: qsTr("Clip…"), form: "view clip"}
        ],
        "Mouse": [
            {text: (mouseMode === "viewing" ? "✓ " : "") + qsTr("3-button viewing"), action: "mouseMode", mode: "viewing"},
            {text: (mouseMode === "select-atoms" ? "✓ " : "") + qsTr("Selecting atoms"), action: "mouseMode", mode: "select-atoms"},
            {text: (mouseMode === "select-residues" ? "✓ " : "") + qsTr("Selecting residues"), action: "mouseMode", mode: "select-residues"},
            {text: (mouseMode === "select-chains" ? "✓ " : "") + qsTr("Selecting chains"), action: "mouseMode", mode: "select-chains"},
            {text: (mouseMode === "select-objects" ? "✓ " : "") + qsTr("Selecting objects"), action: "mouseMode", mode: "select-objects"},
            {text: qsTr("3-button editing (planned)"), enabled: false},
            {separator: true},
            {text: (!prefs.wheelZoom ? "✓ " : "") + qsTr("Wheel moves the clipping planes"), action: "wheel", zoom: false},
            {text: (prefs.wheelZoom ? "✓ " : "") + qsTr("Wheel zooms"), action: "wheel", zoom: true}
        ],
        "Analyze": [
            {text: qsTr("Distance…"), form: "measure distance"},
            {text: qsTr("Angle…"), form: "measure angle"},
            {text: qsTr("Dihedral…"), form: "measure dihedral"},
            {separator: true},
            {text: qsTr("Contacts…"), form: "analyze contacts"},
            {text: qsTr("Hydrogen bonds…"), form: "analyze hbonds"},
            {text: qsTr("Secondary structure"), command: "analyze secondary-structure"},
            {text: qsTr("Surface area…"), form: "analyze sasa"},
            {text: qsTr("Trajectory RMSD…"), form: "analyze trajectory rmsd"},
            {text: qsTr("Dipole…"), form: "analyze dipole"},
            {text: qsTr("Point group"), command: "analyze symmetry"},
            {text: qsTr("Unit cell"), command: "pbc get"}
        ],
        "Tools": [
            {text: qsTr("All commands…"), action: "allCommands", hint: "Ctrl+K"},
            {text: qsTr("Command help"), command: "help"}
        ],
        "Help": [
            {text: qsTr("Command help"), command: "help"},
            {text: qsTr("System information"), command: "system info"},
            {separator: true},
            {text: (localization.currentLanguage === "en" ? "✓ " : "") + "English", action: "language", code: "en"},
            {text: (localization.currentLanguage === "ko" ? "✓ " : "") + "한국어", action: "language", code: "ko"}
        ]
    })
    readonly property var menuOrder: ["File", "Edit", "Build", "Movie", "Display", "Setting", "Scene", "Mouse", "Analyze", "Tools", "Help"]
    // Menu titles are keys of `menus`; the visible text is translated here.
    readonly property var menuTitles: ({
        "File": qsTr("File"), "Edit": qsTr("Edit"), "Build": qsTr("Build"), "Movie": qsTr("Movie"),
        "Display": qsTr("Display"), "Setting": qsTr("Setting"), "Scene": qsTr("Scene"), "Mouse": qsTr("Mouse"),
        "Analyze": qsTr("Analyze"), "Tools": qsTr("Tools"), "Help": qsTr("Help")
    })

    NextMenu {
        id: menuPopup
        onPicked: function(entry) { root.perform(entry) }
    }

    Rectangle {
        id: menubar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 32
        color: NextTheme.chrome
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: NextTheme.line }
        Row {
            anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
            spacing: 2
            Repeater {
                model: root.menuOrder
                delegate: NextButton {
                    required property string modelData
                    text: root.menuTitles[modelData]
                    outlined: false
                    height: 24
                    checked: menuPopup.visible && menuPopup.entries === root.menus[modelData]
                    onClicked: {
                        menuPopup.entries = root.menus[modelData]
                        menuPopup.popup(this, 0, height + 4)
                    }
                }
            }
        }
    }

    // ---- main area ------------------------------------------------------------------------------------------------
    Item {
        id: body
        anchors { left: parent.left; right: parent.right; top: menubar.bottom; bottom: statusbar.top }

        Item {
            id: left
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: divider.left }

            NextSequenceBar {
                id: sequence
                viewport: viewport
                visible: prefs.sequenceVisible
                height: visible ? 34 : 0
                anchors { left: parent.left; right: parent.right; top: parent.top }
                onCommand: function(text) { root.run(text) }
            }

            Item {
                id: viewArea
                anchors { left: parent.left; right: parent.right; top: sequence.bottom; bottom: movie.top }
                clip: true

                MolecularViewport {
                    id: viewport
                    objectName: "molecularViewport"
                    anchors.fill: parent
                    angle: 0

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                        property real previousX: 0
                        property real previousY: 0
                        property real pressX: 0
                        property real pressY: 0
                        property bool dragged: false
                        onPressed: function(mouse) {
                            previousX = mouse.x; previousY = mouse.y; pressX = mouse.x; pressY = mouse.y; dragged = false
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed) return
                            const dx = mouse.x - previousX
                            const dy = mouse.y - previousY
                            if (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) > 4) dragged = true
                            const buttons = mouse.buttons
                            if ((buttons & Qt.MiddleButton) !== 0 || ((buttons & Qt.LeftButton) !== 0 && (mouse.modifiers & Qt.ShiftModifier) !== 0))
                                viewport.pan(dx, dy)
                            else if ((buttons & Qt.RightButton) !== 0)
                                viewport.dolly(dy / 12.0)
                            else if ((buttons & Qt.LeftButton) !== 0)
                                viewport.orbit(dx, dy)
                            previousX = mouse.x; previousY = mouse.y
                        }
                        onWheel: function(wheel) {
                            const notches = wheel.angleDelta.y / 120.0
                            if (prefs.wheelZoom || (wheel.modifiers & Qt.ControlModifier) !== 0) viewport.dolly(-notches)
                            else viewport.clipCamera("move", notches * 2.0, "", "")
                            wheel.accepted = true
                        }
                        onClicked: function(mouse) {
                            if (dragged || mouse.button !== Qt.LeftButton) return
                            const adding = root.mouseMode !== "viewing" && (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
                            const granularity = root.mouseMode === "select-residues" ? "residue" : (root.mouseMode === "select-chains" ? "chain" : (root.mouseMode === "select-objects" ? "object" : "atom"))
                            viewport.setPickMode(granularity, root.mouseMode === "viewing" ? "picked" : "sele", adding)
                            viewport.pickAt(mouse.x, mouse.y)
                        }
                        onDoubleClicked: viewport.resetViewAnimated(0.35, 1)
                    }
                }

                DropArea {
                    anchors.fill: parent
                    onDropped: function(drop) {
                        for (const url of drop.urls) root.run("load --path " + root.quote(root.localPath(url)))
                    }
                }

                // HUD: selection chip (top-left), sequence toggle (top-right), mouse hint (bottom-right)
                Row {
                    x: 12; y: 10; spacing: 6
                    Rectangle {
                        visible: viewport.selectionText.length > 0 && viewport.selectionText.indexOf("No selection") !== 0
                        height: 22; width: chipText.implicitWidth + 18; radius: 11
                        color: Qt.rgba(0.94, 0.64, 0.23, 0.2); border.color: NextTheme.accent
                        Text { id: chipText; anchors.centerIn: parent; text: viewport.selectionText; color: NextTheme.viewText; font.family: NextTheme.mono; font.pixelSize: 12 }
                    }
                }
                NextButton {
                    id: seqButton
                    anchors { right: parent.right; rightMargin: 12; top: parent.top; topMargin: 10 }
                    text: "SEQ"
                    mono: true
                    hud: true
                    pixelSize: 11
                    checked: prefs.sequenceVisible
                    ToolTip.text: qsTr("Sequence viewer (Ctrl+Shift+S)")
                    onClicked: root.toggleSequence()
                }
                Text {
                    anchors { right: parent.right; rightMargin: 12; bottom: parent.bottom; bottomMargin: 8 }
                    text: root.mouseMode === "viewing" ? qsTr("3-button viewing · left rotate · middle move · right zoom · wheel clip") : qsTr("%1 · click selects into (sele) · Ctrl-click adds · drag rotates").arg(side.modeLabel(root.mouseMode))
                    color: NextTheme.viewText
                    opacity: 0.65
                    font.family: NextTheme.mono
                    font.pixelSize: 11
                }
            }

            NextMovieBar {
                id: movie
                viewport: viewport
                anchors { left: parent.left; right: parent.right; bottom: output.top }
            }

            NextConsole {
                id: output
                viewport: viewport
                outputVisible: prefs.outputVisible
                outputHeight: prefs.outputHeight
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: (prefs.outputVisible ? 7 + 24 + prefs.outputHeight : 0) + 34
                onOutputVisibleToggled: root.toggleOutput()
                onOutputHeightEdited: function(value) { prefs.outputHeight = value }
                onCommandEntered: function(text) { root.run(text) }
            }
        }

        Rectangle {
            id: divider
            anchors { right: side.left; top: parent.top; bottom: parent.bottom }
            width: 5
            color: NextTheme.chrome
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 1; height: parent.height; color: NextTheme.line }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                property real pressX: 0
                property int pressWidth: 0
                onPressed: function(mouse) { pressX = mapToItem(null, mouse.x, mouse.y).x; pressWidth = prefs.sideWidth }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const x = mapToItem(null, mouse.x, mouse.y).x
                    prefs.sideWidth = Math.max(240, Math.min(520, pressWidth + (pressX - x)))
                }
            }
        }
        NextObjectPanel {
            id: side
            viewport: viewport
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: prefs.sideWidth
            mouseMode: root.mouseMode
            onCommand: function(text) { root.run(text) }
            onCommandIn: function(owner, text) { root.runIn(owner, text) }
            onMouseModeRequested: function(mode) { root.setMouseMode(mode) }
            onFormRequested: function(name, preset) { forms.openFor(name, preset) }
            onRenameRequested: function(name) { renameDialog.start(name) }
        }
    }

    Rectangle {
        id: statusbar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 26
        color: NextTheme.chrome
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: NextTheme.line }
        Row {
            anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 6
            NextButton { text: qsTr("Output") + (prefs.outputVisible ? " ▣" : " ▢"); outlined: false; checked: prefs.outputVisible; height: 20; pixelSize: 12
                ToolTip.text: qsTr("Show or hide the output window (Ctrl+`)"); onClicked: root.toggleOutput() }
            NextButton { text: qsTr("Sequence") + (prefs.sequenceVisible ? " ▣" : " ▢"); outlined: false; checked: prefs.sequenceVisible; height: 20; pixelSize: 12
                ToolTip.text: qsTr("Show or hide the sequence viewer (Ctrl+Shift+S)"); onClicked: root.toggleSequence() }
        }
        Text {
            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
            text: viewport.statusText + "  ·  " + qsTr("%1 atoms").arg(viewport.atomCount)
            color: NextTheme.muted
            font.pixelSize: 12
            elide: Text.ElideLeft
        }
    }

    // ---- dialogs and shortcuts ------------------------------------------------------------------------------------
    FileDialog {
        id: fileDialog
        onAccepted: root.onFileChosen(root.localPath(selectedFile))
    }
    NextCommandForm {
        id: forms
        viewport: viewport
        onRun: function(text) { root.run(text) }
    }
    Popup {
        id: renameDialog
        property string oldName: ""
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 340
        padding: 14
        background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }
        function start(name) { oldName = name; field.text = name; open(); field.selectAll(); field.forceActiveFocus() }
        Column {
            spacing: 8
            width: parent.width
            Text { text: qsTr("Rename %1").arg(renameDialog.oldName); color: NextTheme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            TextField {
                id: field
                width: parent.width
                font.pixelSize: NextTheme.fontSize
                color: NextTheme.text
                background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: NextTheme.active }
                onAccepted: {
                    if (text.length > 0 && text !== renameDialog.oldName)
                        root.run("object rename --object " + root.quote(renameDialog.oldName) + " --name " + root.quote(text))
                    renameDialog.close()
                }
            }
        }
    }
    Shortcut { sequence: StandardKey.Open; onActivated: root.perform({action: "openStructure"}) }
    Shortcut { sequence: "Ctrl+Z"; onActivated: root.run("edit undo") }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: root.run("edit redo") }
    Shortcut { sequence: "Ctrl+`"; onActivated: root.toggleOutput() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: root.toggleSequence() }
    Shortcut { sequence: "Ctrl+K"; onActivated: root.perform({action: "allCommands"}) }
    Shortcut { sequence: "Ctrl+L"; onActivated: { if (!prefs.outputVisible) {} output.focusInput() } }
    Shortcut { sequence: StandardKey.Quit; onActivated: Qt.quit() }
}
