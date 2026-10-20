import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Run a Python script (SEQ-330 G3). A script runs with MolShredder's permissions, so the person confirms the file and
// chooses whether it runs in an isolated child process (recommended; killable, bounded output). The viewport runs it as
// a background task and streams its output here; Cancel stops it.
Popup {
    id: panel
    objectName: "scriptOverlay"
    property var viewport
    signal browseRequested()
    signal info(string text)

    readonly property bool running: viewport ? viewport.scriptRunning : false
    readonly property bool ready: pathField.text.trim().length > 0 && !running

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 40 : 640)
    height: Math.min(560, Overlay.overlay ? Overlay.overlay.height - 60 : 560)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function openPanel(path) {
        if (path !== undefined && path.length > 0) pathField.text = path
        open()
    }
    function setPath(path) { pathField.text = path }
    function setIsolated(on) { isolatedBox.checked = on }
    // The isolated worker is a plain Python without the molshredder module; scripts that import it need the other mode.
    readonly property bool missingModule: viewport ? viewport.scriptOutput.indexOf("No module named 'molshredder'") >= 0 : false
    // The script text mentions molshredder (checked when the path changes), so it will want the live workspace.
    property bool importsModule: false
    readonly property bool needsInProcess: (importsModule || missingModule) && isolatedBox.checked
    function start() {
        const started = viewport.runPythonScriptPath(pathField.text.trim(), isolatedBox.checked)
        info(started ? qsTr("Script started: %1").arg(pathField.text.trim()) : qsTr("The script could not start."))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Layout.bottomMargin: 0
            Text { text: qsTr("RUN PYTHON SCRIPT"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            wrapMode: Text.Wrap
            color: NextTheme.accent
            font.pixelSize: NextTheme.fontSize
            font.weight: Font.DemiBold
            text: qsTr("Run trusted Python code?")
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            wrapMode: Text.Wrap
            color: NextTheme.muted
            font.pixelSize: NextTheme.smallFont
            text: qsTr("A script runs with the same permissions as MolShredder. It can read files, use the network and start processes, and may leave partial changes in the viewer if it fails. Review the file before continuing.")
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            spacing: 6
            TextField {
                id: pathField
                objectName: "scriptPath"
                Layout.fillWidth: true
                implicitHeight: 28
                placeholderText: qsTr("path of the .py file")
                placeholderTextColor: NextTheme.muted
                color: NextTheme.text
                font.pixelSize: NextTheme.fontSize
                selectByMouse: true
                background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: pathField.activeFocus ? NextTheme.active : NextTheme.line }
                onTextChanged: panel.importsModule = panel.viewport ? panel.viewport.fileContainsText(text.trim(), "molshredder") : false
            }
            NextButton { text: qsTr("Browse…"); onClicked: panel.browseRequested() }
        }
        CheckBox {
            id: isolatedBox
            objectName: "isolatedScriptCheck"
            Layout.leftMargin: 14
            checked: true
            text: qsTr("Run in an isolated child process (recommended)")
            font.pixelSize: NextTheme.fontSize
            indicator: Rectangle {
                y: (isolatedBox.height - height) / 2
                width: 16; height: 16; radius: 3
                color: isolatedBox.checked ? NextTheme.activeSoft : NextTheme.chrome
                border.color: NextTheme.line
                Text { anchors.centerIn: parent; visible: isolatedBox.checked; text: "✓"; color: NextTheme.active; font.pixelSize: 12 }
            }
            contentItem: Text { leftPadding: 22; text: isolatedBox.text; color: NextTheme.text; font: isolatedBox.font; verticalAlignment: Text.AlignVCenter }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            wrapMode: Text.Wrap
            color: NextTheme.muted
            font.pixelSize: NextTheme.smallFont
            text: isolatedBox.checked
                  ? qsTr("Isolated: the script cannot change the open workspace directly, can be cancelled at any time and its output is bounded.")
                  : qsTr("In this process: the script can change the open workspace directly; cancelling waits for the script to yield.")
        }
        RowLayout {
            objectName: "scriptModuleHint"
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            visible: panel.needsInProcess
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: NextTheme.accent
                font.pixelSize: NextTheme.smallFont
                text: qsTr("This script uses molshredder, which the isolated Python does not have. Run it in this process instead, where it drives the open workspace.")
            }
            NextButton { text: qsTr("Run in this process"); onClicked: isolatedBox.checked = false }
        }
        Text { Layout.leftMargin: 14; text: qsTr("Output"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            TextArea {
                objectName: "scriptOutput"
                readOnly: true
                text: panel.viewport ? panel.viewport.scriptOutput : ""
                color: NextTheme.text
                font.family: NextTheme.mono
                font.pixelSize: 12
                wrapMode: TextEdit.WrapAnywhere
                background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: NextTheme.line }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0
            NextButton { text: qsTr("Clear output"); onClicked: viewport.clearScriptOutput() }
            Item { Layout.fillWidth: true }
            NextButton { objectName: "scriptCancelButton"; text: qsTr("Cancel script"); enabled: panel.running; onClicked: viewport.cancelPythonScript() }
            NextButton { objectName: "scriptRunButton"; text: panel.running ? qsTr("Running…") : qsTr("Run script"); checked: true; enabled: panel.ready; onClicked: panel.start() }
        }
    }
}
