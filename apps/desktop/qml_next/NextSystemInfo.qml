import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// System information (SEQ-330 G1): graphics device and runtime, platform, build and features of this installation,
// read from the canonical `system info` operation as the viewport runs it (so the graphics section is the live
// renderer, not the headless CLI's "not attached").
Popup {
    id: panel
    objectName: "systemInfoOverlay"
    property var viewport
    property string sourceJson: ""
    property var info: ({})

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, Overlay.overlay ? Overlay.overlay.width - 40 : 560)
    height: Math.min(560, Overlay.overlay ? Overlay.overlay.height - 60 : 560)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function openPanel() {
        sourceJson = viewport ? viewport.systemInfoJson() : ""
        try {
            const parsed = JSON.parse(sourceJson)
            info = parsed.data !== undefined ? parsed.data : parsed
        } catch (error) {
            info = ({})
        }
        open()
    }
    function text(value) {
        if (value === null || value === undefined) return "—"
        return String(value)
    }
    function rows() {
        const graphics = info.runtime && info.runtime.graphics ? info.runtime.graphics : ({})
        const list = [
            {section: qsTr("Graphics")},
            {key: qsTr("status"), value: text(graphics.status)},
            {key: qsTr("device"), value: text(graphics.device_name)},
            {key: qsTr("device type"), value: text(graphics.device_type)},
            {key: qsTr("API"), value: text(graphics.api)},
            {key: qsTr("backend"), value: text(graphics.backend)},
            {key: qsTr("driver"), value: text(graphics.driver_version)},
            {key: qsTr("RHI based"), value: text(graphics.rhi_based)}
        ]
        if (graphics.failure_reason) list.push({key: qsTr("note"), value: text(graphics.failure_reason)})
        list.push({section: qsTr("Platform")})
        if (info.platform) {
            list.push({key: qsTr("system"), value: text(info.platform.operating_system)})
            list.push({key: qsTr("architecture"), value: text(info.platform.architecture)})
        }
        list.push({section: qsTr("Build")})
        list.push({key: qsTr("version"), value: text(info.project_version)})
        list.push({key: qsTr("configuration"), value: text(info.build_configuration)})
        if (info.toolchain) list.push({key: qsTr("compiler"), value: text(info.toolchain.compiler_id) + " " + text(info.toolchain.compiler_version) + " · C++" + text(info.toolchain.cxx_standard)})
        if (info.features) {
            list.push({section: qsTr("Features")})
            for (const name of Object.keys(info.features)) list.push({key: name.replace(/_/g, " "), value: info.features[name] ? qsTr("yes") : qsTr("no")})
        }
        if (info.dependencies) {
            list.push({section: qsTr("Dependencies")})
            for (const name of Object.keys(info.dependencies)) list.push({key: name, value: text(info.dependencies[name])})
        }
        return list
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("SYSTEM INFORMATION"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: qsTr("Copy JSON"); onClicked: { clipboard.text = panel.sourceJson; clipboard.selectAll(); clipboard.copy() } }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }
        TextEdit { id: clipboard; visible: false }
        Text {
            id: summary
            objectName: "systemInfoSummary"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            wrapMode: Text.Wrap
            color: NextTheme.muted
            font.pixelSize: NextTheme.smallFont
            text: qsTr("Values come from the canonical system info operation, as the viewport runs it.")
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 6
            clip: true
            model: panel.rows()
            ScrollBar.vertical: ScrollBar { }
            delegate: Item {
                id: row
                required property var modelData
                width: list.width
                height: modelData.section !== undefined ? 30 : 22
                Text {
                    visible: row.modelData.section !== undefined
                    anchors { left: parent.left; leftMargin: 6; bottom: parent.bottom; bottomMargin: 2 }
                    text: row.modelData.section !== undefined ? row.modelData.section : ""
                    color: NextTheme.text
                    font.pixelSize: NextTheme.fontSize
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: row.modelData.section === undefined
                    x: 12; width: 150
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.key !== undefined ? row.modelData.key : ""
                    color: NextTheme.muted
                    font.pixelSize: NextTheme.fontSize
                }
                Text {
                    visible: row.modelData.section === undefined
                    anchors { left: parent.left; leftMargin: 170; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    text: row.modelData.value !== undefined ? row.modelData.value : ""
                    color: NextTheme.text
                    font.family: NextTheme.mono
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }
}
