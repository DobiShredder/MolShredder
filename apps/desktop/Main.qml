import QtQuick
import QtQuick.Dialogs
import MolShredder.Desktop

Window {
    id: root
    property string trajectoryCoordinateUnit: "angstrom"
    property url pendingScriptUrl
    property var systemInfoData: ({})
    property string systemInfoPanelSourceJson: ""
    property string systemInfoPanelError: ""

    function systemInfoValue(group, key) {
        if (!root.systemInfoData || !root.systemInfoData[group])
            return "Not reported"
        const value = root.systemInfoData[group][key]
        return value === null || value === undefined || value === ""
               ? "Not reported" : String(value)
    }

    function graphicsInfoValue(key) {
        if (!root.systemInfoData || !root.systemInfoData.runtime ||
                !root.systemInfoData.runtime.graphics)
            return "Not reported"
        const value = root.systemInfoData.runtime.graphics[key]
        return value === null || value === undefined || value === ""
               ? "Not reported" : String(value)
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
    width: 1080
    height: 720
    visible: true
    color: "#050812"
    title: "MolShredder GPU Viewport Prototype"

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

    FileDialog {
        id: structureDialog
        title: "Open molecular structure or scalar volume"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Molecular data (*.pdb *.ent *.cif *.mmcif *.bcif *.pqr *.mol *.mol2 *.psf *.prmtop *.parm7 *.top *.sdf *.sd *.gro *.g96 *.vtf *.xyz *.dx *.mrc *.map *.ccp4 *.mrcs)",
                      "OpenDX scalar volumes (*.dx)",
                      "MRC/CCP4 scalar volumes (*.mrc *.map *.ccp4 *.mrcs)",
                      "All files (*)"]
        onAccepted: viewport.loadStructure(selectedFile)
    }

    FileDialog {
        id: saveDialog
        title: "Save active molecular coordinates"
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
        nameFilters: ["XYZ coordinates (*.xyz)",
                      "Protein Data Bank 3.3 (*.pdb *.ent)",
                      "PDBx/mmCIF (*.cif *.mmcif)",
                      "BinaryCIF (*.bcif)",
                      "GROMACS structure/trajectory (*.gro)",
                      "GROMOS-96 structure/trajectory (*.g96)",
                      "PQR electrostatics (*.pqr)",
                      "MDL MOL V2000 (*.mol)",
                      "Tripos MOL2 (*.mol2)",
                      "CHARMM/NAMD PSF topology (*.psf)",
                      "SDF V2000 record (*.sdf *.sd)"]
        onAccepted: viewport.saveStructure(selectedFile, false)
    }

    FileDialog {
        id: trajectoryDialog
        title: "Attach molecular dynamics trajectory"
        fileMode: FileDialog.OpenFile
        nameFilters: ["MD trajectories/restarts (*.dcd *.xtc *.trr *.mdcrd *.crd *.nc *.ncdf *.netcdf *.h5md *.rst7 *.restrt *.inpcrd *.inprst *.lammpstrj *.lammpstraj *.dump *.binpos)",
                      "All files (*)"]
        onAccepted: viewport.loadTrajectory(selectedFile,
                                              root.trajectoryCoordinateUnit)
    }

    FileDialog {
        id: scriptDialog
        title: "Run a local Python script"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Python scripts (*.py)"]
        onAccepted: {
            root.pendingScriptUrl = selectedFile
            scriptTrustOverlay.visible = true
        }
    }

    component ToolbarButton: Rectangle {
        required property string label
        required property var action
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
        anchors.margins: 20
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
                label: "Open"
                action: function() { structureDialog.open() }
            }
            ToolbarButton {
                label: "Trajectory"
                action: function() { trajectoryDialog.open() }
            }
            ToolbarButton {
                label: root.trajectoryCoordinateUnit === "angstrom"
                       ? "Traj Å" : "Traj nm"
                action: function() {
                    root.trajectoryCoordinateUnit =
                        root.trajectoryCoordinateUnit === "angstrom"
                        ? "nanometer" : "angstrom"
                }
            }
            ToolbarButton {
                label: "Save"
                action: function() { saveDialog.open() }
            }
            ToolbarButton {
                label: "Run Script"
                action: function() { scriptDialog.open() }
            }
            ToolbarButton {
                label: "Views"
                action: function() { root.openViews() }
            }
            ToolbarButton {
                label: "System"
                action: function() { root.openSystemInfo() }
            }
            ToolbarButton {
                label: "Lines"
                selected: viewport.representation === "lines"
                action: function() { viewport.setRepresentation("lines") }
            }
            ToolbarButton {
                label: "Sticks"
                selected: viewport.representation === "sticks"
                action: function() { viewport.setRepresentation("sticks") }
            }
            ToolbarButton {
                label: "Spheres"
                selected: viewport.representation === "spheres"
                action: function() { viewport.setRepresentation("spheres") }
            }
            ToolbarButton {
                label: "Ribbon"
                selected: viewport.representation === "ribbon"
                action: function() { viewport.setRepresentation("ribbon") }
            }
            ToolbarButton {
                label: "Cartoon"
                selected: viewport.representation === "cartoon"
                action: function() { viewport.setRepresentation("cartoon") }
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
        height: 104
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
                label: viewport.trajectoryPlaying ? "Pause" : "Play"
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
                label: viewport.playbackDirection === "forward" ? "Forward" : "Reverse"
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
        }

        Text {
            id: frameLabel
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 13
            anchors.topMargin: 14
            text: "Frame " + viewport.trajectoryFrame + " / " +
                  Math.max(0, viewport.trajectoryFrameCount - 1)
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
            text: "Contour " + Number(viewport.volumeLevel).toPrecision(5)
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
                label: "Midpoint"
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
        width: 270
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
            text: "Objects"
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
                            anchors.left: visibilityToggle.right
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 9
                            anchors.rightMargin: 8
                            text: parent.modelData.name + " · " + parent.modelData.atoms + " atoms · " +
                                  parent.modelData.representation
                            color: parent.modelData.visible ? "#e5f1ff" : "#8291a6"
                            elide: Text.ElideRight
                            font.pixelSize: 13
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
        property var stereoModes: ["side_by_side", "crosseye", "walleye", "anaglyph"]
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
                    text: "Camera & Named Views"
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }

                Text {
                    width: parent.width
                    text: "Frame a selection or change its rotation pivot. The same actions are available from CLI and Python."
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
                        label: "State " + viewsOverlay.cameraStateModes[viewsOverlay.cameraStateModeIndex]
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
                        label: "Center"
                        action: function() {
                            viewport.centerSelection(cameraSelectionInput.text,
                                                     true, viewsOverlay.cameraStateArgument(),
                                                     0.35, 1)
                        }
                    }
                    ToolbarButton {
                        label: "Fit"
                        action: function() {
                            viewport.zoomSelection(cameraSelectionInput.text,
                                                   0.0, true,
                                                   viewsOverlay.cameraStateArgument(),
                                                   0.35, 1)
                        }
                    }
                    ToolbarButton {
                        objectName: "cameraOrientButton"
                        label: "Orient"
                        action: function() {
                            viewport.orientSelection(
                                cameraSelectionInput.text,
                                viewsOverlay.cameraStateArgument(), 0.35, 1)
                        }
                    }
                    ToolbarButton {
                        label: "Set pivot"
                        action: function() {
                            viewport.setOriginSelection(
                                cameraSelectionInput.text,
                                viewsOverlay.cameraStateArgument())
                        }
                    }
                    ToolbarButton {
                        label: "Reset all"
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
                        label: "Object pivot"
                        action: function() {
                            viewport.setObjectOriginSelection(
                                objectOriginInput.text, cameraSelectionInput.text,
                                viewsOverlay.cameraStateArgument())
                        }
                    }
                    ToolbarButton {
                        label: "Reset object"
                        action: function() {
                            viewport.resetObjectTransform(objectOriginInput.text)
                        }
                    }
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        label: "Set XYZ camera"
                        action: function() {
                            viewport.setOriginPosition(
                                Number(objectOriginX.text), Number(objectOriginY.text),
                                Number(objectOriginZ.text), "")
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Object accepts current, all (reset only), name, or ID"
                        color: "#8ea5bf"
                        font.pixelSize: 12
                    }
                }

                Text {
                    text: "Stereo · adjacent-eye and anaglyph presentation"
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        objectName: "stereoEnabledButton"
                        label: viewsOverlay.stereoEnabled ? "Stereo on" : "Stereo off"
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
                        label: viewsOverlay.stereoSwapEyes ? "Eyes swapped" : "Eye order"
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
                        text: "angle scale"
                        color: "#8ea5bf"
                        font.pixelSize: 12
                    }
                    ToolbarButton {
                        objectName: "anaglyphModeButton"
                        label: "Anaglyph " + viewsOverlay.anaglyphModes[viewsOverlay.anaglyphModeIndex]
                        action: function() {
                            viewsOverlay.anaglyphModeIndex =
                                (viewsOverlay.anaglyphModeIndex + 1) % viewsOverlay.anaglyphModes.length
                        }
                    }
                    ToolbarButton {
                        objectName: "stereoApplyButton"
                        label: "Apply stereo"
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
                    text: "Projection · preserve target-plane scale by default"
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
                        label: viewsOverlay.preserveProjectionScale ? "Scale locked" : "Raw switch"
                        selected: viewsOverlay.preserveProjectionScale
                        action: function() {
                            viewsOverlay.preserveProjectionScale =
                                !viewsOverlay.preserveProjectionScale
                        }
                    }

                    ToolbarButton {
                        label: "Apply projection"
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
                    text: "Axis navigation · camera-local move / pivoted turn"
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        objectName: "navigationAxisButton"
                        label: "Axis " + viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex].toUpperCase()
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
                        label: "Move −"
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.moveCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], -step)
                        }
                    }
                    ToolbarButton {
                        label: "Move +"
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.moveCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], step)
                        }
                    }
                    ToolbarButton {
                        label: "Turn −"
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.turnCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], -step)
                        }
                    }
                    ToolbarButton {
                        label: "Turn +"
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.turnCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], step)
                        }
                    }
                }

                Text {
                    text: "Clipping · click mode to change · " + viewsOverlay.clipRange
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
                        label: "Apply"
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
                    text: "Named views"
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
                                text: "View name"
                                color: "#667b94"
                                font.pixelSize: 14
                                visible: viewNameInput.text.length === 0 && !viewNameInput.activeFocus
                            }
                        }
                    }

                    ToolbarButton {
                        label: "Store"
                        action: function() {
                            if (viewNameInput.text.trim().length > 0 &&
                                    viewport.storeNamedView(viewNameInput.text))
                                viewNameInput.text = ""
                        }
                    }
                }

                Text {
                    visible: viewport.viewItems.length === 0
                    text: "No named views stored yet."
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
                            label: "Recall"
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
                            label: "Delete"
                            action: function() {
                                viewport.deleteNamedView(parent.modelData.name)
                            }
                        }
                    }
                }

                Text {
                    text: "PyMOL 18-value camera"
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
                            text: "Paste cmd.get_view() values here, or export the current camera."
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
                        label: "Export current"
                        action: function() {
                            pymolViewInput.text = viewport.pymolViewText()
                            pymolViewInput.forceActiveFocus()
                            pymolViewInput.selectAll()
                        }
                    }
                    ToolbarButton {
                        label: "Import values"
                        action: function() {
                            viewport.importPymolViewAnimated(
                                pymolViewInput.text, 0.35, 1)
                        }
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: viewsOverlay.clearArmed
                    text: "This removes every stored camera view. Click confirm to continue."
                    color: "#e7b56f"
                    font.pixelSize: 12
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    ToolbarButton {
                        label: viewsOverlay.clearArmed ? "Confirm clear all" : "Clear all"
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
                        label: "Close"
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
                    text: "MolShredder System Information"
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }

                Text {
                    objectName: "systemInfoSummary"
                    width: parent.width
                    text: root.systemInfoPanelError.length > 0
                          ? root.systemInfoPanelError
                          : "Build and graphics runtime reported by the canonical system info operation."
                    color: root.systemInfoPanelError.length > 0 ? "#ff9f9f" : "#a9bdd5"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Rectangle { width: parent.width; height: 1; color: "#405270" }

                InfoRow {
                    width: parent.width
                    label: "Version / configuration"
                    value: root.systemInfoData.project_version === undefined
                           ? "Not reported"
                           : String(root.systemInfoData.project_version) + " / " +
                             String(root.systemInfoData.build_configuration)
                }
                InfoRow {
                    width: parent.width
                    label: "Platform"
                    value: root.systemInfoValue("platform", "operating_system") + " / " +
                           root.systemInfoValue("platform", "architecture")
                }
                InfoRow {
                    width: parent.width
                    label: "Compiler"
                    value: root.systemInfoValue("toolchain", "compiler_id") + " " +
                           root.systemInfoValue("toolchain", "compiler_version") +
                           " · C++" + root.systemInfoValue("toolchain", "cxx_standard")
                }
                InfoRow {
                    width: parent.width
                    label: "Python / HDF5 / netCDF"
                    value: root.systemInfoValue("dependencies", "python") + " / " +
                           root.systemInfoValue("dependencies", "hdf5") + " / " +
                           root.systemInfoValue("dependencies", "netcdf")
                }

                Rectangle { width: parent.width; height: 1; color: "#405270" }

                InfoRow {
                    width: parent.width
                    label: "Graphics status"
                    value: root.graphicsInfoValue("status")
                }
                InfoRow {
                    width: parent.width
                    label: "API / backend"
                    value: root.graphicsInfoValue("api") + " / " +
                           root.graphicsInfoValue("backend")
                }
                InfoRow {
                    width: parent.width
                    label: "Device"
                    value: root.graphicsInfoValue("device_name") + " / " +
                           root.graphicsInfoValue("device_type")
                }
                InfoRow {
                    width: parent.width
                    label: "Driver version"
                    value: root.graphicsInfoValue("driver_version")
                }
                InfoRow {
                    width: parent.width
                    label: "Failure reason"
                    value: root.graphicsInfoValue("failure_reason")
                }

                Text {
                    width: parent.width
                    text: "Full machine-readable report: molshredder system info --format json"
                    color: "#7fb9ec"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                ToolbarButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    label: "Close"
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
                    text: "Run trusted Python code?"
                    color: "#eef6ff"
                    font.pixelSize: 19
                    font.bold: true
                }

                Text {
                    width: parent.width
                    text: "Python scripts run with the same permissions as MolShredder. Review the selected file before continuing."
                    color: "#d8e8fa"
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: "Scripts can read files, use the network, start processes, and may leave partial viewer changes if they fail."
                    color: "#e7b56f"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    ToolbarButton {
                        label: "Run script"
                        selected: true
                        action: function() {
                            scriptTrustOverlay.visible = false
                            viewport.runPythonScript(root.pendingScriptUrl)
                            root.pendingScriptUrl = ""
                        }
                    }

                    ToolbarButton {
                        label: "Cancel"
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
                    text: "Python script is running"
                    color: "#eef6ff"
                    font.pixelSize: 18
                    font.bold: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Viewer editing is paused to protect the workspace."
                    color: "#a9bdd5"
                    font.pixelSize: 13
                }

                ToolbarButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    label: "Request cancellation"
                    action: function() { viewport.cancelPythonScript() }
                }
            }
        }
    }
}
