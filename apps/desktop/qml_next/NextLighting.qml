import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Lighting: ambient light, the headlight, extra lights fixed in camera space and the specular highlight.
// Every control runs an ordinary `setting set --name … --value …` command, so the output window, undo history,
// CLI and Python see the same operation (the same settings as PyMOL: ambient, direct, reflect, specular,
// shininess, light_count, light, light2 … light9).
Popup {
    id: panel
    objectName: "lightingOverlay"
    property var viewport
    property int revision: 0
    signal run(string text)

    // name, label, minimum, maximum, step precision
    readonly property var sliders: [
        {name: "ambient", label: qsTr("Ambient"), from: 0, to: 1},
        {name: "direct", label: qsTr("Headlight"), from: 0, to: 1.5},
        {name: "reflect", label: qsTr("Extra lights"), from: 0, to: 1.5},
        {name: "specular", label: qsTr("Highlight"), from: 0, to: 1.5},
        {name: "shininess", label: qsTr("Shininess"), from: 1, to: 120}
    ]
    readonly property var lightNames: ["light", "light2", "light3", "light4", "light5", "light6", "light7", "light8", "light9"]
    // The look presets set these settings together (values are PyMOL's own where "Default").
    readonly property var presets: [
        {id: "default", label: qsTr("Default"), values: {ambient: 0.14, direct: 0.45, reflect: 0.45, specular: 1.0, shininess: 55, light_count: 2}},
        {id: "soft", label: qsTr("Soft"), values: {ambient: 0.3, direct: 0.35, reflect: 0.35, specular: 0.3, shininess: 30, light_count: 3}},
        {id: "matte", label: qsTr("Matte"), values: {ambient: 0.2, direct: 0.45, reflect: 0.45, specular: 0.0, shininess: 55, light_count: 2}},
        {id: "studio", label: qsTr("Studio"), values: {ambient: 0.2, direct: 0.3, reflect: 0.5, specular: 0.8, shininess: 55, light_count: 4}},
        {id: "dramatic", label: qsTr("Dramatic"), values: {ambient: 0.05, direct: 0.2, reflect: 0.8, specular: 1.2, shininess: 80, light_count: 2}}
    ]
    readonly property var lightDefaults: ({
        light: [-0.4, -0.4, -1.0], light2: [-0.55, -0.7, 0.15], light3: [0.3, -0.6, -0.2], light4: [-1.2, 0.3, -0.2],
        light5: [0.3, 0.6, -0.75], light6: [-0.3, 0.5, 0.0], light7: [0.9, -0.1, -0.15], light8: [1.3, 2.0, 0.8], light9: [-1.7, -0.5, 1.2]
    })

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    x: Overlay.overlay ? Overlay.overlay.width - width - 12 : 0
    y: 52
    width: 420
    height: Overlay.overlay ? Math.max(380, Overlay.overlay.height - 130) : 600
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function command(text) { run(text); revision += 1 }
    function openPanel() { revision += 1; open() }
    function state(name) { revision; return viewport ? viewport.settingState(name, "global") : ({}) }
    function numberOf(name, fallback) {
        const v = Number(state(name).value)
        return isNaN(v) ? fallback : v
    }
    function vectorOf(name) {
        const value = state(name).value
        return value && value.length === 3 ? [Number(value[0]), Number(value[1]), Number(value[2])] : lightDefaults[name]
    }
    function setNumber(name, value) { command("setting set --name " + name + " --value " + Number(value.toFixed(3))) }
    function setVector(name, x, y, z) {
        if (!isFinite(x) || !isFinite(y) || !isFinite(z)) return
        command("setting set --name " + name + " --value " + x + "," + y + "," + z)
    }
    function applyPreset(values) {
        for (const name in values) command("setting set --name " + name + " --value " + values[name])
    }
    function resetAll() {
        for (const entry of sliders) command("setting unset --name " + entry.name)
        command("setting unset --name light_count")
        for (const name of lightNames) command("setting unset --name " + name)
    }
    readonly property int lightCount: Math.max(1, Math.min(10, Math.round(numberOf("light_count", 2))))

    component Caption: Text { color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; wrapMode: Text.Wrap }
    component Heading: Text { color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold }
    component Field: TextField {
        property string label: ""
        Accessible.name: label
        implicitHeight: 26
        implicitWidth: 56
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("LIGHTING"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: qsTr("Reset all"); onClicked: panel.resetAll() }
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

                Heading { text: qsTr("Looks") }
                Caption { width: parent.width; text: qsTr("Lights stay fixed relative to the screen: the side facing you is always lit, however the molecule is turned.") }
                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: panel.presets
                        delegate: NextButton {
                            required property var modelData
                            objectName: "lightingPreset_" + modelData.id
                            text: modelData.label
                            onClicked: panel.applyPreset(modelData.values)
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("Intensity") }
                Repeater {
                    model: panel.sliders
                    delegate: RowLayout {
                        id: sliderRow
                        required property var modelData
                        readonly property real current: panel.numberOf(modelData.name, 0)
                        width: body.width
                        spacing: 8
                        Caption { Layout.preferredWidth: 90; text: sliderRow.modelData.label }
                        Slider {
                            id: slider
                            objectName: "lightingSlider_" + sliderRow.modelData.name
                            Layout.fillWidth: true
                            from: sliderRow.modelData.from
                            to: sliderRow.modelData.to
                            value: sliderRow.current
                            Accessible.name: sliderRow.modelData.label
                            onPressedChanged: if (!pressed) panel.setNumber(sliderRow.modelData.name, value)
                        }
                        Field {
                            label: sliderRow.modelData.label
                            implicitWidth: 52
                            text: String(Number(slider.value.toFixed(2)))
                            onEditingFinished: {
                                const v = parseFloat(text)
                                if (isFinite(v) && v >= 0) panel.setNumber(sliderRow.modelData.name, v)
                            }
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: NextTheme.line }
                Heading { text: qsTr("Lights") }
                Row {
                    spacing: 8
                    Caption { text: qsTr("Number of lights (headlight included)"); height: 26; verticalAlignment: Text.AlignVCenter }
                    SpinBox {
                        id: countBox
                        objectName: "lightingCount"
                        from: 1
                        to: 10
                        value: panel.lightCount
                        implicitHeight: 28
                        font.pixelSize: NextTheme.fontSize
                        Accessible.name: qsTr("Number of lights")
                        onValueModified: panel.setNumber("light_count", value)
                    }
                }
                Caption { width: parent.width; text: qsTr("Each extra light shines in the direction x, y, z (camera space: x right, y up, z toward you).") }
                Repeater {
                    model: panel.lightCount - 1
                    delegate: Row {
                        id: lightRow
                        required property int index
                        readonly property string settingName: panel.lightNames[index]
                        readonly property var vector: panel.vectorOf(settingName)
                        spacing: 6
                        Caption { width: 54; height: 26; verticalAlignment: Text.AlignVCenter; text: qsTr("Light %1").arg(lightRow.index + 1) }
                        Field { id: lx; label: qsTr("Light %1 x").arg(lightRow.index + 1); text: String(lightRow.vector[0]); onEditingFinished: panel.setVector(lightRow.settingName, parseFloat(text), parseFloat(ly.text), parseFloat(lz.text)) }
                        Field { id: ly; label: qsTr("Light %1 y").arg(lightRow.index + 1); text: String(lightRow.vector[1]); onEditingFinished: panel.setVector(lightRow.settingName, parseFloat(lx.text), parseFloat(text), parseFloat(lz.text)) }
                        Field { id: lz; label: qsTr("Light %1 z").arg(lightRow.index + 1); text: String(lightRow.vector[2]); onEditingFinished: panel.setVector(lightRow.settingName, parseFloat(lx.text), parseFloat(ly.text), parseFloat(text)) }
                    }
                }
            }
        }
    }
}
