import QtQuick
import QtQuick.Controls.Basic

// Movie / trajectory strip. Uses the trajectory when one is attached and the typed movie timeline otherwise.
Rectangle {
    id: bar
    property var viewport
    signal command(string text)
    readonly property bool trajectory: viewport && viewport.hasTrajectory
    readonly property var movie: viewport ? viewport.movieState : ({})
    readonly property bool hasMovie: movie && movie.configured === true && movie.frameCount > 0
    readonly property real frameCount: trajectory ? viewport.trajectoryFrameCount : (hasMovie ? movie.frameCount : 0)
    readonly property real frame: trajectory ? viewport.trajectoryFrame : (hasMovie ? movie.currentFrame - 1 : 0)
    readonly property bool playing: trajectory ? viewport.trajectoryPlaying : (hasMovie && movie.playing === true)
    readonly property real fps: trajectory ? viewport.trajectoryFps : (hasMovie ? movie.fps : 0)
    height: 34
    color: NextTheme.chrome
    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: NextTheme.line }

    function seek(frame) {
        if (trajectory) viewport.seekTrajectory(frame)
        else if (hasMovie) viewport.seekMovie(frame + 1)
    }
    function step(direction) {
        if (trajectory) viewport.stepTrajectory(direction)
        else if (hasMovie) viewport.stepMovie(1)
    }
    function togglePlay() {
        if (trajectory) viewport.setTrajectoryPlaying(!playing)
        else if (hasMovie) viewport.setMoviePlaying(!playing)
    }

    Row {
        id: buttons
        x: 8; anchors.verticalCenter: parent.verticalCenter
        spacing: 3
        NextButton { text: "⏮"; width: 28; enabled: bar.frameCount > 0; ToolTip.text: qsTr("First frame"); onClicked: bar.seek(0) }
        NextButton { text: "◀"; width: 28; enabled: bar.frameCount > 0; ToolTip.text: qsTr("Previous frame"); onClicked: bar.step(-1) }
        NextButton { text: bar.playing ? "❚❚" : "▶"; width: 32; enabled: bar.frameCount > 0; checked: bar.playing
            ToolTip.text: bar.playing ? qsTr("Pause") : qsTr("Play"); onClicked: bar.togglePlay() }
        NextButton { text: "▶▶"; width: 32; enabled: bar.frameCount > 0; ToolTip.text: qsTr("Next frame"); onClicked: bar.step(1) }
        NextButton { text: "⏭"; width: 28; enabled: bar.frameCount > 0; ToolTip.text: qsTr("Last frame"); onClicked: bar.seek(bar.frameCount - 1) }
    }
    Row {
        id: taskRow
        visible: viewport && viewport.trajectoryTaskRunning
        anchors { right: label.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
        spacing: 6
        ProgressBar { objectName: "trajectoryTaskProgress"; width: 90; value: viewport ? viewport.trajectoryTaskProgress : 0; anchors.verticalCenter: parent.verticalCenter }
        NextButton { objectName: "trajectoryTaskCancelButton"; text: qsTr("Cancel"); onClicked: viewport.cancelTrajectoryTask() }
    }
    Text {
        id: label
        anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
        text: bar.frameCount > 0
              ? qsTr("frame %1 / %2 · %3 fps").arg(Math.round(bar.frame) + 1).arg(Math.round(bar.frameCount)).arg(Number(bar.fps).toFixed(0))
              : qsTr("no trajectory or movie")
        font.family: NextTheme.mono
        font.pixelSize: 12
        color: NextTheme.muted
    }
    Item {
        id: track
        anchors { left: buttons.right; leftMargin: 14; right: taskRow.visible ? taskRow.left : label.left; rightMargin: 14; verticalCenter: parent.verticalCenter }
        height: 18
        enabled: bar.frameCount > 1
        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 6; radius: 3; color: NextTheme.line }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: bar.frameCount > 1 ? parent.width * bar.frame / (bar.frameCount - 1) : 0
            height: 6; radius: 3; color: NextTheme.active
        }
        Rectangle {
            visible: bar.frameCount > 1
            x: bar.frameCount > 1 ? (parent.width - 4) * bar.frame / (bar.frameCount - 1) : 0
            anchors.verticalCenter: parent.verticalCenter
            width: 4; height: 16; radius: 2; color: NextTheme.accent
        }
        MouseArea {
            anchors.fill: parent
            function at(mouse) { return Math.round(Math.max(0, Math.min(1, mouse.x / width)) * (bar.frameCount - 1)) }
            onPressed: function(mouse) { bar.seek(at(mouse)) }
            onPositionChanged: function(mouse) { if (pressed) bar.seek(at(mouse)) }
        }
    }
}
