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
    // Automated runs (see main.cpp) stay behind everything and off-screen.
    flags: backgroundRun ? (Qt.Tool | Qt.WindowStaysOnBottomHint | Qt.WindowDoesNotAcceptFocus) : Qt.Window
    x: backgroundRun ? -30000 : Screen.virtualX + 60
    y: backgroundRun ? -30000 : Screen.virtualY + 60
    title: qsTr("MolShredder")
    color: NextTheme.page

    property alias viewport: viewport
    property string pendingAction: ""
    property int pendingResult: -1
    property string pendingResultFormat: "json"
    // viewing | select-atoms | select-residues | select-chains | select-objects
    property string mouseMode: "viewing"
    // Number of failed commands since start; the smoke tests read it.
    // Exactly the JSON the System information panel shows (the smoke tests compare it with the viewport's).
    property string systemInfoPanelSourceJson: ""
    // The session file last saved or loaded; while one is known the workspace is autosaved next to it.
    property string currentSessionPath: ""
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
        case "inspector": showInspector(rest === "color" ? 1 : (rest === "settings" ? 2 : 0)); break
        case "movieeditor": movieEditor.openEditor(); break
        case "results": results.openResults(); break
        case "selection": selectionEditor.openEditor("sele", rest); break
        case "scenes": scenesPanel.openPanel(); break
        case "surface": surfacePanel.openPanel(); break
        case "editor": atomEditor.openEditor(rest); break
        case "chemistry": chemistryPanel.openPanel(); if (rest === "analyze") chemistryPanel.analyze(); break
        case "builder": moleculeBuilder.openBuilder(); break
        case "camera": cameraPanel.openPanel(); break
        case "script": scriptPanel.openPanel(rest); break
        case "scriptrun": {
            const inProcess = rest.indexOf("inprocess ") === 0
            scriptPanel.openPanel(inProcess ? rest.substring(10) : rest)
            scriptPanel.setIsolated(!inProcess)
            scriptPanel.start()
            break
        }
        case "sysinfo": openSystemInfo(); break
        case "menuaudit": runMenuAudit(); break
        case "expect": expect(rest); break
        case "scope": viewport.workspaceScope = rest === "on"; break
        case "delete": deleteDialog.ask(rest); break
        case "confirmdelete": deleteDialog.confirm(); break
        case "autosavenow": if (!autosaveNow()) { errorCount += 1; console.log("MolShredder autosave failed") } break
        case "volume": volumePanel.openPanel(rest === "slice" ? 1 : (rest === "direct" ? 2 : 0)); break
        case "trajectory": trajectoryImport.openPanel(rest.length > 0 ? rest : undefined); break
        case "menu": menuPopup.entries = menus[rest]; menuPopup.popup(menubar, menuOrder.indexOf(rest) * 62 + 6, 34); break
        case "objectmenu": side.openMenu(rest); break
        case "sequence": prefs.sequenceVisible = rest === "on"; break
        case "output": prefs.outputVisible = rest === "on"; break
        case "theme": setTheme(rest); break
        case "mouse": setMouseMode(rest); break
        case "lang": setLanguage(rest); break
        }
    }
    function showInspector(tab) {
        let name = ""
        for (const item of viewport.objectItems) if (item.active) name = item.name
        inspector.targetObject = name
        inspector.openInspector(tab)
        if (inspector.catalog.length === 0) { errorCount += 1; output.append("error", "error: the render-setting catalog is empty") }
    }
    // Every menu entry must name a real command (with real options), a real form or a shell action; the same goes
    // for the object-row menus of the objects, selections and groups that exist right now. Returns the problems.
    function menuAudit() {
        const problems = []
        let entries = 0
        const names = viewport.commandCatalog().map(function(entry) { return entry.name })
        const check = function(where, entry) {
            if (entry.separator) return
            entries += 1
            if (!entry.text || entry.text.length === 0) problems.push(where + ": entry without text")
            if (entry.command) {
                const problem = viewport.auditCommandLine(entry.command)
                if (problem.length > 0) problems.push(where + " ▸ " + entry.text + ": " + problem)
            } else if (entry.form) {
                if (names.indexOf(entry.form) < 0) problems.push(where + " ▸ " + entry.text + ": unknown form " + entry.form)
            } else if (!entry.action && entry.enabled !== false) {
                problems.push(where + " ▸ " + entry.text + ": no command, form or action")
            }
        }
        for (const title of menuOrder) {
            const list = menus[title]
            if (!list) { problems.push(title + ": menu is missing"); continue }
            for (const entry of list) check(title, entry)
        }
        for (const row of viewport.objectTree) {
            const kinds = row.kind === "object" ? ["A", "S", "H", "L", "C"] : (row.kind === "selection" ? ["A", "S", "H", "L", "C"] : ["A"])
            for (const kind of kinds)
                for (const entry of side.menuFor(kind, row.name, row.kind)) check(row.kind + " " + row.name + " " + kind, entry)
        }
        return {entries: entries, problems: problems}
    }
    function runMenuAudit() {
        const audit = menuAudit()
        for (const problem of audit.problems) { errorCount += 1; output.append("error", "error: " + problem); console.log("MolShredder menu audit problem: " + problem) }
        console.log("MolShredder menu audit: entries=" + audit.entries + " problems=" + audit.problems.length)
    }
    function autosaveNow() {
        if (currentSessionPath.length === 0) return false
        return viewport.executeCommandLine("session autosave --path " + quote(currentSessionPath + ".autosave")
                                           + " --recovery " + quote(currentSessionPath + ".autosave.previous")
                                           + " --ui-visible-panels " + quote(visiblePanels())).ok
    }
    // `ui:expect …` lines of the smoke tests: a failed expectation counts as an error of the run.
    function expect(spec) {
        const parts = spec.split(/\s+/)
        let ok = false
        switch (parts[0]) {
        case "labels": ok = viewport.analysisLabelItems.length >= parseInt(parts[1], 10); break
        case "movietimer": ok = moviePlaybackTimer.running; break
        case "scriptready": ok = scriptPanel.ready; break
        case "scripthint": ok = scriptPanel.needsInProcess; break
        case "nohint": ok = !scriptPanel.needsInProcess; break
        case "scope": ok = viewport.workspaceScope === (parts[1] === "on"); break
        case "deleteopen": ok = deleteDialog.visible; break
        case "file": ok = viewport.fileExists(spec.substring(5).trim()); break
        case "panels": ok = visiblePanels().split(",").indexOf(parts[1]) >= 0; break
        }
        if (!ok) { errorCount += 1; output.append("error", "error: expectation failed: " + spec); console.log("MolShredder expectation failed: " + spec) }
    }
    // The entry points the desktop smoke tests (and the session restore) call by name.
    function openSystemInfo() { systemInfoPanel.openPanel(); systemInfoPanelSourceJson = systemInfoPanel.sourceJson }
    function openViews() { cameraPanel.openPanel() }
    function openRenderSettings() { showInspector(2) }
    function openCoordinateEditor() { atomEditor.openEditor() }
    function openTopologyEditor() { atomEditor.openEditor() }
    function openMoleculeBuilder() { moleculeBuilder.openBuilder() }
    function openAnalyze() { results.openResults() }
    readonly property var sessionPanels: [
        {id: "selection", panel: function() { return selectionEditor }, open: function() { selectionEditor.openEditor() }},
        {id: "analysis", panel: function() { return results }, open: function() { results.openResults() }},
        {id: "coordinate-edit", panel: function() { return atomEditor }, open: function() { atomEditor.openEditor() }},
        {id: "molecule-builder", panel: function() { return moleculeBuilder }, open: function() { moleculeBuilder.openBuilder() }},
        {id: "render-settings", panel: function() { return inspector }, open: function() { showInspector(2) }},
        {id: "views", panel: function() { return cameraPanel }, open: function() { cameraPanel.openPanel() }},
        {id: "scenes", panel: function() { return scenesPanel }, open: function() { scenesPanel.openPanel() }},
        {id: "movie", panel: function() { return movieEditor }, open: function() { movieEditor.openEditor() }},
        {id: "chemistry", panel: function() { return chemistryPanel }, open: function() { chemistryPanel.openPanel() }},
        {id: "system-information", panel: function() { return systemInfoPanel }, open: function() { openSystemInfo() }},
        {id: "trajectory-import", panel: function() { return trajectoryImport }, open: function() { trajectoryImport.openPanel() }},
        {id: "surface", panel: function() { return surfacePanel }, open: function() { surfacePanel.openPanel() }},
        {id: "volume", panel: function() { return volumePanel }, open: function() { volumePanel.openPanel() }}
    ]
    // Ids of the open panels, in the form `session save --ui-visible-panels` stores.
    function visiblePanels() {
        const names = []
        for (const entry of sessionPanels) if (entry.panel().visible) names.push(entry.id)
        return names.join(",")
    }
    // Closes every panel and opens the ones the loaded session recorded.
    function restoreSessionVisiblePanels() {
        for (const entry of sessionPanels) entry.panel().close()
        const wanted = viewport.sessionVisiblePanels.split(",")
        for (const entry of sessionPanels) if (wanted.indexOf(entry.id) >= 0) entry.open()
    }
    function run(text) {
        if (text.indexOf("ui:") === 0) { uiCommand(text); return ({ok: true, lines: []}) }
        output.append("cmd", "MolShredder> " + text)
        const result = viewport.executeCommandLine(text)
        commandCount += 1
        if (!result.ok) errorCount += 1
        else if (/^(invoke\s+"session (save|load)"|session (save|load))(\s|$)/.test(text)) {
            const found = /--path\s+("(?:[^"\\]|\\.)*"|\S+)/.exec(text)
            if (found) currentSessionPath = found[1].replace(/^"|"$/g, "").replace(/\\(.)/g, "$1")
            if (/session load/.test(text)) restoreSessionVisiblePanels()
        }
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
    // Makes the named object active (named selections and most panels act on the active object).
    function activateObject(name) {
        for (const item of viewport.objectItems) {
            if (item.name !== name) continue
            if (!item.active) viewport.executeCommandLine("object activate --id " + item.id)
            return true
        }
        return false
    }
    // Runs a command after making `owner` the active object (named selections belong to one object).
    function runIn(owner, text) {
        activateObject(owner)
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
            openStructure: "load --path ", openVolume: "volume load --path ",
            openSession: "session load --path ", saveStructure: "save --path ",
            saveImage: "image save --path "
        }
        if (pendingAction === "trajectoryPath") trajectoryImport.setPath(path)
        else if (pendingAction === "scriptPath") scriptPanel.setPath(path)
        else if (verbs[pendingAction]) run(verbs[pendingAction] + quote(path))
        else if (pendingAction === "saveSession") run("session save --path " + quote(path) + " --ui-visible-panels " + quote(visiblePanels()))
        else if (pendingAction === "exportResult")
            run("result export --id " + pendingResult + " --path " + quote(path) + " --output-format " + pendingResultFormat)
    }
    function perform(entry) {
        if (entry.command) { run(entry.command); return }
        if (entry.form) { forms.openFor(entry.form, entry.preset || ({})); return }
        switch (entry.action) {
        case "openStructure": chooseFile(qsTr("Open structure"), "openStructure", false, [qsTr("Structures (*.pdb *.ent *.cif *.mmcif *.bcif *.mmtf *.pdbqt *.mol *.mol2 *.sdf *.mae *.cml *.xyz *.gro *.pqr *.pse)"), qsTr("All files (*)")]); break
        case "openTrajectory": trajectoryImport.openPanel(); break
        case "openVolume": chooseFile(qsTr("Open volume"), "openVolume", false, [qsTr("Maps (*.dx *.mrc *.map *.ccp4 *.xplor *.brix *.phi *.grd)"), qsTr("All files (*)")]); break
        case "openSession": chooseFile(qsTr("Open session"), "openSession", false); break
        case "saveStructure": chooseFile(qsTr("Save structure"), "saveStructure", true, [qsTr("PDB (*.pdb)"), qsTr("mmCIF (*.cif)"), qsTr("All files (*)")]); break
        case "saveSession": chooseFile(qsTr("Save session"), "saveSession", true); break
        case "saveImage": chooseFile(qsTr("Save image"), "saveImage", true, [qsTr("PNG (*.png)"), qsTr("PPM (*.ppm)")]); break
        case "runScript": scriptPanel.openPanel(); break
        case "toggleOutput": toggleOutput(); break
        case "toggleSequence": toggleSequence(); break
        case "allCommands": forms.openFor("", ({})); break
        case "inspector": showInspector(entry.tab || 0); break
        case "movieEditor": movieEditor.openEditor(); break
        case "results": results.openResults(); break
        case "selectionEditor": selectionEditor.openEditor(); break
        case "surfaceOptions": surfacePanel.openPanel(); break
        case "volumes": volumePanel.openPanel(); break
        case "editAtoms": atomEditor.openEditor(); break
        case "camera": cameraPanel.openPanel(); break
        case "systemInfo": openSystemInfo(); break
        case "builder": moleculeBuilder.openBuilder(); break
        case "chemistry": chemistryPanel.openPanel(); break
        case "scenes": scenesPanel.openPanel(); break
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
            {text: qsTr("Select by expression…"), action: "selectionEditor", hint: "Ctrl+Shift+F"},
            {separator: true},
            {text: qsTr("Undo"), command: "edit undo", hint: "Ctrl+Z"},
            {text: qsTr("Redo"), command: "edit redo", hint: "Ctrl+Shift+Z"},
            {text: qsTr("Edit history"), command: "edit history"},
            {separator: true},
            {text: qsTr("Select all"), command: "select sele, all"},
            {text: qsTr("Clear selection"), command: "select sele, none"}
        ],
        "Build": [
            {text: qsTr("Molecule builder…"), action: "builder"},
            {text: qsTr("Edit atoms and bonds…"), action: "editAtoms", hint: "Ctrl+Shift+E"},
            {separator: true},
            {text: qsTr("Chemistry…"), action: "chemistry"}
        ],
        "Movie": [
            {text: qsTr("Movie editor…"), action: "movieEditor", hint: "Ctrl+E"},
            {separator: true},
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
            {text: qsTr("Surface options…"), action: "surfaceOptions"},
            {text: qsTr("Volumes…"), action: "volumes", hint: "Ctrl+Shift+U"},
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
            {text: qsTr("Inspector…"), action: "inspector", tab: 0, hint: "Ctrl+I"},
            {separator: true},
            {text: qsTr("Set render setting…"), form: "setting set"},
            {text: qsTr("Show setting…"), form: "setting get"},
            {text: qsTr("List settings"), command: "setting list"},
            {text: qsTr("Reset setting…"), form: "setting reset"}
        ],
        "Scene": [
            {text: qsTr("Camera…"), action: "camera", hint: "Ctrl+Shift+J"},
            {text: qsTr("Views and scenes…"), action: "scenes", hint: "Ctrl+Shift+N"},
            {separator: true},
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
            {text: qsTr("Results…"), action: "results", hint: "Ctrl+R"},
            {separator: true},
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
            {text: qsTr("System information…"), action: "systemInfo"},
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

                // Atom labels and measurement labels are text over the viewport; an atom label can be dragged
                // (stored as label_placement_offset by the setting operation).
                Repeater {
                    model: viewport.analysisLabelItems
                    Text {
                        id: labelText
                        required property var modelData
                        x: modelData.x + 8
                        y: modelData.y - height - 6
                        text: modelData.text
                        color: modelData.color
                        font.pixelSize: modelData.size
                        font.bold: true
                        style: Text.Outline
                        styleColor: "#cc050812"
                        z: 20
                        MouseArea {
                            anchors.fill: parent
                            enabled: labelText.modelData.atomId > 0
                            cursorShape: enabled ? Qt.SizeAllCursor : Qt.ArrowCursor
                            property point pressPoint
                            onPressed: function(mouse) { pressPoint = mapToItem(null, mouse.x, mouse.y) }
                            onReleased: function(mouse) {
                                const releasePoint = mapToItem(null, mouse.x, mouse.y)
                                viewport.moveLabel(labelText.modelData.objectId, labelText.modelData.atomId,
                                                   releasePoint.x - pressPoint.x, releasePoint.y - pressPoint.y)
                            }
                        }
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
            onInspectRequested: function(name) { root.runIn(name, "ui:inspector") }
            onDeleteRequested: function(name) { deleteDialog.ask(name) }
            onSelectionEditRequested: function(name, owner) {
                let expression = ""
                for (const row of viewport.objectTree) if (row.kind === "selection" && row.name === name && row.owner === owner) expression = row.expression
                if (owner.length > 0) activateObject(owner)
                selectionEditor.openEditor(name, expression)
            }
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

    // ---- background behavior ----------------------------------------------------------------------------------------
    Timer {
        id: sessionAutosaveTimer
        interval: 120000
        repeat: true
        running: root.currentSessionPath.length > 0 && viewport.objectItems.length > 0
        onTriggered: root.autosaveNow()
    }
    // Steps the movie at its frame rate while it plays (the trajectory has its own clock in the viewport).
    Timer {
        id: moviePlaybackTimer
        interval: viewport.movieState.configured && viewport.movieState.fps > 0 ? Math.max(4, Math.round(1000 / viewport.movieState.fps)) : 33
        repeat: true
        running: viewport.movieState.configured === true && viewport.movieState.playing === true
        onTriggered: viewport.stepMovie(1)
    }

    // ---- dialogs and shortcuts ------------------------------------------------------------------------------------
    FileDialog {
        id: fileDialog
        onAccepted: root.onFileChosen(root.localPath(selectedFile))
    }
    NextInspector {
        id: inspector
        viewport: viewport
        onRun: function(text) { root.run(text) }
    }
    NextTrajectoryImport {
        id: trajectoryImport
        viewport: viewport
        onRun: function(text) { root.run(text) }
        onBrowseRequested: chooseFile(qsTr("Open trajectory"), "trajectoryPath", false, [qsTr("All files (*)")])
    }
    NextScript {
        id: scriptPanel
        viewport: viewport
        onBrowseRequested: chooseFile(qsTr("Run script"), "scriptPath", false, [qsTr("Python (*.py)"), qsTr("All files (*)")])
        onInfo: function(text) { output.append("dim", text) }
    }
    NextSystemInfo {
        id: systemInfoPanel
        viewport: viewport
    }
    NextCamera {
        id: cameraPanel
        viewport: viewport
        onRun: function(text) { root.run(text) }
    }
    NextBuilder {
        id: moleculeBuilder
        viewport: viewport
        onRun: function(text) { root.run(text) }
    }
    NextChemistry {
        id: chemistryPanel
        viewport: viewport
        onRun: function(text) { root.run(text) }
    }
    NextEditor {
        id: atomEditor
        viewport: viewport
        onRun: function(text) { root.run(text) }
    }
    NextVolume {
        id: volumePanel
        viewport: viewport
        onRun: function(text) { root.run(text) }
        onFormRequested: function(name, preset) { forms.openFor(name, preset) }
        onOpenRequested: root.perform({action: "openVolume"})
    }
    NextSurface {
        id: surfacePanel
        viewport: viewport
        onRun: function(text) { root.run(text) }
    }
    NextScenes {
        id: scenesPanel
        viewport: viewport
        onRun: function(text) { root.run(text) }
        onFormRequested: function(name, preset) { forms.openFor(name, preset) }
    }
    NextSelectionEditor {
        id: selectionEditor
        viewport: viewport
        onRun: function(text) { root.run(text) }
    }
    NextResults {
        id: results
        viewport: viewport
        onRun: function(text) { root.run(text) }
        onInfo: function(text) { output.append("dim", text) }
        onExportRequested: function(id, format) {
            pendingResult = id
            pendingResultFormat = format
            chooseFile(qsTr("Export result"), "exportResult", true, format === "csv" ? [qsTr("CSV (*.csv)")] : [qsTr("JSON (*.json)")])
        }
    }
    NextMovieEditor {
        id: movieEditor
        viewport: viewport
        onRun: function(text) { root.run(text) }
        onFormRequested: function(name, preset) { forms.openFor(name, preset) }
    }
    NextCommandForm {
        id: forms
        viewport: viewport
        onRun: function(text) { root.run(text) }
    }
    Popup {
        id: deleteDialog
        objectName: "deleteDialog"
        property string target: ""
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 360
        padding: 14
        background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }
        function ask(name) { target = name; open() }
        function confirm() { root.run("delete " + root.quote(target)); close() }
        Column {
            spacing: 10
            width: parent.width
            Text { width: parent.width; wrapMode: Text.Wrap; text: qsTr("Delete %1?").arg(deleteDialog.target); color: NextTheme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Text { width: parent.width; wrapMode: Text.Wrap; text: qsTr("The object and its selections are removed from the workspace."); color: NextTheme.muted; font.pixelSize: NextTheme.fontSize }
            Row {
                spacing: 8
                layoutDirection: Qt.RightToLeft
                width: parent.width
                NextButton { objectName: "deleteConfirmButton"; text: qsTr("Delete"); checked: true; onClicked: deleteDialog.confirm() }
                NextButton { text: qsTr("Cancel"); onClicked: deleteDialog.close() }
            }
        }
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
    Shortcut { sequence: "Ctrl+I"; onActivated: root.showInspector(0) }
    Shortcut { sequence: "Ctrl+Shift+E"; onActivated: atomEditor.openEditor() }
    Shortcut { sequence: "Ctrl+Shift+U"; onActivated: volumePanel.openPanel() }
    Shortcut { sequence: "Ctrl+Shift+J"; onActivated: cameraPanel.openPanel() }
    Shortcut { sequence: "Ctrl+Shift+N"; onActivated: scenesPanel.openPanel() }
    Shortcut { sequence: "Ctrl+Shift+F"; onActivated: selectionEditor.openEditor() }
    Shortcut { sequence: "Ctrl+R"; onActivated: results.openResults() }
    Shortcut { sequence: "Ctrl+E"; onActivated: movieEditor.openEditor() }
    Shortcut { sequence: "Ctrl+K"; onActivated: root.perform({action: "allCommands"}) }
    Shortcut { sequence: "Ctrl+L"; onActivated: { if (!prefs.outputVisible) {} output.focusInput() } }
    Shortcut { sequence: StandardKey.Quit; onActivated: Qt.quit() }
}
