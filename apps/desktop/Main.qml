import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import MolShredder.Desktop

Window {
    id: root
    property string trajectoryCoordinateUnit: "angstrom"
    property string trajectoryMapping: "exact"
    property string trajectoryAtomMap: ""
    property url pendingScriptUrl
    property var systemInfoData: ({})
    property string systemInfoPanelSourceJson: ""
    property string systemInfoPanelError: ""
    property var analysisExportResultId: 0
    property string analysisExportFormat: "json"
    readonly property var fileOpenMetadata: localization.actionMetadata("file.open")

    function systemInfoValue(group, key) {
        if (!root.systemInfoData || !root.systemInfoData[group])
            return qsTr("Not reported")
        const value = root.systemInfoData[group][key]
        return value === null || value === undefined || value === ""
               ? qsTr("Not reported") : String(value)
    }

    function graphicsInfoValue(key) {
        if (!root.systemInfoData || !root.systemInfoData.runtime ||
                !root.systemInfoData.runtime.graphics)
            return qsTr("Not reported")
        const value = root.systemInfoData.runtime.graphics[key]
        return value === null || value === undefined || value === ""
               ? qsTr("Not reported") : String(value)
    }

    function openSystemInfo() {
        root.systemInfoPanelSourceJson = viewport.systemInfoJson()
        try {
            const envelope = JSON.parse(root.systemInfoPanelSourceJson)
            if (envelope.status !== "ok" || !envelope.data)
                throw new Error("system info operation did not return data")
            root.systemInfoData = envelope.data
            root.systemInfoPanelError = ""
        } catch (error) {
            root.systemInfoData = ({})
            root.systemInfoPanelError = String(error)
        }
        systemInfoOverlay.visible = true
    }

    function openViews() {
        const projection = viewport.projectionModeText()
        viewsOverlay.projectionModeIndex =
            viewsOverlay.projectionModes.indexOf(projection)
        if (viewsOverlay.projectionModeIndex < 0)
            viewsOverlay.projectionModeIndex = 0
        projectionFovInput.text = String(viewport.fieldOfViewDegrees())
        viewsOverlay.stereoEnabled = viewport.stereoEnabled()
        const stereoMode = viewport.stereoModeText()
        viewsOverlay.stereoModeIndex =
            viewsOverlay.stereoModes.indexOf(stereoMode)
        if (viewsOverlay.stereoModeIndex < 0)
            viewsOverlay.stereoModeIndex = 0
        viewsOverlay.stereoSwapEyes = viewport.stereoSwapEyes()
        const anaglyphMode = viewport.anaglyphModeText()
        viewsOverlay.anaglyphModeIndex =
            viewsOverlay.anaglyphModes.indexOf(anaglyphMode)
        if (viewsOverlay.anaglyphModeIndex < 0)
            viewsOverlay.anaglyphModeIndex = 4
        stereoShiftInput.text = String(viewport.stereoShiftPercent())
        stereoAngleInput.text = String(viewport.stereoAngleScale())
        viewsOverlay.visible = true
        viewsOverlay.clipRange = viewport.clipRangeText()
        viewNameInput.forceActiveFocus()
    }

    function openRenderSettings() {
        renderSettingsOverlay.visible = true
        renderSettingNameInput.forceActiveFocus()
        renderSettingResult.text = qsTr("Choose a P0 setting, scope and value. Atom/bond targets are 1-based.")
    }

    function openAnalyze() {
        analysisOverlay.visible = true
        analysisPrimaryInput.forceActiveFocus()
    }

    function openCommandPalette() {
        commandPaletteQuery.text = ""
        commandPaletteOverlay.visible = true
        commandPaletteQuery.forceActiveFocus()
    }
    width: 1080
    height: 720
    visible: true
    color: "#050812"
    title: qsTr("MolShredder Molecular Viewer")

    Action {
        id: fileOpenAction
        objectName: "fileOpenAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.fileOpenMetadata.label)
        }
        shortcut: root.fileOpenMetadata.shortcut === "standard.open"
                  ? StandardKey.Open : ""
        onTriggered: structureDialog.open()
    }

    Action {
        id: showCommandPaletteAction
        objectName: "showCommandPaletteAction"
        text: qsTr("Command Palette")
        shortcut: Qt.platform.os === "osx" ? "Meta+Shift+P" : "Ctrl+Shift+P"
        onTriggered: root.openCommandPalette()
    }

    MenuBar {
        id: mainMenuBar
        objectName: "mainMenuBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        z: 50

        Menu {
            id: fileMenu
            objectName: "fileMenu"
            title: qsTr("File")

            MenuItem {
                objectName: "fileOpenMenuItem"
                action: fileOpenAction
            }
        }
    }

    Rectangle {
        id: commandPaletteOverlay
        objectName: "commandPaletteOverlay"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: mainMenuBar.bottom
        anchors.topMargin: 18
        width: Math.min(560, parent.width - 40)
        height: 126
        visible: false
        radius: 10
        color: "#f2101827"
        border.color: "#69aef0"
        z: 60

        TextInput {
            id: commandPaletteQuery
            objectName: "commandPaletteQuery"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            height: 34
            color: "#eef6ff"
            selectByMouse: true
            font.pixelSize: 15
            Keys.onEscapePressed: commandPaletteOverlay.visible = false

            Text {
                anchors.fill: parent
                text: qsTr("Search actions")
                visible: parent.text.length === 0
                color: "#71849c"
                font.pixelSize: 15
            }
        }

        Rectangle {
            id: commandPaletteFileOpen
            objectName: "commandPaletteFileOpen"
            property string actionId: root.fileOpenMetadata.id
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            height: 56
            radius: 7
            visible: commandPaletteQuery.text.length === 0 ||
                     fileOpenAction.text.toLowerCase().indexOf(
                         commandPaletteQuery.text.toLowerCase()) >= 0 ||
                     actionId.indexOf(commandPaletteQuery.text.toLowerCase()) >= 0
            color: commandPaletteEntryMouse.containsMouse ? "#284767" : "#172235"
            border.color: "#405270"

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 12
                anchors.topMargin: 8
                text: fileOpenAction.text
                color: "#eef6ff"
                font.pixelSize: 14
                font.bold: true
            }

            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 12
                anchors.bottomMargin: 7
                text: {
                    localization.currentLanguage
                    return localization.translateUi(root.fileOpenMetadata.status)
                }
                color: "#91a8c2"
                font.pixelSize: 12
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 12
                text: fileOpenAction.shortcut
                color: "#91a8c2"
                font.pixelSize: 12
            }

            MouseArea {
                id: commandPaletteEntryMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    commandPaletteOverlay.visible = false
                    fileOpenAction.trigger()
                }
            }
        }
    }

    MolecularViewport {
        id: viewport
        objectName: "molecularViewport"
        anchors.fill: parent
        angle: 0

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property real previousX: 0
            property real previousY: 0
            property real pressX: 0
            property real pressY: 0
            property bool dragged: false
            onPressed: function(mouse) {
                previousX = mouse.x
                previousY = mouse.y
                pressX = mouse.x
                pressY = mouse.y
                dragged = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                const dx = mouse.x - previousX
                const dy = mouse.y - previousY
                if (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) > 4)
                    dragged = true
                if ((mouse.buttons & Qt.RightButton) !== 0)
                    viewport.pan(dx, dy)
                else
                    viewport.orbit(dx, dy)
                previousX = mouse.x
                previousY = mouse.y
            }
            onWheel: function(wheel) {
                viewport.dolly(-wheel.angleDelta.y / 120.0)
                wheel.accepted = true
            }
            onClicked: function(mouse) {
                if (!dragged && mouse.button === Qt.LeftButton)
                    viewport.pickAt(mouse.x, mouse.y)
            }
            onDoubleClicked: viewport.resetViewAnimated(0.35, 1)
        }
    }

    Repeater {
        model: viewport.analysisLabelItems
        Text {
            required property var modelData
            x: modelData.x + 8
            y: modelData.y - height - 6
            text: modelData.text
            color: modelData.color
            font.pixelSize: 13
            font.bold: true
            style: Text.Outline
            styleColor: "#cc050812"
            z: 20
        }
    }

    FileDialog {
        id: structureDialog
        title: qsTr("Open molecular structure or scalar volume")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Molecular data (*.pdb *.ent *.cif *.mmcif *.bcif *.pqr *.mol *.mol2 *.psf *.prmtop *.parm7 *.top *.sdf *.sd *.gro *.g96 *.vtf *.xyz *.dx *.mrc *.map *.ccp4 *.mrcs)"),
                      qsTr("OpenDX scalar volumes (*.dx)"),
                      qsTr("MRC/CCP4 scalar volumes (*.mrc *.map *.ccp4 *.mrcs)"),
                      qsTr("All files (*)")]
        onAccepted: viewport.loadStructures(selectedFiles)
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save active molecular coordinates")
        fileMode: FileDialog.SaveFile
        defaultSuffix: selectedNameFilter.index === 1 ? "pdb"
                       : selectedNameFilter.index === 2 ? "cif"
                       : selectedNameFilter.index === 3 ? "gro"
                       : selectedNameFilter.index === 4 ? "g96"
                       : selectedNameFilter.index === 5 ? "pqr"
                       : selectedNameFilter.index === 6 ? "mol"
                       : selectedNameFilter.index === 7 ? "mol2"
                       : selectedNameFilter.index === 8 ? "psf"
                       : selectedNameFilter.index === 9 ? "sdf" : "xyz"
        nameFilters: [qsTr("XYZ coordinates (*.xyz)"),
                      qsTr("Protein Data Bank 3.3 (*.pdb *.ent)"),
                      qsTr("PDBx/mmCIF (*.cif *.mmcif)"),
                      qsTr("BinaryCIF (*.bcif)"),
                      qsTr("GROMACS structure/trajectory (*.gro)"),
                      qsTr("GROMOS-96 structure/trajectory (*.g96)"),
                      qsTr("PQR electrostatics (*.pqr)"),
                      qsTr("MDL MOL V2000 (*.mol)"),
                      qsTr("Tripos MOL2 (*.mol2)"),
                      qsTr("CHARMM/NAMD PSF topology (*.psf)"),
                      qsTr("SDF V2000 record (*.sdf *.sd)")]
        onAccepted: viewport.saveStructure(selectedFile, false)
    }

    FileDialog {
        id: trajectoryDialog
        title: qsTr("Attach molecular dynamics trajectory")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("MD trajectories/restarts (*.dcd *.xtc *.trr *.mdcrd *.crd *.nc *.ncdf *.netcdf *.h5md *.rst7 *.restrt *.inpcrd *.inprst *.lammpstrj *.lammpstraj *.dump *.binpos)"),
                      qsTr("All files (*)")]
        onAccepted: viewport.loadTrajectory(selectedFile,
                                              root.trajectoryCoordinateUnit,
                                              root.trajectoryMapping,
                                              root.trajectoryAtomMap)
    }

    FileDialog {
        id: scriptDialog
        title: qsTr("Run a local Python script")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Python scripts (*.py)")]
        onAccepted: {
            root.pendingScriptUrl = selectedFile
            scriptTrustOverlay.visible = true
        }
    }


    FileDialog {
        id: analysisExportDialog
        title: qsTr("Export persistent analysis result")
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.analysisExportFormat
        nameFilters: root.analysisExportFormat === "csv"
                     ? [qsTr("CSV table (*.csv)")] : [qsTr("JSON result (*.json)")]
        onAccepted: viewport.exportAnalysisResult(
                        root.analysisExportResultId, selectedFile,
                        root.analysisExportFormat)
    }

    component ToolbarButton: Rectangle {
        id: toolbarButtonRoot
        required property string label
        required property var action
        property string actionId: ""
        property string toolTip: ""
        property bool selected: false
        width: buttonText.implicitWidth + 22
        height: 34
        radius: 7
        color: selected ? "#285f99" : mouse.containsMouse ? "#24354c" : "#172235"
        border.color: selected ? "#69aef0" : "#405270"

        Text {
            id: buttonText
            anchors.centerIn: parent
            text: parent.label
            color: "#eef6ff"
            font.pixelSize: 14
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.action()
        }

        ToolTip.visible: mouse.containsMouse && toolbarButtonRoot.toolTip.length > 0
        ToolTip.text: toolbarButtonRoot.toolTip
    }

    component InfoRow: Item {
        required property string label
        required property string value
        width: 548
        height: 27

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 155
            text: parent.label
            color: "#91a8c2"
            font.pixelSize: 13
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 165
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: parent.value
            color: "#e5f1ff"
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 20
        anchors.topMargin: 58
        width: toolbar.width + 28
        height: toolbar.height + 22
        radius: 8
        color: "#cc101827"
        border.color: "#405270"

        Row {
            id: toolbar
            anchors.centerIn: parent
            spacing: 8

            ToolbarButton {
                objectName: "fileOpenToolbarButton"
                actionId: root.fileOpenMetadata.id
                label: fileOpenAction.text
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(root.fileOpenMetadata.status)
                }
                action: function() { fileOpenAction.trigger() }
            }
            ToolbarButton {
                label: qsTr("Trajectory")
                action: function() { trajectoryDialog.open() }
            }
            ToolbarButton {
                label: root.trajectoryCoordinateUnit === "angstrom"
                       ? qsTr("Traj Å") : qsTr("Traj nm")
                action: function() {
                    root.trajectoryCoordinateUnit =
                        root.trajectoryCoordinateUnit === "angstrom"
                        ? "nanometer" : "angstrom"
                }
            }
            ToolbarButton {
                objectName: "trajectoryMappingButton"
                label: root.trajectoryMapping === "exact" ? qsTr("Map exact") :
                       root.trajectoryMapping === "explicit" ? qsTr("Map IDs") :
                       qsTr("Map index")
                selected: root.trajectoryMapping !== "index"
                action: function() {
                    root.trajectoryMapping =
                        root.trajectoryMapping === "index" ? "exact" :
                        root.trajectoryMapping === "exact" ? "explicit" :
                        "index"
                }
            }
            Rectangle {
                objectName: "trajectoryAtomMapInputContainer"
                visible: root.trajectoryMapping === "explicit"
                width: visible ? 150 : 0
                height: 34
                radius: 6
                color: "#172235"
                border.color: "#506889"
                TextInput {
                    objectName: "trajectoryAtomMapInput"
                    anchors.fill: parent
                    anchors.margins: 8
                    text: root.trajectoryAtomMap
                    color: "#eef6ff"
                    selectByMouse: true
                    clip: true
                    onTextChanged: root.trajectoryAtomMap = text
                    Text {
                        anchors.fill: parent
                        text: qsTr("stable IDs: 3,2,1")
                        visible: parent.text.length === 0
                        color: "#71849c"
                        font.pixelSize: 11
                    }
                }
            }
            ToolbarButton {
                label: qsTr("Save")
                action: function() { saveDialog.open() }
            }
            ToolbarButton {
                label: qsTr("Run Script")
                action: function() { scriptDialog.open() }
            }
            ToolbarButton {
                label: qsTr("Views")
                action: function() { root.openViews() }
            }
            ToolbarButton {
                label: qsTr("System")
                action: function() { root.openSystemInfo() }
            }
            ToolbarButton {
                label: qsTr("Settings")
                action: function() { root.openRenderSettings() }
            }
            ToolbarButton {
                label: qsTr("Analyze")
                action: root.openAnalyze
            }
            ToolbarButton {
                label: localization.currentLanguage === "ko" ? "한국어" : "English"
                selected: localization.currentLanguage === "ko"
                action: function() {
                    localization.setLanguage(localization.currentLanguage === "ko"
                                             ? "en" : "ko")
                }
            }
            ToolbarButton {
                label: qsTr("Lines")
                selected: viewport.representation === "lines"
                action: function() { viewport.setRepresentation("lines") }
            }
            ToolbarButton {
                label: qsTr("Sticks")
                selected: viewport.representation === "sticks"
                action: function() { viewport.setRepresentation("sticks") }
            }
            ToolbarButton {
                label: qsTr("Spheres")
                selected: viewport.representation === "spheres"
                action: function() { viewport.setRepresentation("spheres") }
            }
            ToolbarButton {
                label: qsTr("Ribbon")
                selected: viewport.representation === "ribbon"
                action: function() { viewport.setRepresentation("ribbon") }
            }
            ToolbarButton {
                label: qsTr("Cartoon")
                selected: viewport.representation === "cartoon"
                action: function() { viewport.setRepresentation("cartoon") }
            }
            ToolbarButton {
                label: qsTr("Show")
                action: function() {
                    viewport.applyRepresentationVisibility("show", "all")
                }
            }
            ToolbarButton {
                label: qsTr("Hide")
                action: function() {
                    viewport.applyRepresentationVisibility("hide", "all")
                }
            }
            ToolbarButton {
                label: qsTr("As")
                action: function() {
                    viewport.applyRepresentationVisibility("as", "all")
                }
            }
            ToolbarButton {
                label: qsTr("Toggle")
                action: function() {
                    viewport.applyRepresentationVisibility("toggle", "all")
                }
            }
        }
    }

    Rectangle {
        id: scriptOutputPanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 20
        anchors.topMargin: 88
        width: Math.min(540, parent.width - 330)
        height: 150
        visible: viewport.scriptOutput.length > 0
        radius: 8
        color: "#ee101827"
        border.color: "#506889"
        z: 5

        Text {
            anchors.left: parent.left
            anchors.right: closeScriptOutput.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 12
            text: viewport.scriptOutput
            color: "#d8e8fa"
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 8
            elide: Text.ElideRight
        }

        Text {
            id: closeScriptOutput
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            text: "×"
            color: closeScriptMouse.containsMouse ? "#ffffff" : "#9fb3cc"
            font.pixelSize: 20

            MouseArea {
                id: closeScriptMouse
                anchors.fill: parent
                anchors.margins: -8
                hoverEnabled: true
                onClicked: viewport.clearScriptOutput()
            }
        }
    }

    Rectangle {
        id: trajectoryPanel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: Math.min(parent.width - 420, 610)
        height: 120
        visible: viewport.hasTrajectory
        radius: 9
        color: "#e6101827"
        border.color: "#506889"
        z: 3

        Row {
            id: playbackControls
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 7

            ToolbarButton {
                label: "|◀"
                action: function() { viewport.seekTrajectory(0) }
            }
            ToolbarButton {
                label: "◀"
                action: function() { viewport.stepTrajectory(-1) }
            }
            ToolbarButton {
                label: viewport.trajectoryPlaying ? qsTr("Pause") : qsTr("Play")
                selected: viewport.trajectoryPlaying
                action: function() {
                    viewport.setTrajectoryPlaying(!viewport.trajectoryPlaying)
                }
            }
            ToolbarButton {
                label: "▶"
                action: function() { viewport.stepTrajectory(1) }
            }
            ToolbarButton {
                label: "▶|"
                action: function() {
                    viewport.seekTrajectory(Math.max(0, viewport.trajectoryFrameCount - 1))
                }
            }
            ToolbarButton {
                label: viewport.playbackMode
                selected: viewport.playbackMode !== "once"
                action: function() {
                    const next = viewport.playbackMode === "once" ? "loop" :
                                 viewport.playbackMode === "loop" ? "rock" : "once"
                    viewport.setPlaybackMode(next)
                }
            }
            ToolbarButton {
                label: viewport.playbackDirection === "forward" ? qsTr("Forward") : qsTr("Reverse")
                selected: viewport.playbackDirection === "reverse"
                action: function() {
                    viewport.setPlaybackDirection(viewport.playbackDirection === "forward" ?
                                                  "reverse" : "forward")
                }
            }
            ToolbarButton {
                label: Number(viewport.trajectoryFps).toFixed(0) + " fps"
                action: function() {
                    const fps = viewport.trajectoryFps < 5 ? 5 :
                                viewport.trajectoryFps < 15 ? 15 :
                                viewport.trajectoryFps < 30 ? 30 :
                                viewport.trajectoryFps < 60 ? 60 : 1
                    viewport.setTrajectoryFps(fps)
                }
            }
            ToolbarButton {
                objectName: "trajectoryTaskCancelButton"
                visible: viewport.trajectoryTaskRunning
                label: qsTr("Cancel")
                action: function() { viewport.cancelTrajectoryTask() }
            }
        }

        Text {
            id: frameLabel
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 13
            anchors.topMargin: 14
            text: viewport.trajectoryTaskRunning ?
                  viewport.trajectoryTaskStage + " " +
                  Math.round(viewport.trajectoryTaskProgress * 100) + "%" :
                  qsTr("Frame %1 / %2").arg(viewport.trajectoryFrame).arg(
                      Math.max(0, viewport.trajectoryFrameCount - 1))
            color: "#d8e8fa"
            font.pixelSize: 13
        }

        Rectangle {
            id: timelineTrack
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 15
            anchors.rightMargin: 15
            anchors.bottomMargin: 15
            height: 14
            radius: 7
            color: "#27364a"
            border.color: "#516984"

            Rectangle {
                objectName: "trajectoryTaskProgress"
                visible: viewport.trajectoryTaskRunning
                anchors.left: parent.left
                anchors.top: parent.top
                height: 3
                width: parent.width * viewport.trajectoryTaskProgress
                radius: 2
                color: "#7ad7ff"
                z: 3
            }

            Rectangle {
                width: viewport.trajectoryFrameCount <= 1 ? 0 :
                       parent.width * viewport.trajectoryFrame /
                       (viewport.trajectoryFrameCount - 1)
                height: parent.height
                radius: parent.radius
                color: "#3d88c7"
            }

            Rectangle {
                x: viewport.trajectoryFrameCount <= 1 ? 0 :
                   (parent.width - width) * viewport.trajectoryFrame /
                   (viewport.trajectoryFrameCount - 1)
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 9
                color: "#dcefff"
                border.color: "#64aef0"
            }

            MouseArea {
                anchors.fill: parent
                anchors.topMargin: -7
                anchors.bottomMargin: -7
                onReleased: function(mouse) {
                    if (viewport.trajectoryFrameCount <= 1)
                        return
                    const ratio = Math.max(0, Math.min(1, mouse.x / width))
                    const frame = Math.round(ratio * (viewport.trajectoryFrameCount - 1))
                    viewport.seekTrajectory(frame)
                }
            }
        }
    }

    Rectangle {
        id: volumePanel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: 430
        height: 72
        visible: viewport.hasVolume && !viewport.hasTrajectory
        radius: 9
        color: "#e6101827"
        border.color: "#506889"
        z: 3

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            text: qsTr("Contour %1").arg(Number(viewport.volumeLevel).toPrecision(5))
            color: "#d8e8fa"
            font.pixelSize: 14
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 12
            spacing: 8

            ToolbarButton {
                label: "−"
                action: function() {
                    const step = (viewport.volumeMaximum - viewport.volumeMinimum) / 20
                    viewport.setVolumeIsosurface(
                        Math.max(viewport.volumeMinimum, viewport.volumeLevel - step))
                }
            }
            ToolbarButton {
                label: qsTr("Midpoint")
                action: function() {
                    viewport.setVolumeIsosurface(
                        (viewport.volumeMinimum + viewport.volumeMaximum) / 2)
                }
            }
            ToolbarButton {
                label: "+"
                action: function() {
                    const step = (viewport.volumeMaximum - viewport.volumeMinimum) / 20
                    viewport.setVolumeIsosurface(
                        Math.min(viewport.volumeMaximum, viewport.volumeLevel + step))
                }
            }
        }
    }

    Rectangle {
        id: objectPanel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 20
        anchors.topMargin: 90
        width: 430
        height: Math.min(330, 52 + viewport.objectItems.length * 44)
        visible: viewport.objectItems.length > 0
        radius: 8
        color: "#dd101827"
        border.color: "#405270"
        clip: true

        Text {
            id: objectPanelTitle
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 14
            text: qsTr("Objects")
            color: "#eef6ff"
            font.pixelSize: 15
            font.bold: true
        }

        Flickable {
            id: objectList
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: objectPanelTitle.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: 8
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.bottomMargin: 8
            contentHeight: objectColumn.height
            clip: true

            Column {
                id: objectColumn
                width: objectList.width
                spacing: 4

                Repeater {
                    model: viewport.objectItems

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        property bool editingName: false
                        property bool deleteArmed: false
                        width: objectColumn.width
                        height: 40
                        radius: 6
                        color: modelData.active ? "#2a527a" : rowMouse.containsMouse ? "#22344c" : "#172235"
                        border.color: modelData.active ? "#69aef0" : "#354a66"

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: viewport.activateObject(parent.modelData.id)
                        }

                        Rectangle {
                            id: visibilityToggle
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 9
                            width: 22
                            height: 22
                            radius: 5
                            color: parent.modelData.visible ? "#3977ad" : "#202a38"
                            border.color: parent.modelData.visible ? "#8ac7f5" : "#536176"
                            z: 2

                            Text {
                                anchors.centerIn: parent
                                text: visibilityToggle.parent.modelData.visible ? "●" : "○"
                                color: "#eef6ff"
                                font.pixelSize: 12
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: function(mouse) {
                                    viewport.setObjectVisible(visibilityToggle.parent.modelData.id,
                                                              !visibilityToggle.parent.modelData.visible)
                                    mouse.accepted = true
                                }
                            }
                        }

                        Text {
                            id: objectName
                            anchors.left: visibilityToggle.right
                            anchors.right: renameButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 9
                            anchors.rightMargin: 5
                            text: parent.modelData.name + " · " +
                                  qsTr("%1 atoms").arg(parent.modelData.atoms)
                            visible: !parent.editingName
                            color: parent.modelData.visible ? "#e5f1ff" : "#8291a6"
                            elide: Text.ElideRight
                            font.pixelSize: 13
                        }

                        Rectangle {
                            id: objectNameEditorContainer
                            objectName: "objectNameEditor"
                            anchors.left: visibilityToggle.right
                            anchors.right: renameButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 6
                            anchors.rightMargin: 4
                            height: 30
                            visible: parent.editingName
                            radius: 5
                            color: "#101827"
                            border.color: "#69aef0"
                            z: 3
                            TextInput {
                                id: objectNameEditor
                                anchors.fill: parent
                                anchors.margins: 6
                                text: objectNameEditorContainer.parent.modelData.name
                                color: "#eef6ff"
                                selectByMouse: true
                                onAccepted: {
                                    if (viewport.renameObject(objectNameEditorContainer.parent.modelData.id, text))
                                        objectNameEditorContainer.parent.editingName = false
                                }
                            }
                        }

                        Rectangle {
                            id: renameButton
                            objectName: "objectRenameButton"
                            anchors.right: upButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34
                            height: 28
                            radius: 5
                            color: "#28445f"
                            border.color: "#58799a"
                            z: 3
                            Text { anchors.centerIn: parent; text: "✎"; color: "#eef6ff" }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    renameButton.parent.editingName = true
                                    objectNameEditor.forceActiveFocus()
                                    objectNameEditor.selectAll()
                                }
                            }
                        }

                        Rectangle {
                            id: upButton
                            objectName: "objectMoveUpButton"
                            anchors.right: downButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 28
                            enabled: parent.index > 0
                            opacity: enabled ? 1.0 : 0.35
                            radius: 5
                            color: "#28445f"
                            border.color: "#58799a"
                            z: 3
                            Text { anchors.centerIn: parent; text: "↑"; color: "#eef6ff" }
                            MouseArea {
                                anchors.fill: parent
                                enabled: upButton.enabled
                                onClicked: viewport.reorderObject(upButton.parent.modelData.id,
                                                                  upButton.parent.index)
                            }
                        }

                        Rectangle {
                            id: downButton
                            objectName: "objectMoveDownButton"
                            anchors.right: deleteButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 28
                            enabled: parent.index + 1 < viewport.objectItems.length
                            opacity: enabled ? 1.0 : 0.35
                            radius: 5
                            color: "#28445f"
                            border.color: "#58799a"
                            z: 3
                            Text { anchors.centerIn: parent; text: "↓"; color: "#eef6ff" }
                            MouseArea {
                                anchors.fill: parent
                                enabled: downButton.enabled
                                onClicked: viewport.reorderObject(downButton.parent.modelData.id,
                                                                  downButton.parent.index + 2)
                            }
                        }

                        Rectangle {
                            id: deleteButton
                            objectName: "objectDeleteButton"
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 28
                            radius: 5
                            color: parent.deleteArmed ? "#a13e4f" : "#63313b"
                            border.color: "#a85b6a"
                            z: 3
                            Text { anchors.centerIn: parent; text: deleteButton.parent.deleteArmed ? "?" : "×"; color: "#fff0f2" }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (deleteButton.parent.deleteArmed)
                                        viewport.deleteObject(deleteButton.parent.modelData.id)
                                    else {
                                        deleteButton.parent.deleteArmed = true
                                        deleteReset.restart()
                                    }
                                }
                            }
                            Timer {
                                id: deleteReset
                                interval: 3000
                                onTriggered: deleteButton.parent.deleteArmed = false
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 20
        anchors.bottomMargin: viewport.hasTrajectory ? 132 : viewport.hasVolume ? 108 : 20
        width: statusLabel.implicitWidth + 28
        height: statusLabel.implicitHeight + 18
        radius: 8
        color: "#cc101827"
        border.color: "#405270"

        Text {
            id: statusLabel
            anchors.centerIn: parent
            color: "#d8e8fa"
            text: viewport.statusText
            font.pixelSize: 14
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 20
        anchors.bottomMargin: viewport.hasTrajectory ? 132 : viewport.hasVolume ? 108 : 20
        width: selectionLabel.implicitWidth + 28
        height: selectionLabel.implicitHeight + 18
        radius: 8
        color: "#cc101827"
        border.color: "#405270"

        Text {
            id: selectionLabel
            anchors.centerIn: parent
            color: "#f4d58d"
            text: viewport.selectionText
            font.pixelSize: 14
        }
    }

    Rectangle {
        id: analysisOverlay
        objectName: "analysisOverlay"
        property int modeIndex: 0
        property var modes: [qsTr("Centroid"), qsTr("Center of mass"),
                             qsTr("Distance"), qsTr("Contacts"),
                             qsTr("Trajectory RMSD")]
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 75

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: 760
            height: 610
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 11

                Row {
                    width: parent.width
                    spacing: 12
                    Text {
                        width: 620
                        text: qsTr("Analyze · persistent results")
                        color: "#eef6ff"
                        font.pixelSize: 20
                        font.bold: true
                    }
                    ToolbarButton {
                        label: qsTr("Close")
                        action: function() { analysisOverlay.visible = false }
                    }
                }

                Text {
                    width: parent.width
                    text: qsTr("Computations, CLI commands and Python calls share one canonical operation. Selections use MolShredder expressions; atom distance endpoints must each select one atom.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Row {
                    spacing: 9
                    ToolbarButton {
                        label: analysisOverlay.modes[analysisOverlay.modeIndex]
                        selected: true
                        action: function() {
                            analysisOverlay.modeIndex =
                                (analysisOverlay.modeIndex + 1) % analysisOverlay.modes.length
                        }
                    }
                    Rectangle {
                        width: 180; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisPrimaryInput
                            anchors.fill: parent; anchors.margins: 9
                            text: analysisOverlay.modeIndex === 2 ? qsTr("index 1") : qsTr("all")
                            color: "#eef6ff"; selectByMouse: true; clip: true
                        }
                    }
                    Rectangle {
                        width: 160; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisSecondaryInput
                            anchors.fill: parent; anchors.margins: 9
                            text: qsTr("index 2")
                            color: "#eef6ff"; selectByMouse: true; clip: true
                        }
                    }
                    Rectangle {
                        width: 130; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisResultNameInput
                            anchors.fill: parent; anchors.margins: 9
                            text: ""
                            color: "#eef6ff"; selectByMouse: true; clip: true
                            Text { anchors.fill: parent; text: qsTr("optional name");
                                   visible: parent.text.length === 0;
                                   color: "#71849c"; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                Row {
                    spacing: 9
                    Text {
                        width: 265
                        text: analysisOverlay.modeIndex === 0 || analysisOverlay.modeIndex === 1
                              ? qsTr("Selection")
                              : analysisOverlay.modeIndex === 2
                                ? qsTr("From / To selections")
                                : analysisOverlay.modeIndex === 3
                                  ? qsTr("First / optional second selection")
                                  : qsTr("Selection / reference frame")
                        color: "#91a8c2"; font.pixelSize: 12
                    }
                    Rectangle {
                        width: 85; height: 34; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisCutoffInput
                            anchors.fill: parent; anchors.margins: 8
                            text: analysisOverlay.modeIndex === 4 ? "0" : "4.0"
                            color: "#eef6ff"; validator: DoubleValidator { bottom: 0 }
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Compute and store")
                        action: function() {
                            if (analysisOverlay.modeIndex === 0)
                                viewport.analyzeCenter(analysisPrimaryInput.text, "centroid", analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 1)
                                viewport.analyzeCenter(analysisPrimaryInput.text, "com", analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 2)
                                viewport.analyzeDistance(analysisPrimaryInput.text, analysisSecondaryInput.text, "raw", analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 3)
                                viewport.analyzeContacts(analysisPrimaryInput.text, analysisSecondaryInput.text, Number(analysisCutoffInput.text), "raw", analysisResultNameInput.text)
                            else
                                viewport.analyzeTrajectoryRmsd(analysisPrimaryInput.text, Math.max(0, Number(analysisCutoffInput.text)), analysisResultNameInput.text)
                        }
                    }
                }

                Text {
                    text: qsTr("Results (click a row for provenance)")
                    color: "#dceaff"; font.pixelSize: 14; font.bold: true
                }

                Flickable {
                    width: parent.width
                    height: 210
                    contentHeight: analysisResultColumn.height
                    clip: true

                    Column {
                        id: analysisResultColumn
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: viewport.analysisItems
                            Rectangle {
                                required property var modelData
                                width: analysisResultColumn.width
                                height: 42
                                radius: 6
                                color: "#172235"
                                border.color: modelData.sourceStatus === "current" ? "#405270" : "#b27843"

                                Text {
                                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 10; width: 330
                                    text: "#" + parent.modelData.id + " · " + parent.modelData.name +
                                          " · " + parent.modelData.kind + " · " + parent.modelData.sourceStatus
                                    color: "#eef6ff"; elide: Text.ElideRight
                                }
                                Row {
                                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                    anchors.rightMargin: 7; spacing: 6
                                    ToolbarButton {
                                        label: parent.parent.modelData.visible ? qsTr("Hide") : qsTr("Show")
                                        action: function() { viewport.setAnalysisResultVisible(
                                            parent.parent.modelData.id, !parent.parent.modelData.visible) }
                                    }
                                    ToolbarButton {
                                        label: "JSON"
                                        action: function() {
                                            root.analysisExportResultId = parent.parent.modelData.id
                                            root.analysisExportFormat = "json"
                                            analysisExportDialog.open()
                                        }
                                    }
                                    ToolbarButton {
                                        label: "CSV"
                                        action: function() {
                                            root.analysisExportResultId = parent.parent.modelData.id
                                            root.analysisExportFormat = "csv"
                                            analysisExportDialog.open()
                                        }
                                    }
                                    ToolbarButton {
                                        label: qsTr("Delete")
                                        action: function() { viewport.deleteAnalysisResult(parent.parent.modelData.id) }
                                    }
                                }
                                MouseArea {
                                    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                                    width: 340
                                    onClicked: analysisResultDetail.text =
                                        viewport.analysisResultJson(parent.modelData.id)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width; height: 105; radius: 6
                    color: "#0b1320"; border.color: "#405270"
                    Text {
                        id: analysisResultDetail
                        anchors.fill: parent; anchors.margins: 9
                        text: qsTr("Select a result to inspect algorithm, units, PBC, missing-data policy and source status.")
                        color: "#a9bdd5"; font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere; elide: Text.ElideRight
                    }
                }
            }
        }
    }

    Rectangle {
        id: renderSettingsOverlay
        objectName: "renderSettingsOverlay"
        property int scopeIndex: 0
        property var scopes: ["global", "object", "state", "atom", "bond"]
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 74

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: 590
            height: 390
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                Text {
                    text: qsTr("Render Settings")
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }

                Text {
                    width: parent.width
                    text: qsTr("The editor calls the same typed setting operation as CLI and Python. State is current; atom and bond IDs are 1-based.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Row {
                    spacing: 10

                    Rectangle {
                        width: 210; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: renderSettingNameInput
                            anchors.fill: parent; anchors.margins: 9
                            text: "sphere_scale"
                            color: "#eef6ff"; selectByMouse: true
                        }
                    }
                    Rectangle {
                        width: 125; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: renderSettingValueInput
                            anchors.fill: parent; anchors.margins: 9
                            text: "1.0"
                            color: "#eef6ff"; selectByMouse: true
                        }
                    }
                    Rectangle {
                        width: 125; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: renderSettingTargetInput
                            anchors.fill: parent; anchors.margins: 9
                            text: "1"
                            color: "#eef6ff"; selectByMouse: true
                        }
                    }
                }

                Row {
                    spacing: 9

                    ToolbarButton {
                        label: qsTr("Scope: %1").arg(renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex])
                        action: function() {
                            renderSettingsOverlay.scopeIndex =
                                (renderSettingsOverlay.scopeIndex + 1) % renderSettingsOverlay.scopes.length
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Set")
                        selected: true
                        action: function() {
                            viewport.applyRenderSetting("set", renderSettingNameInput.text,
                                                        renderSettingValueInput.text,
                                                        renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                                        renderSettingTargetInput.text)
                            renderSettingResult.text = viewport.renderSettingJson(
                                renderSettingNameInput.text,
                                renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                renderSettingTargetInput.text)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Get")
                        action: function() {
                            renderSettingResult.text = viewport.renderSettingJson(
                                renderSettingNameInput.text,
                                renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                renderSettingTargetInput.text)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Unset")
                        action: function() {
                            viewport.applyRenderSetting("unset", renderSettingNameInput.text, "",
                                                        renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                                        renderSettingTargetInput.text)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Reset scope")
                        action: function() {
                            viewport.applyRenderSetting("reset", "", "",
                                                        renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                                        renderSettingTargetInput.text)
                        }
                    }
                }

                Rectangle {
                    width: parent.width; height: 92; radius: 7
                    color: "#111b2a"; border.color: "#405270"
                    Text {
                        id: renderSettingResult
                        objectName: "renderSettingResult"
                        anchors.fill: parent; anchors.margins: 10
                        color: "#bcd4ec"; font.pixelSize: 12
                        wrapMode: Text.WrapAnywhere; elide: Text.ElideRight
                    }
                }

                ToolbarButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    label: qsTr("Close")
                    action: function() { renderSettingsOverlay.visible = false }
                }
            }
        }
    }

    Rectangle {
        id: viewsOverlay
        objectName: "viewsOverlay"
        property bool clearArmed: false
        property int clipModeIndex: 0
        property var clipModes: ["atoms", "slab", "near", "far", "move", "near-set", "far-set"]
        property string clipRange: ""
        property int navigationAxisIndex: 0
        property var navigationAxes: ["x", "y", "z"]
        property int projectionModeIndex: 0
        property var projectionModes: ["perspective", "orthographic"]
        property bool preserveProjectionScale: true
        property bool stereoEnabled: false
        property int stereoModeIndex: 0
        property var stereoModes: ["side_by_side", "crosseye", "walleye", "anaglyph",
                                   "row_interleaved", "column_interleaved", "checkerboard"]
        property bool stereoSwapEyes: false
        property int anaglyphModeIndex: 4
        property var anaglyphModes: ["true", "gray", "color", "half_color", "optimized"]
        property int cameraStateModeIndex: 0
        property var cameraStateModes: ["current", "all", "explicit"]
        function cameraStateArgument() {
            return cameraStateModes[cameraStateModeIndex] === "explicit"
                    ? cameraStateInput.text
                    : cameraStateModes[cameraStateModeIndex]
        }
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 75

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(680, parent.width - 40)
            height: Math.min(720, parent.height - 40)
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Flickable {
                id: viewsScroller
                anchors.fill: parent
                anchors.margins: 24
                contentHeight: viewsColumn.implicitHeight
                clip: true

                Column {
                    id: viewsColumn
                    width: viewsScroller.width
                    spacing: 12

                Text {
                    text: qsTr("Camera & Named Views")
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }

                Text {
                    width: parent.width
                    text: qsTr("Frame a selection or change its rotation pivot. The same actions are available from CLI and Python.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Row {
                    spacing: 10

                    Rectangle {
                        width: 290
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: cameraSelectionInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: cameraSelectionInput
                            objectName: "cameraSelectionInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "all"
                            color: "#eef6ff"
                            selectionColor: "#285f99"
                            selectedTextColor: "#ffffff"
                            font.pixelSize: 14
                            clip: true
                        }
                    }

                    ToolbarButton {
                        objectName: "cameraStateModeButton"
                        label: qsTr("State %1").arg(viewsOverlay.cameraStateModes[viewsOverlay.cameraStateModeIndex])
                        action: function() {
                            viewsOverlay.cameraStateModeIndex =
                                (viewsOverlay.cameraStateModeIndex + 1) % viewsOverlay.cameraStateModes.length
                        }
                    }

                    Rectangle {
                        width: 70
                        height: 36
                        visible: viewsOverlay.cameraStateModes[viewsOverlay.cameraStateModeIndex] === "explicit"
                        radius: 6
                        color: "#111b2b"
                        border.color: cameraStateInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: cameraStateInput
                            objectName: "cameraStateInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "1"
                            color: "#eef6ff"
                            validator: IntValidator { bottom: 1 }
                            font.pixelSize: 14
                        }
                    }
                }

                Row {
                    spacing: 10

                    ToolbarButton {
                        label: qsTr("Center")
                        action: function() {
                            viewport.centerSelection(cameraSelectionInput.text,
                                                     true, viewsOverlay.cameraStateArgument(),
                                                     0.35, 1)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Fit")
                        action: function() {
                            viewport.zoomSelection(cameraSelectionInput.text,
                                                   0.0, true,
                                                   viewsOverlay.cameraStateArgument(),
                                                   0.35, 1)
                        }
                    }
                    ToolbarButton {
                        objectName: "cameraOrientButton"
                        label: qsTr("Orient")
                        action: function() {
                            viewport.orientSelection(
                                cameraSelectionInput.text,
                                viewsOverlay.cameraStateArgument(), 0.35, 1)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Set pivot")
                        action: function() {
                            viewport.setOriginSelection(
                                cameraSelectionInput.text,
                                viewsOverlay.cameraStateArgument())
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Reset all")
                        action: function() { viewport.resetViewAnimated(0.35, 1) }
                    }
                }

                Row {
                    spacing: 8

                    Rectangle {
                        width: 150
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: objectOriginInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: objectOriginInput
                            objectName: "objectOriginInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "current"
                            color: "#eef6ff"
                            font.pixelSize: 14
                            clip: true
                        }
                    }

                    Rectangle {
                        width: 58
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: objectOriginX.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: objectOriginX
                            objectName: "objectOriginX"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "0"
                            color: "#eef6ff"
                            validator: DoubleValidator {}
                            font.pixelSize: 14
                            horizontalAlignment: TextInput.AlignHCenter
                        }
                    }
                    Rectangle {
                        width: 58
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: objectOriginY.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: objectOriginY
                            objectName: "objectOriginY"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "0"
                            color: "#eef6ff"
                            validator: DoubleValidator {}
                            font.pixelSize: 14
                            horizontalAlignment: TextInput.AlignHCenter
                        }
                    }
                    Rectangle {
                        width: 58
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: objectOriginZ.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: objectOriginZ
                            objectName: "objectOriginZ"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "0"
                            color: "#eef6ff"
                            validator: DoubleValidator {}
                            font.pixelSize: 14
                            horizontalAlignment: TextInput.AlignHCenter
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Object pivot")
                        action: function() {
                            viewport.setObjectOriginSelection(
                                objectOriginInput.text, cameraSelectionInput.text,
                                viewsOverlay.cameraStateArgument())
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Reset object")
                        action: function() {
                            viewport.resetObjectTransform(objectOriginInput.text)
                        }
                    }
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        label: qsTr("Set XYZ camera")
                        action: function() {
                            viewport.setOriginPosition(
                                Number(objectOriginX.text), Number(objectOriginY.text),
                                Number(objectOriginZ.text), "")
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Object accepts current, all (reset only), name, or ID")
                        color: "#8ea5bf"
                        font.pixelSize: 12
                    }
                }

                Text {
                    text: qsTr("Stereo · adjacent-eye and anaglyph presentation")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        objectName: "stereoEnabledButton"
                        label: viewsOverlay.stereoEnabled ? qsTr("Stereo on") : qsTr("Stereo off")
                        selected: viewsOverlay.stereoEnabled
                        action: function() {
                            viewsOverlay.stereoEnabled = !viewsOverlay.stereoEnabled
                        }
                    }
                    ToolbarButton {
                        objectName: "stereoModeButton"
                        label: viewsOverlay.stereoModes[viewsOverlay.stereoModeIndex]
                        action: function() {
                            viewsOverlay.stereoModeIndex =
                                (viewsOverlay.stereoModeIndex + 1) % viewsOverlay.stereoModes.length
                        }
                    }
                    ToolbarButton {
                        objectName: "stereoSwapButton"
                        label: viewsOverlay.stereoSwapEyes ? qsTr("Eyes swapped") : qsTr("Eye order")
                        selected: viewsOverlay.stereoSwapEyes
                        action: function() {
                            viewsOverlay.stereoSwapEyes = !viewsOverlay.stereoSwapEyes
                        }
                    }
                }

                Row {
                    spacing: 8

                    Rectangle {
                        width: 90
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: stereoShiftInput.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: stereoShiftInput
                            objectName: "stereoShiftInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "2.0"
                            color: "#eef6ff"
                            validator: DoubleValidator { bottom: 0; top: 100 }
                            font.pixelSize: 14
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "% shift"
                        color: "#8ea5bf"
                        font.pixelSize: 12
                    }
                    Rectangle {
                        width: 90
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: stereoAngleInput.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: stereoAngleInput
                            objectName: "stereoAngleInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "2.1"
                            color: "#eef6ff"
                            validator: DoubleValidator { bottom: 0; top: 20 }
                            font.pixelSize: 14
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("angle scale")
                        color: "#8ea5bf"
                        font.pixelSize: 12
                    }
                    ToolbarButton {
                        objectName: "anaglyphModeButton"
                        label: qsTr("Anaglyph %1").arg(viewsOverlay.anaglyphModes[viewsOverlay.anaglyphModeIndex])
                        action: function() {
                            viewsOverlay.anaglyphModeIndex =
                                (viewsOverlay.anaglyphModeIndex + 1) % viewsOverlay.anaglyphModes.length
                        }
                    }
                    ToolbarButton {
                        objectName: "stereoApplyButton"
                        label: qsTr("Apply stereo")
                        action: function() {
                            const shift = Number(stereoShiftInput.text)
                            const angle = Number(stereoAngleInput.text)
                            if (Number.isFinite(shift) && Number.isFinite(angle))
                                viewport.setStereo(
                                    viewsOverlay.stereoEnabled,
                                    viewsOverlay.stereoModes[viewsOverlay.stereoModeIndex],
                                    viewsOverlay.stereoSwapEyes, shift, angle,
                                    viewsOverlay.anaglyphModes[viewsOverlay.anaglyphModeIndex])
                        }
                    }
                }

                Text {
                    text: qsTr("Projection · preserve target-plane scale by default")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        objectName: "projectionModeButton"
                        label: viewsOverlay.projectionModes[viewsOverlay.projectionModeIndex]
                        action: function() {
                            viewsOverlay.projectionModeIndex =
                                (viewsOverlay.projectionModeIndex + 1) % viewsOverlay.projectionModes.length
                        }
                    }

                    Rectangle {
                        width: 90
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: projectionFovInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: projectionFovInput
                            objectName: "projectionFovInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "45"
                            color: "#eef6ff"
                            validator: DoubleValidator { bottom: 0.000001; top: 179.999999 }
                            font.pixelSize: 14
                        }
                    }

                    ToolbarButton {
                        objectName: "projectionPreserveButton"
                        label: viewsOverlay.preserveProjectionScale ? qsTr("Scale locked") : qsTr("Raw switch")
                        selected: viewsOverlay.preserveProjectionScale
                        action: function() {
                            viewsOverlay.preserveProjectionScale =
                                !viewsOverlay.preserveProjectionScale
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Apply projection")
                        action: function() {
                            const fov = Number(projectionFovInput.text)
                            if (Number.isFinite(fov))
                                viewport.setProjection(
                                    viewsOverlay.projectionModes[viewsOverlay.projectionModeIndex],
                                    fov, viewsOverlay.preserveProjectionScale)
                        }
                    }
                }

                Text {
                    text: qsTr("Axis navigation · camera-local move / pivoted turn")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        objectName: "navigationAxisButton"
                        label: qsTr("Axis %1").arg(viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex].toUpperCase())
                        action: function() {
                            viewsOverlay.navigationAxisIndex =
                                (viewsOverlay.navigationAxisIndex + 1) % viewsOverlay.navigationAxes.length
                        }
                    }

                    Rectangle {
                        width: 70
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: navigationStepInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: navigationStepInput
                            objectName: "navigationStepInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "5"
                            color: "#eef6ff"
                            validator: DoubleValidator { bottom: 0.000001 }
                            font.pixelSize: 14
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Move −")
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.moveCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], -step)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Move +")
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.moveCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], step)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Turn −")
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.turnCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], -step)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Turn +")
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.turnCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], step)
                        }
                    }
                }

                Text {
                    text: qsTr("Clipping · click mode to change · %1").arg(viewsOverlay.clipRange)
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 10

                    ToolbarButton {
                        objectName: "clipModeButton"
                        label: viewsOverlay.clipModes[viewsOverlay.clipModeIndex]
                        action: function() {
                            viewsOverlay.clipModeIndex =
                                (viewsOverlay.clipModeIndex + 1) % viewsOverlay.clipModes.length
                        }
                    }

                    Rectangle {
                        width: 90
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: clipDistanceInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: clipDistanceInput
                            objectName: "clipDistanceInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "5"
                            color: "#eef6ff"
                            validator: DoubleValidator {}
                            font.pixelSize: 14
                        }
                    }

                    Rectangle {
                        width: 205
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: clipSelectionInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: clipSelectionInput
                            objectName: "clipSelectionInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: cameraSelectionInput.text
                            color: "#eef6ff"
                            font.pixelSize: 14
                            clip: true
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Apply")
                        action: function() {
                            const distance = Number(clipDistanceInput.text)
                            if (Number.isFinite(distance) && viewport.clipCamera(
                                    viewsOverlay.clipModes[viewsOverlay.clipModeIndex],
                                    distance, clipSelectionInput.text,
                                    viewsOverlay.cameraStateArgument()))
                                viewsOverlay.clipRange = viewport.clipRangeText()
                        }
                    }
                }

                Text {
                    text: qsTr("Named views")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 10

                    Rectangle {
                        width: 350
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: viewNameInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: viewNameInput
                            objectName: "viewNameInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            color: "#eef6ff"
                            selectionColor: "#285f99"
                            selectedTextColor: "#ffffff"
                            font.pixelSize: 14
                            clip: true
                            onAccepted: {
                                if (text.trim().length > 0 && viewport.storeNamedView(text))
                                    text = ""
                            }

                            Text {
                                anchors.fill: parent
                                text: qsTr("View name")
                                color: "#667b94"
                                font.pixelSize: 14
                                visible: viewNameInput.text.length === 0 && !viewNameInput.activeFocus
                            }
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Store")
                        action: function() {
                            if (viewNameInput.text.trim().length > 0 &&
                                    viewport.storeNamedView(viewNameInput.text))
                                viewNameInput.text = ""
                        }
                    }
                }

                Text {
                    visible: viewport.viewItems.length === 0
                    text: qsTr("No named views stored yet.")
                    color: "#8291a6"
                    font.pixelSize: 13
                }

                ListView {
                    width: parent.width
                    height: 55
                    spacing: 7
                    clip: true
                    model: viewport.viewItems

                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 42
                        radius: 6
                        color: "#172235"
                        border.color: "#354a66"

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            width: 275
                            text: parent.modelData.name + " · " +
                                  parent.modelData.projection
                            color: "#e5f1ff"
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        ToolbarButton {
                            anchors.right: deleteViewButton.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            label: qsTr("Recall")
                            action: function() {
                                viewport.recallNamedViewAnimated(
                                    parent.modelData.name, 0.35, 1)
                            }
                        }

                        ToolbarButton {
                            id: deleteViewButton
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            label: qsTr("Delete")
                            action: function() {
                                viewport.deleteNamedView(parent.modelData.name)
                            }
                        }
                    }
                }

                Text {
                    text: qsTr("PyMOL 18-value camera")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Rectangle {
                    width: parent.width
                    height: 50
                    radius: 6
                    color: "#111b2b"
                    border.color: pymolViewInput.activeFocus ? "#69aef0" : "#405270"

                    TextEdit {
                        id: pymolViewInput
                        objectName: "pymolViewInput"
                        anchors.fill: parent
                        anchors.margins: 8
                        color: "#dceaff"
                        selectionColor: "#285f99"
                        selectedTextColor: "#ffffff"
                        font.pixelSize: 11
                        wrapMode: TextEdit.WrapAnywhere
                        clip: true

                        Text {
                            anchors.fill: parent
                            text: qsTr("Paste cmd.get_view() values here, or export the current camera.")
                            color: "#667b94"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            visible: pymolViewInput.text.length === 0 && !pymolViewInput.activeFocus
                        }
                    }
                }

                Row {
                    spacing: 10

                    ToolbarButton {
                        label: qsTr("Export current")
                        action: function() {
                            pymolViewInput.text = viewport.pymolViewText()
                            pymolViewInput.forceActiveFocus()
                            pymolViewInput.selectAll()
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Import values")
                        action: function() {
                            viewport.importPymolViewAnimated(
                                pymolViewInput.text, 0.35, 1)
                        }
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: viewsOverlay.clearArmed
                    text: qsTr("This removes every stored camera view. Click confirm to continue.")
                    color: "#e7b56f"
                    font.pixelSize: 12
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    ToolbarButton {
                        label: viewsOverlay.clearArmed ? qsTr("Confirm clear all") : qsTr("Clear all")
                        selected: viewsOverlay.clearArmed
                        action: function() {
                            if (!viewsOverlay.clearArmed) {
                                viewsOverlay.clearArmed = true
                                return
                            }
                            viewport.clearNamedViews()
                            viewsOverlay.clearArmed = false
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Close")
                        action: function() {
                            viewsOverlay.clearArmed = false
                            viewsOverlay.visible = false
                        }
                    }
                }
                }
            }
        }
    }

    Rectangle {
        id: systemInfoOverlay
        objectName: "systemInfoOverlay"
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 80

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(620, parent.width - 40)
            height: Math.min(510, parent.height - 40)
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 7

                Text {
                    text: qsTr("MolShredder System Information")
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }

                Text {
                    objectName: "systemInfoSummary"
                    width: parent.width
                    text: root.systemInfoPanelError.length > 0
                          ? root.systemInfoPanelError
                          : qsTr("Build and graphics runtime reported by the canonical system info operation.")
                    color: root.systemInfoPanelError.length > 0 ? "#ff9f9f" : "#a9bdd5"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Rectangle { width: parent.width; height: 1; color: "#405270" }

                InfoRow {
                    width: parent.width
                    label: qsTr("Version / configuration")
                    value: root.systemInfoData.project_version === undefined
                           ? qsTr("Not reported")
                           : String(root.systemInfoData.project_version) + " / " +
                             String(root.systemInfoData.build_configuration)
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Platform")
                    value: root.systemInfoValue("platform", "operating_system") + " / " +
                           root.systemInfoValue("platform", "architecture")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Compiler")
                    value: root.systemInfoValue("toolchain", "compiler_id") + " " +
                           root.systemInfoValue("toolchain", "compiler_version") +
                           " · C++" + root.systemInfoValue("toolchain", "cxx_standard")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Python / HDF5 / netCDF")
                    value: root.systemInfoValue("dependencies", "python") + " / " +
                           root.systemInfoValue("dependencies", "hdf5") + " / " +
                           root.systemInfoValue("dependencies", "netcdf")
                }

                Rectangle { width: parent.width; height: 1; color: "#405270" }

                InfoRow {
                    width: parent.width
                    label: qsTr("Graphics status")
                    value: root.graphicsInfoValue("status")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("API / backend")
                    value: root.graphicsInfoValue("api") + " / " +
                           root.graphicsInfoValue("backend")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Device")
                    value: root.graphicsInfoValue("device_name") + " / " +
                           root.graphicsInfoValue("device_type")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Driver version")
                    value: root.graphicsInfoValue("driver_version")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Failure reason")
                    value: root.graphicsInfoValue("failure_reason")
                }

                Text {
                    width: parent.width
                    text: qsTr("Full machine-readable report: molshredder system info --format json")
                    color: "#7fb9ec"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                ToolbarButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    label: qsTr("Close")
                    action: function() { systemInfoOverlay.visible = false }
                }
            }
        }
    }

    Rectangle {
        id: scriptTrustOverlay
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 90

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(540, parent.width - 40)
            height: 250
            radius: 12
            color: "#f0101827"
            border.color: "#e4a24a"

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 13

                Text {
                    width: parent.width
                    text: qsTr("Run trusted Python code?")
                    color: "#eef6ff"
                    font.pixelSize: 19
                    font.bold: true
                }

                Text {
                    width: parent.width
                    text: qsTr("Python scripts run with the same permissions as MolShredder. Review the selected file before continuing.")
                    color: "#d8e8fa"
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: qsTr("Scripts can read files, use the network, start processes, and may leave partial viewer changes if they fail.")
                    color: "#e7b56f"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    ToolbarButton {
                        label: qsTr("Run script")
                        selected: true
                        action: function() {
                            scriptTrustOverlay.visible = false
                            viewport.runPythonScript(root.pendingScriptUrl)
                            root.pendingScriptUrl = ""
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Cancel")
                        action: function() {
                            scriptTrustOverlay.visible = false
                            root.pendingScriptUrl = ""
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: viewport.scriptRunning
        color: "#99050812"
        z: 100

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.centerIn: parent
            width: 410
            height: 154
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.centerIn: parent
                spacing: 16

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Python script is running")
                    color: "#eef6ff"
                    font.pixelSize: 18
                    font.bold: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Viewer editing is paused to protect the workspace.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                }

                ToolbarButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    label: qsTr("Request cancellation")
                    action: function() { viewport.cancelPythonScript() }
                }
            }
        }
    }
}
