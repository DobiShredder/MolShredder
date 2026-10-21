import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Camera (SEQ-330 G2): frame a selection, rotation origins, projection, stereo, clipping planes, stepwise
// navigation and PyMOL-compatible view text. Every control runs an ordinary `view …` / `stereo …` command.
Popup {
    id: panel
    objectName: "viewsOverlay"
    property var viewport
    property int revision: 0
    signal run(string text)

    readonly property var stateModes: ["current", "all", "number"]
    readonly property var clipModes: ["near", "far", "move", "slab", "atoms", "near-set", "far-set"]
    readonly property var axes: ["x", "y", "z"]
    readonly property var stereoModes: ["side_by_side", "crosseye", "walleye", "anaglyph", "quad_buffer", "row_interleaved", "column_interleaved", "checkerboard", "openvr"]
    property string stateMode: "current"
    property string clipMode: "near"
    property string navigationAxis: "x"
    property string projectionMode: "perspective"
    property string stereoMode: "side_by_side"

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    x: Overlay.overlay ? Overlay.overlay.width - width - 12 : 0
    y: 52
    width: 440
    height: Overlay.overlay ? Math.max(380, Overlay.overlay.height - 130) : 600
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    function command(text) { run(text); revision += 1 }
    function openPanel() {
        revision += 1
        projectionMode = viewport && viewport.projectionModeText() === "orthographic" ? "orthographic" : "perspective"
        fovField.text = String(Number(viewport ? viewport.fieldOfViewDegrees() : 30))
        if (viewport && viewport.stereoEnabled()) {
            stereoMode = viewport.stereoModeText()
            swapButton.checked = viewport.stereoSwapEyes()
            shiftField.text = String(viewport.stereoShiftPercent())
            angleField.text = String(viewport.stereoAngleScale())
        }
        stereoEnabledButton.checked = viewport ? viewport.stereoEnabled() : false
        open()
    }
    function cycle(list, current) { return list[(list.indexOf(current) + 1) % list.length] }
    function selection() { return cameraSelectionInput.text.trim().length > 0 ? cameraSelectionInput.text.trim() : "all" }
    function stateText() {
        if (stateMode === "current") return "current"
        if (stateMode === "all") return "all"
        return cameraStateInput.text.trim().length > 0 ? cameraStateInput.text.trim() : "1"
    }
    function seconds() { const v = parseFloat(animateField.text); return isNaN(v) || v < 0 ? 0 : v }
    function number(text, fallback) { const v = parseFloat(String(text).trim()); return isNaN(v) ? fallback : v }
    function framing(name) {
        return "view " + name + " --selection " + quote(selection()) + " --state " + quote(stateText())
    }

    component Field: TextField {
        // Said by screen readers; give each field a `label:` where no placeholder tells what it is.
        property string label: ""
        property string derivedLabel: ""
        Accessible.name: label.length > 0 ? label : (placeholderText.length > 0 ? placeholderText : derivedLabel)
        Item { visible: false; width: 0; height: 0; Component.onCompleted: parent.derivedLabel = NextTheme.captionBefore(parent) }
        implicitHeight: 26
        implicitWidth: 70
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        placeholderTextColor: NextTheme.muted
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
    }
    component Caption: Text { color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; wrapMode: Text.Wrap }
    component Heading: Text { color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("CAMERA"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: qsTr("Reset view"); onClicked: panel.command("view reset") }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: body.implicitHeight + 16
            ScrollBar.vertical: ScrollBar { }
            Column {
                id: body
                x: 14
                width: parent.width - 28
                spacing: 8

                // ---- frame a selection ----------------------------------------------------------------------
                Heading { text: qsTr("Frame a selection") }
                RowLayout {
                    width: parent.width
                    spacing: 6
                    Field { id: cameraSelectionInput; objectName: "cameraSelectionInput"; Layout.fillWidth: true; placeholderText: "all"; font.family: NextTheme.mono }
                    NextButton { text: "sele"; onClicked: cameraSelectionInput.text = "@sele" }
                }
                Row {
                    spacing: 6
                    Caption { text: qsTr("state"); height: 26; verticalAlignment: Text.AlignVCenter }
                    NextButton { id: stateModeButton; objectName: "cameraStateModeButton"; text: panel.stateMode; onClicked: panel.stateMode = panel.cycle(panel.stateModes, panel.stateMode) }
                    Field { id: cameraStateInput; objectName: "cameraStateInput"; implicitWidth: 56; text: "1"; enabled: panel.stateMode === "number"; opacity: enabled ? 1 : 0.4 }
                    Caption { text: qsTr("animate (s)"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: animateField; implicitWidth: 50; text: "0" }
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    NextButton { text: qsTr("Zoom"); onClicked: panel.command(panel.framing("zoom") + " --duration " + panel.seconds()) }
                    NextButton { text: qsTr("Center"); onClicked: panel.command(panel.framing("center") + " --duration " + panel.seconds()) }
                    NextButton { id: orientButton; objectName: "cameraOrientButton"; text: qsTr("Orient"); onClicked: panel.command(panel.framing("orient") + " --duration " + panel.seconds()) }
                    NextButton { text: qsTr("Set rotation origin"); onClicked: panel.command("view origin --selection " + panel.quote(panel.selection()) + " --state " + panel.quote(panel.stateText())) }
                }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("Object origin") }
                Row {
                    spacing: 6
                    Field { id: objectOriginInput; objectName: "objectOriginInput"; implicitWidth: 110; placeholderText: qsTr("object (current)") }
                    Field { id: originX; label: qsTr("Origin x"); objectName: "objectOriginX"; implicitWidth: 56; text: "0" }
                    Field { id: originY; label: qsTr("Origin y"); implicitWidth: 56; text: "0" }
                    Field { id: originZ; label: qsTr("Origin z"); implicitWidth: 56; text: "0" }
                }
                Row {
                    spacing: 6
                    NextButton {
                        text: qsTr("Set origin")
                        enabled: isFinite(Number(originX.text)) && isFinite(Number(originY.text)) && isFinite(Number(originZ.text))
                        onClicked: {
                            let text = "view origin --position " + panel.quote(Number(originX.text) + " " + Number(originY.text) + " " + Number(originZ.text))
                            if (objectOriginInput.text.trim().length > 0) text += " --object " + panel.quote(objectOriginInput.text.trim())
                            panel.command(text)
                        }
                    }
                    NextButton {
                        text: qsTr("Reset object")
                        enabled: objectOriginInput.text.trim().length > 0
                        onClicked: panel.command("view reset --object " + panel.quote(objectOriginInput.text.trim()))
                    }
                }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("Projection") }
                Row {
                    spacing: 6
                    NextButton { id: projectionModeButton; objectName: "projectionModeButton"; text: panel.projectionMode; onClicked: panel.projectionMode = panel.projectionMode === "perspective" ? "orthographic" : "perspective" }
                    Caption { text: qsTr("field of view (°)"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: fovField; objectName: "projectionFovInput"; implicitWidth: 56; text: "30"; enabled: panel.projectionMode === "perspective"; opacity: enabled ? 1 : 0.4 }
                    NextButton { id: preserveButton; objectName: "projectionPreserveButton"; text: qsTr("keep scale"); checkable: true; checked: true }
                    NextButton {
                        text: qsTr("Apply")
                        onClicked: panel.command("view projection --mode " + panel.projectionMode
                                                 + (panel.projectionMode === "perspective" ? " --field-of-view-degrees " + panel.number(fovField.text, 30) : "")
                                                 + " --preserve-scale " + (preserveButton.checked ? "true" : "false"))
                    }
                }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("Stereo") }
                Flow {
                    width: parent.width
                    spacing: 6
                    NextButton { id: stereoEnabledButton; objectName: "stereoEnabledButton"; text: qsTr("enabled"); checkable: true }
                    NextButton { id: stereoModeButton; objectName: "stereoModeButton"; text: panel.stereoMode; onClicked: panel.stereoMode = panel.cycle(panel.stereoModes, panel.stereoMode) }
                    NextButton { id: swapButton; objectName: "stereoSwapButton"; text: qsTr("swap eyes"); checkable: true }
                    Caption { text: qsTr("shift %"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: shiftField; objectName: "stereoShiftInput"; implicitWidth: 50; text: "2" }
                    Caption { text: qsTr("angle ×"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: angleField; objectName: "stereoAngleInput"; implicitWidth: 50; text: "2.1" }
                    NextButton {
                        id: stereoApplyButton
                        objectName: "stereoApplyButton"
                        text: qsTr("Apply")
                        onClicked: panel.command("stereo set --enabled " + (stereoEnabledButton.checked ? "true" : "false") + " --mode " + panel.stereoMode
                                                 + " --swap-eyes " + (swapButton.checked ? "true" : "false") + " --shift-percent " + panel.number(shiftField.text, 2)
                                                 + " --angle-scale " + panel.number(angleField.text, 2.1))
                    }
                }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("Clipping planes") }
                Caption { text: panel.revision >= 0 && viewport ? viewport.clipRangeText() : ""; font.family: NextTheme.mono }
                Flow {
                    width: parent.width
                    spacing: 6
                    NextButton { id: clipModeButton; objectName: "clipModeButton"; text: panel.clipMode; onClicked: panel.clipMode = panel.cycle(panel.clipModes, panel.clipMode) }
                    Caption { text: qsTr("distance"); height: 26; verticalAlignment: Text.AlignVCenter }
                    Field { id: clipDistanceInput; objectName: "clipDistanceInput"; implicitWidth: 56; text: "1" }
                    Field { id: clipSelectionInput; objectName: "clipSelectionInput"; implicitWidth: 110; placeholderText: qsTr("selection (atoms)") }
                    NextButton {
                        text: qsTr("Apply")
                        enabled: isFinite(Number(clipDistanceInput.text))
                        onClicked: {
                            let text = "view clip --mode " + panel.clipMode + " --distance " + Number(clipDistanceInput.text)
                            if (clipSelectionInput.text.trim().length > 0) text += " --selection " + panel.quote(clipSelectionInput.text.trim())
                            panel.command(text)
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("Step the camera") }
                Row {
                    spacing: 6
                    NextButton { id: navigationAxisButton; objectName: "navigationAxisButton"; text: panel.navigationAxis; onClicked: panel.navigationAxis = panel.cycle(panel.axes, panel.navigationAxis) }
                    Field { id: navigationStepInput; label: qsTr("Step size"); objectName: "navigationStepInput"; implicitWidth: 56; text: "1" }
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    NextButton { text: qsTr("Move −"); onClicked: panel.command("view move --axis " + panel.navigationAxis + " --distance " + (-Math.abs(panel.number(navigationStepInput.text, 1)))) }
                    NextButton { text: qsTr("Move +"); onClicked: panel.command("view move --axis " + panel.navigationAxis + " --distance " + Math.abs(panel.number(navigationStepInput.text, 1))) }
                    NextButton { text: qsTr("Turn −"); onClicked: panel.command("view turn --axis " + panel.navigationAxis + " --angle " + (-Math.abs(panel.number(navigationStepInput.text, 1)))) }
                    NextButton { text: qsTr("Turn +"); onClicked: panel.command("view turn --axis " + panel.navigationAxis + " --angle " + Math.abs(panel.number(navigationStepInput.text, 1))) }
                }
                Caption { width: parent.width; text: qsTr("Move steps are in Å, turn steps in degrees.") }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("PyMOL view text") }
                Caption { width: parent.width; text: qsTr("The 18 numbers of PyMOL's get_view; paste some to restore a camera.") }
                ScrollView {
                    width: parent.width
                    height: 64
                    TextArea {
                        id: pymolViewInput
                        objectName: "pymolViewInput"
                        Accessible.name: qsTr("PyMOL view text")
                        color: NextTheme.text
                        font.family: NextTheme.mono
                        font.pixelSize: 11
                        wrapMode: TextEdit.WrapAnywhere
                        selectByMouse: true
                        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: pymolViewInput.activeFocus ? NextTheme.active : NextTheme.line }
                    }
                }
                Row {
                    spacing: 6
                    NextButton { text: qsTr("Copy current view"); onClicked: pymolViewInput.text = viewport ? viewport.pymolViewText() : "" }
                    NextButton {
                        text: qsTr("Apply view")
                        enabled: pymolViewInput.text.trim().length > 0
                        onClicked: panel.command("view import-pymol --values " + panel.quote(pymolViewInput.text.trim().replace(/[\[\]()\s]+/g, ",").replace(/^,|,$/g, "")))
                    }
                }
            }
        }
    }
}
