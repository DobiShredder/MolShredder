import QtQuick
import QtQuick.Dialogs
import MolShredder.Desktop

Window {
    id: root
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
            onDoubleClicked: viewport.resetView()
        }
    }

    FileDialog {
        id: structureDialog
        title: "Open molecular structure"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Molecular structures (*.pdb *.ent *.cif *.mmcif)",
                      "All files (*)"]
        onAccepted: viewport.loadStructure(selectedFile)
    }

    FileDialog {
        id: trajectoryDialog
        title: "Attach molecular dynamics trajectory"
        fileMode: FileDialog.OpenFile
        nameFilters: ["MD trajectories (*.dcd *.xtc *.trr)",
                      "All files (*)"]
        onAccepted: viewport.loadTrajectory(selectedFile)
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
        anchors.bottomMargin: viewport.hasTrajectory ? 132 : 20
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
        anchors.bottomMargin: viewport.hasTrajectory ? 132 : 20
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
}
