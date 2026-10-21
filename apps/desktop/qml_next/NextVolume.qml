import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Volumes (SEQ-330 G1): the loaded scalar volumes and the three ways to show the active one — contours
// (isosurface/mesh/dot), an orthogonal slice and direct volume rendering. Everything runs the ordinary
// `volume …` commands.
Popup {
    id: panel
    objectName: "volumePanel"
    property var viewport
    property int revision: 0
    signal run(string text)
    signal formRequested(string name, var preset)
    signal openRequested()

    readonly property bool hasVolume: viewport ? viewport.hasVolume : false
    readonly property var volumes: { revision; hasVolume; return viewport ? viewport.volumeItems() : [] }
    readonly property var activeVolume: {
        for (const volume of volumes) if (volume.active) return volume
        return null
    }
    readonly property real minimum: viewport ? viewport.volumeMinimum : 0
    readonly property real maximum: viewport ? viewport.volumeMaximum : 1
    readonly property var colors: ["cyan", "blue", "green", "magenta", "orange", "red", "white", "yellow"]
    readonly property var contourColors: ["default"].concat(colors)
    readonly property var contourModes: ["mesh", "surface", "dot"]
    readonly property var ramps: ["current", "density", "fire", "grayscale", "ice", "spectrum"]

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    x: Overlay.overlay ? Overlay.overlay.width - width - 12 : 0
    y: 52
    width: 420
    height: Overlay.overlay ? Math.max(380, Overlay.overlay.height - 130) : 560
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    function command(text) { run(text); revision += 1 }
    function openPanel(tab) {
        if (tab !== undefined) tabs.currentIndex = tab
        revision += 1
        levelField.text = String(Number(((panel.minimum + panel.maximum) / 2).toPrecision(5)))
        open()
    }
    function number(text, fallback) {
        const value = parseFloat(String(text).trim())
        return isNaN(value) ? fallback : value
    }
    function positive(text) { const v = parseFloat(String(text).trim()); return !isNaN(v) && v > 0 }
    function dims(value) { return Array.isArray(value) ? value.join("×") : String(value) }
    function sliceMaximum() { return viewport ? Math.max(0, viewport.volumeSliceMaximum) : 0 }

    component Field: TextField {
        // Said by screen readers; give each field a `label:` where no placeholder tells what it is.
        property string label: ""
        property string derivedLabel: ""
        Accessible.name: label.length > 0 ? label : (placeholderText.length > 0 ? placeholderText : derivedLabel)
        Item { visible: false; width: 0; height: 0; Component.onCompleted: parent.derivedLabel = NextTheme.captionBefore(parent) }
        implicitHeight: 28
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        placeholderTextColor: NextTheme.muted
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
    }
    component Choice: ComboBox {
        property string label: ""
        property string derivedLabel: ""
        Accessible.name: label.length > 0 ? label : (derivedLabel.length > 0 ? derivedLabel : displayText)
        Item { visible: false; width: 0; height: 0; Component.onCompleted: parent.derivedLabel = NextTheme.captionBefore(parent) }
        implicitHeight: 28
        implicitWidth: 120
        font.pixelSize: NextTheme.fontSize
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
        contentItem: Text { leftPadding: 8; text: parent.displayText; color: NextTheme.text; font: parent.font; verticalAlignment: Text.AlignVCenter }
    }
    component Caption: Text { color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; wrapMode: Text.Wrap }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("VOLUMES"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1 }
            Text {
                Layout.fillWidth: true
                text: panel.activeVolume ? panel.activeVolume.name + " · " + panel.dims(panel.activeVolume.dimensions) + " · " + Number(panel.activeVolume.minimum).toPrecision(4) + " … " + Number(panel.activeVolume.maximum).toPrecision(4) : ""
                color: NextTheme.text
                font.pixelSize: NextTheme.fontSize
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }

        // loaded volumes
        ListView {
            id: volumeList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(96, Math.max(34, count * 30))
            clip: true
            model: panel.volumes
            ScrollBar.vertical: ScrollBar { }
            delegate: Rectangle {
                id: volumeRow
                required property var modelData
                width: volumeList.width
                height: 30
                color: modelData.active ? NextTheme.activeSoft : (rowHover.hovered ? NextTheme.chrome : "transparent")
                HoverHandler { id: rowHover }
                Rectangle {
                    x: 12; anchors.verticalCenter: parent.verticalCenter
                    width: 12; height: 12; radius: 6
                    color: volumeRow.modelData.active ? NextTheme.active : "transparent"
                    border.color: volumeRow.modelData.active ? NextTheme.active : NextTheme.muted
                    ToolTip.visible: dotHover.hovered
                    ToolTip.text: volumeRow.modelData.active ? qsTr("Active: the panels below act on this volume") : ""
                    HoverHandler { id: dotHover }
                }
                Text {
                    anchors { left: parent.left; leftMargin: 34; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    text: volumeRow.modelData.name + "  ·  " + panel.dims(volumeRow.modelData.dimensions)
                    color: NextTheme.text
                    font.pixelSize: NextTheme.fontSize
                    elide: Text.ElideRight
                }
            }
            Text {
                anchors.centerIn: parent
                visible: volumeList.count === 0
                text: qsTr("No volume loaded.")
                color: NextTheme.muted
                font.pixelSize: NextTheme.fontSize
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 6
            spacing: 6
            NextButton { text: qsTr("Open volume…"); onClicked: panel.openRequested() }
            NextButton { text: qsTr("Combine…"); enabled: panel.volumes.length > 1; onClicked: panel.formRequested("volume combine", ({})) }
            NextButton { text: qsTr("Save…"); enabled: panel.activeVolume !== null; onClicked: panel.formRequested("volume save", ({})) }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            Repeater {
                model: [qsTr("Contour"), qsTr("Slice"), qsTr("Direct")]
                delegate: TabButton {
                    required property string modelData
                    text: modelData
                    Accessible.name: modelData
                    font.pixelSize: NextTheme.fontSize
                    contentItem: Text { text: parent.text; font: parent.font; color: parent.checked ? NextTheme.text : NextTheme.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 2; color: parent.parent.checked ? NextTheme.active : NextTheme.line }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex
            enabled: panel.activeVolume !== null
            opacity: enabled ? 1 : 0.45

            // ---- contour -------------------------------------------------------------------------------------
            Flickable {
                clip: true
                contentHeight: contourBody.implicitHeight + 16
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: contourBody
                    x: 14; y: 10
                    width: parent.width - 28
                    spacing: 8
                    Caption { width: parent.width; text: qsTr("A named contour is an isosurface of the active volume. Creating it again with the same name and mode replaces it; a name cannot change mode.") }
                    RowLayout {
                        width: parent.width
                        spacing: 8
                        Column {
                            Layout.fillWidth: true
                            spacing: 3
                            Caption { text: qsTr("Name") }
                            Field { id: contourName; width: parent.width; text: "contour1"; objectName: "contourName" }
                        }
                        Column {
                            spacing: 3
                            Caption { text: qsTr("Mode") }
                            Choice { id: contourMode; model: panel.contourModes; implicitWidth: 96 }
                        }
                        Column {
                            spacing: 3
                            Caption { text: qsTr("Color") }
                            Choice { id: contourColor; model: panel.contourColors; implicitWidth: 104 }
                        }
                    }
                    Caption { text: qsTr("Level") }
                    RowLayout {
                        width: parent.width
                        spacing: 8
                        Slider {
                            id: levelSlider
                            Accessible.name: qsTr("Level")
                            Layout.fillWidth: true
                            from: panel.minimum
                            to: panel.maximum > panel.minimum ? panel.maximum : panel.minimum + 1
                            value: panel.number(levelField.text, from)
                            onMoved: levelField.text = String(Number(value.toPrecision(5)))
                        }
                        Field { id: levelField; label: qsTr("Level value"); objectName: "contourLevel"; implicitWidth: 90; text: "1" }
                    }
                    NextButton { id: carveToggle; text: (checked ? "▾ " : "▸ ") + qsTr("Restrict to a selection"); outlined: false; checkable: true }
                    Grid {
                        visible: carveToggle.checked
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 6
                        Caption { text: qsTr("Selection"); height: 28; verticalAlignment: Text.AlignVCenter }
                        Field { id: carveSelection; width: 230; placeholderText: "@sele" }
                        Caption { text: qsTr("Carve (Å)"); height: 28; verticalAlignment: Text.AlignVCenter }
                        Field { id: carveField; width: 90; text: "0" }
                        Caption { text: qsTr("Buffer (Å)"); height: 28; verticalAlignment: Text.AlignVCenter }
                        Field { id: bufferField; width: 90; text: "0" }
                    }
                    Row {
                        spacing: 8
                        NextButton {
                            text: qsTr("Create / replace")
                            checked: true
                            enabled: contourName.text.trim().length > 0 && !isNaN(parseFloat(levelField.text))
                            onClicked: {
                                let text = "volume contour --name " + panel.quote(contourName.text.trim()) + " --level " + panel.number(levelField.text, 1)
                                    + " --mode " + contourMode.currentText + " --color " + contourColor.currentText
                                if (carveToggle.checked && carveSelection.text.trim().length > 0) {
                                    text += " --selection " + panel.quote(carveSelection.text.trim())
                                    if (panel.number(carveField.text, 0) > 0) text += " --carve " + panel.number(carveField.text, 0)
                                    if (panel.number(bufferField.text, 0) > 0) text += " --buffer " + panel.number(bufferField.text, 0)
                                }
                                panel.command(text)
                            }
                        }
                        NextButton {
                            text: qsTr("Change level only")
                            enabled: contourName.text.trim().length > 0 && !isNaN(parseFloat(levelField.text))
                            ToolTip.text: qsTr("Updates the level of the contour with this name")
                            onClicked: panel.command("volume isolevel --name " + panel.quote(contourName.text.trim()) + " --level " + panel.number(levelField.text, 1))
                        }
                    }
                    Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                    Caption { text: qsTr("Quick isosurface (one, replaced each time)") }
                    Row {
                        spacing: 8
                        Choice { id: quickColor; model: panel.colors; implicitWidth: 96 }
                        NextButton { text: qsTr("Show"); onClicked: panel.command("volume isosurface --level " + panel.number(levelField.text, 1) + " --color " + quickColor.currentText) }
                    }
                }
            }

            // ---- slice ---------------------------------------------------------------------------------------
            Flickable {
                clip: true
                contentHeight: sliceBody.implicitHeight + 16
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: sliceBody
                    x: 14; y: 10
                    width: parent.width - 28
                    spacing: 8
                    Caption { width: parent.width; text: qsTr("A color-mapped plane through the active volume.") }
                    Row {
                        spacing: 8
                        Caption { text: qsTr("Axis"); height: 28; verticalAlignment: Text.AlignVCenter }
                        Repeater {
                            model: ["x", "y", "z"]
                            delegate: NextButton {
                                required property string modelData
                                text: modelData
                                checked: sliceAxis.axis === modelData
                                onClicked: sliceAxis.axis = modelData
                            }
                        }
                        Item { id: sliceAxis; property string axis: "z"; width: 1; height: 1 }
                    }
                    Caption { text: qsTr("Index %1 of %2").arg(Math.round(sliceIndex.value)).arg(panel.sliceMaximum()) }
                    Slider { id: sliceIndex; Accessible.name: qsTr("Slice index"); width: parent.width; from: 0; to: Math.max(1, panel.sliceMaximum()); stepSize: 1; snapMode: Slider.SnapAlways; value: viewport ? viewport.volumeSliceIndex : 0 }
                    Row {
                        spacing: 12
                        Column { spacing: 3; Caption { text: qsTr("Minimum color") } Choice { id: minColor; model: panel.colors; currentIndex: 1; implicitWidth: 104 } }
                        Column { spacing: 3; Caption { text: qsTr("Maximum color") } Choice { id: maxColor; model: panel.colors; currentIndex: 4; implicitWidth: 104 } }
                    }
                    Caption { text: qsTr("Opacity") + "  " + sliceOpacity.value.toFixed(2) }
                    Slider { id: sliceOpacity; Accessible.name: qsTr("Opacity"); width: parent.width; from: 0.05; to: 1; value: 0.8; stepSize: 0.01 }
                    NextButton {
                        text: qsTr("Show slice")
                        checked: true
                        onClicked: panel.command("volume slice --axis " + sliceAxis.axis + " --index " + Math.round(sliceIndex.value)
                                                 + " --minimum-color " + minColor.currentText + " --maximum-color " + maxColor.currentText
                                                 + " --opacity " + sliceOpacity.value.toFixed(2))
                    }
                }
            }

            // ---- direct volume rendering ---------------------------------------------------------------------
            Flickable {
                clip: true
                contentHeight: directBody.implicitHeight + 16
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: directBody
                    x: 14; y: 10
                    width: parent.width - 28
                    spacing: 8
                    Caption { width: parent.width; text: qsTr("Renders the whole volume as a translucent cloud on the GPU.") }
                    Row {
                        spacing: 12
                        Column { spacing: 3; Caption { text: qsTr("Color ramp") } Choice { id: rampBox; model: panel.ramps; implicitWidth: 130 } }
                        Column { spacing: 3; Caption { text: qsTr("Mode") } Choice { id: renderMode; model: ["post-classified", "pre-integrated"]; implicitWidth: 150 } }
                    }
                    Row {
                        spacing: 12
                        Column { spacing: 3; Caption { text: qsTr("Sampling step") } Field { id: stepField; width: 90; text: "0.5" } }
                        Column { spacing: 3; Caption { text: qsTr("Maximum steps") } Field { id: maxStepsField; width: 90; text: "4096" } }
                        Column { spacing: 3; Caption { text: qsTr("Lookup samples") } Field { id: lutField; width: 90; text: "256" } }
                    }
                    Column { spacing: 3; Caption { text: qsTr("Texture budget (bytes)") } Field { id: textureField; width: 180; text: "536870912" } }
                    Row {
                        spacing: 8
                        NextButton {
                            text: qsTr("Render")
                            checked: true
                            enabled: panel.positive(stepField.text) && panel.positive(maxStepsField.text) && panel.positive(lutField.text) && panel.positive(textureField.text)
                            onClicked: panel.command("volume render --ramp " + rampBox.currentText + " --mode " + renderMode.currentText
                                                     + " --sampling-step " + stepField.text.trim() + " --maximum-steps " + maxStepsField.text.trim()
                                                     + " --lookup-table-samples " + lutField.text.trim() + " --texture-budget-bytes " + textureField.text.trim())
                        }
                        NextButton { text: qsTr("Hide"); onClicked: panel.command("volume hide") }
                        NextButton { objectName: "directVolumeTaskCancelButton"; text: qsTr("Cancel"); visible: viewport && viewport.volumeTaskRunning; onClicked: viewport.cancelDirectVolumeTask() }
                    }
                    ProgressBar {
                        width: parent.width
                        visible: viewport && viewport.volumeTaskRunning
                        value: viewport ? viewport.volumeTaskProgress : 0
                    }
                    Caption {
                        width: parent.width
                        text: viewport ? (viewport.volumeGpuState + (viewport.volumeGpuMessage.length > 0 ? " — " + viewport.volumeGpuMessage : "")) : ""
                    }
                }
            }
        }
    }
}
