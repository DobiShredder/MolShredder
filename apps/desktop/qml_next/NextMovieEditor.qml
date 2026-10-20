import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Trajectory and movie editor (SEQ-330 U5): playback range and speed of the active trajectory, the typed movie
// timeline with its keyframes, state mapping and export. Every button runs an ordinary command through `run`.
Popup {
    id: editor
    property var viewport
    signal run(string text)
    signal formRequested(string name, var preset)

    readonly property bool trajectory: viewport && viewport.hasTrajectory
    readonly property var movie: viewport ? viewport.movieState : ({})
    readonly property bool hasMovie: movie && movie.configured === true
    readonly property var keyframes: hasMovie && movie.keyframes ? movie.keyframes : []
    readonly property var scenes: viewport ? viewport.sceneItems : []
    readonly property var playModes: ["once", "loop", "rock"]
    readonly property var directions: ["forward", "reverse"]

    modal: false
    focus: false
    padding: 0
    closePolicy: Popup.CloseOnEscape
    x: 12
    y: Overlay.overlay ? Overlay.overlay.height - height - 110 : 0
    width: Math.min(560, Overlay.overlay ? Overlay.overlay.width - 24 : 560)
    height: Math.min(520, Overlay.overlay ? Overlay.overlay.height - 170 : 520)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    function integer(text, fallback) {
        const value = parseInt(String(text).trim(), 10)
        return isNaN(value) ? fallback : value
    }
    function number(text, fallback) {
        const value = parseFloat(String(text).trim())
        return isNaN(value) ? fallback : value
    }
    function openEditor() {
        if (hasMovie) {
            framesField.text = String(movie.frameCount)
            movieFps.text = String(movie.fps)
            loopBox.checked = movie.loop === true
            keyFrame.text = String(movie.currentFrame)
        } else if (trajectory) {
            framesField.text = String(viewport.trajectoryFrameCount)
        }
        if (trajectory) {
            trajFps.text = String(viewport.trajectoryFps)
            lastField.text = String(Math.max(0, viewport.trajectoryFrameCount - 1))
            modeBox.currentIndex = Math.max(0, playModes.indexOf(viewport.playbackMode))
            directionBox.currentIndex = Math.max(0, directions.indexOf(viewport.playbackDirection))
        }
        open()
    }

    component Field: TextField {
        implicitHeight: 26
        implicitWidth: 70
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        placeholderTextColor: NextTheme.muted
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
    }
    component Choice: ComboBox {
        implicitHeight: 26
        implicitWidth: 92
        font.pixelSize: NextTheme.fontSize
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
        contentItem: Text {
            leftPadding: 8
            text: parent.displayText
            color: NextTheme.text
            font: parent.font
            verticalAlignment: Text.AlignVCenter
        }
    }
    component Check: CheckBox {
        id: check
        font.pixelSize: NextTheme.fontSize
        implicitHeight: 26
        indicator: Rectangle {
            x: 0
            y: (check.height - height) / 2
            width: 16; height: 16; radius: 3
            color: check.checked ? NextTheme.activeSoft : NextTheme.chrome
            border.color: check.activeFocus ? NextTheme.active : NextTheme.line
            Text { anchors.centerIn: parent; visible: check.checked; text: "✓"; color: NextTheme.active; font.pixelSize: 12 }
        }
        contentItem: Text {
            leftPadding: 22
            text: check.text
            color: NextTheme.text
            font: check.font
            verticalAlignment: Text.AlignVCenter
        }
    }
    component Caption: Text {
        color: NextTheme.muted
        font.pixelSize: NextTheme.smallFont
    }
    component Heading: Text {
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        font.weight: Font.DemiBold
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("MOVIE EDITOR"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: editor.close() }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: body.implicitHeight + 16
            ScrollBar.vertical: ScrollBar { }
            Column {
                id: body
                x: 12
                width: parent.width - 24
                spacing: 10

                // ---- Trajectory ------------------------------------------------------------------------------
                Heading { text: qsTr("Trajectory") }
                Caption {
                    visible: !editor.trajectory
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("The active object has no trajectory. File ▸ Open trajectory… attaches one.")
                }
                Column {
                    visible: editor.trajectory
                    width: parent.width
                    spacing: 8
                    Caption {
                        text: qsTr("%1 frames · %2 · %3 fps").arg(editor.trajectory ? viewport.trajectoryFrameCount : 0)
                                  .arg(editor.trajectory ? viewport.playbackMode : "").arg(editor.trajectory ? Number(viewport.trajectoryFps).toFixed(0) : 0)
                    }
                    Flow {
                        width: parent.width
                        spacing: 8
                        Caption { text: qsTr("first"); height: 26; verticalAlignment: Text.AlignVCenter }
                        Field { id: firstField; text: "0"; implicitWidth: 56; objectName: "trajFirst" }
                        Caption { text: qsTr("last"); height: 26; verticalAlignment: Text.AlignVCenter }
                        Field { id: lastField; text: "0"; implicitWidth: 56; objectName: "trajLast" }
                        Caption { text: qsTr("stride"); height: 26; verticalAlignment: Text.AlignVCenter }
                        Field { id: strideField; text: "1"; implicitWidth: 44; objectName: "trajStride" }
                        Choice { id: modeBox; model: editor.playModes }
                        Choice { id: directionBox; model: editor.directions }
                        NextButton {
                            text: qsTr("Apply range")
                            onClicked: editor.run("traj range --first " + editor.integer(firstField.text, 0)
                                                  + " --last " + editor.integer(lastField.text, 0)
                                                  + " --stride " + Math.max(1, editor.integer(strideField.text, 1))
                                                  + " --mode " + modeBox.currentText + " --direction " + directionBox.currentText)
                        }
                    }
                    Row {
                        spacing: 8
                        Caption { text: qsTr("speed (fps)"); height: 26; verticalAlignment: Text.AlignVCenter }
                        Field { id: trajFps; text: "30"; implicitWidth: 56 }
                        NextButton { text: qsTr("Set speed"); onClicked: editor.run("traj speed --fps " + editor.number(trajFps.text, 30)) }
                    }
                    Flow {
                        width: parent.width
                        spacing: 8
                        NextButton { text: qsTr("Delete frames…"); onClicked: editor.formRequested("traj delete-frames", ({})) }
                        NextButton { text: qsTr("Duplicate frame…"); onClicked: editor.formRequested("traj dup-frame", ({})) }
                        NextButton { text: qsTr("Save trajectory…"); onClicked: editor.formRequested("traj save", ({})) }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: NextTheme.line }

                // ---- Movie -----------------------------------------------------------------------------------
                Heading { text: qsTr("Movie") }
                Flow {
                    width: parent.width
                    spacing: 8
                    Caption { text: qsTr("frames"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: framesField; text: "100"; implicitWidth: 64; objectName: "movieFrames" }
                    Caption { text: qsTr("fps"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: movieFps; text: "30"; implicitWidth: 52; objectName: "movieFps" }
                    Check { id: loopBox; text: qsTr("loop") }
                    NextButton {
                        objectName: "configureMovieButton"
                        text: editor.hasMovie ? qsTr("Replace movie") : qsTr("Create movie")
                        onClicked: editor.run("movie configure --frames " + Math.max(1, editor.integer(framesField.text, 1))
                                              + " --fps " + editor.number(movieFps.text, 30) + " --loop " + (loopBox.checked ? "true" : "false"))
                    }
                    NextButton { objectName: "playMovieButton"; text: editor.hasMovie && editor.movie.playing === true ? qsTr("Pause") : qsTr("Play"); enabled: editor.hasMovie; onClicked: editor.run(editor.movie.playing === true ? "movie pause" : "movie play") }
                    NextButton { text: qsTr("Clear"); enabled: editor.hasMovie; onClicked: editor.run("movie clear") }
                }

                // timeline with keyframe markers; a click seeks
                Item {
                    id: timeline
                    visible: editor.hasMovie
                    width: parent.width
                    height: 44
                    readonly property real count: editor.hasMovie ? editor.movie.frameCount : 0
                    function xOf(frame) { return count > 1 ? (width - 8) * (frame - 1) / (count - 1) + 4 : 4 }
                    Rectangle { x: 0; y: 20; width: parent.width; height: 6; radius: 3; color: NextTheme.line }
                    Rectangle { x: 0; y: 20; width: editor.hasMovie ? timeline.xOf(editor.movie.currentFrame) : 0; height: 6; radius: 3; color: NextTheme.active }
                    Repeater {
                        model: editor.keyframes
                        delegate: Rectangle {
                            required property var modelData
                            x: timeline.xOf(modelData.frame) - 5
                            y: 17
                            width: 10; height: 10
                            rotation: 45
                            color: modelData.scene.length > 0 ? NextTheme.accent : NextTheme.ok
                            border.color: NextTheme.panel
                            ToolTip.visible: keyHover.hovered
                            ToolTip.text: qsTr("frame %1").arg(modelData.frame) + (modelData.scene.length > 0 ? " · " + modelData.scene : "")
                                          + (modelData.trajectoryFrame !== null ? " · traj " + modelData.trajectoryFrame : "")
                            HoverHandler { id: keyHover }
                        }
                    }
                    Rectangle {
                        x: editor.hasMovie ? timeline.xOf(editor.movie.currentFrame) - 2 : 0
                        y: 12; width: 4; height: 20; radius: 2; color: NextTheme.accent
                    }
                    Caption { x: 0; y: 32; text: "1" }
                    Caption { anchors.right: parent.right; y: 32; text: String(timeline.count) }
                    MouseArea {
                        anchors.fill: parent
                        function frameAt(mouse) { return Math.round(Math.max(0, Math.min(1, (mouse.x - 4) / Math.max(1, width - 8))) * (timeline.count - 1)) + 1 }
                        onPressed: function(mouse) { if (timeline.count > 0) editor.run("movie seek --frame " + frameAt(mouse)) }
                    }
                }
                Caption {
                    visible: editor.hasMovie
                    text: qsTr("%1 keyframes · %2 mapped frames").arg(editor.keyframes.length).arg(editor.hasMovie ? editor.movie.mappedFrames : 0)
                }

                // keyframe form
                Flow {
                    width: parent.width
                    spacing: 8
                    enabled: editor.hasMovie
                    Caption { text: qsTr("keyframe at"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: keyFrame; text: "1"; implicitWidth: 56; objectName: "keyFrame" }
                    Choice {
                        id: sceneBox
                        implicitWidth: 130
                        model: [qsTr("(no scene)")].concat(editor.scenes.map(function(scene) { return scene.name }))
                    }
                    Caption { text: qsTr("traj frame"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: keyTraj; text: ""; implicitWidth: 56; placeholderText: "-" }
                    NextButton {
                        objectName: "storeMovieKeyframeButton"
                        text: qsTr("Set keyframe")
                        enabled: sceneBox.currentIndex > 0 || keyTraj.text.trim().length > 0
                        ToolTip.text: qsTr("A keyframe needs a scene or a trajectory frame")
                        onClicked: {
                            let text = "movie keyframe --frame " + Math.max(1, editor.integer(keyFrame.text, 1))
                            if (sceneBox.currentIndex > 0) text += " --scene " + editor.quote(sceneBox.currentText)
                            if (keyTraj.text.trim().length > 0) text += " --trajectory-frame " + Math.max(0, editor.integer(keyTraj.text, 0))
                            editor.run(text)
                        }
                    }
                }

                // state mapping
                Row {
                    spacing: 8
                    enabled: editor.hasMovie
                    Caption { text: qsTr("map states"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: mapSpec; text: ""; implicitWidth: 200; placeholderText: qsTr("e.g. 1 x30 or 1 -20"); objectName: "mapSpec" }
                    Check { id: appendBox; text: qsTr("append") }
                    NextButton {
                        text: qsTr("Map")
                        enabled: mapSpec.text.trim().length > 0
                        onClicked: editor.run("movie map --spec " + editor.quote(mapSpec.text.trim()) + (appendBox.checked ? " --append true" : ""))
                    }
                }
                Rectangle { width: parent.width; height: 1; color: NextTheme.line }

                // ---- Export ----------------------------------------------------------------------------------
                Flow {
                    width: parent.width
                    spacing: 8
                    enabled: editor.hasMovie
                    NextButton { text: qsTr("Export frames…"); onClicked: editor.formRequested("movie export", ({})) }
                    NextButton { text: qsTr("Export video…"); onClicked: editor.formRequested("movie export-video", ({})) }
                }
                Item { width: 1; height: 6 }
            }
        }
    }
}
