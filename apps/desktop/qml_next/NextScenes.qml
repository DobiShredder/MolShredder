import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Views and scenes (SEQ-330 G1): stored camera views and full-state scenes of the workspace. Everything runs the
// ordinary `view …` and `scene …` commands; the lists mirror what the workspace holds.
Popup {
    id: panel
    property var viewport
    property string selectedView: ""
    property string selectedScene: ""
    signal run(string text)
    signal formRequested(string name, var preset)

    readonly property var views: viewport ? viewport.viewItems : []
    readonly property var scenes: viewport ? viewport.sceneItems : []

    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(760, Overlay.overlay ? Overlay.overlay.width - 40 : 760)
    height: Math.min(470, Overlay.overlay ? Overlay.overlay.height - 60 : 470)
    background: Rectangle { color: NextTheme.panel; border.color: NextTheme.line; radius: 8 }

    function quote(text) { return "\"" + String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"" }
    function duration() {
        const value = parseFloat(durationField.text)
        return isNaN(value) || value < 0 ? 0 : value
    }
    function recallView(name) {
        run("view recall --name " + quote(name) + " --duration " + duration())
    }
    function openPanel() {
        open()
    }

    component Field: TextField {
        implicitHeight: 26
        color: NextTheme.text
        font.pixelSize: NextTheme.fontSize
        placeholderTextColor: NextTheme.muted
        selectByMouse: true
        background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: parent.activeFocus ? NextTheme.active : NextTheme.line }
    }
    component Caption: Text { color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Text { text: qsTr("VIEWS AND SCENES"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1; Layout.fillWidth: true }
            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Close"); onClicked: panel.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- views ---------------------------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 12
                Layout.topMargin: 0
                spacing: 8
                Text { text: qsTr("Views"); color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold }
                Caption { text: qsTr("A view stores the camera only."); Layout.fillWidth: true; wrapMode: Text.Wrap }
                RowLayout {
                    spacing: 6
                    Field { id: viewName; objectName: "viewName"; Layout.fillWidth: true; placeholderText: qsTr("view name") ; onAccepted: storeView.clicked() }
                    NextButton {
                        id: storeView
                        text: qsTr("Store")
                        enabled: viewName.text.trim().length > 0
                        onClicked: { panel.run("view store --name " + panel.quote(viewName.text.trim())); viewName.text = "" }
                    }
                }
                RowLayout {
                    spacing: 6
                    Caption { text: qsTr("animate (s)") }
                    Field { id: durationField; text: "0"; implicitWidth: 48 }
                    NextButton { text: qsTr("Perspective"); onClicked: panel.run("view projection --mode perspective") }
                    NextButton { text: qsTr("Orthographic"); onClicked: panel.run("view projection --mode orthographic") }
                }
                ListView {
                    id: viewList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: panel.views
                    ScrollBar.vertical: ScrollBar { }
                    delegate: Rectangle {
                        id: viewRow
                        required property var modelData
                        width: viewList.width
                        height: 34
                        color: modelData.name === panel.selectedView ? NextTheme.activeSoft : (viewHover.hovered ? NextTheme.chrome : "transparent")
                        HoverHandler { id: viewHover }
                        TapHandler { onTapped: panel.selectedView = viewRow.modelData.name; onDoubleTapped: panel.recallView(viewRow.modelData.name) }
                        Column {
                            anchors { left: parent.left; leftMargin: 8; right: viewButtons.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
                            Text { width: parent.width; text: viewRow.modelData.name; color: NextTheme.text; font.pixelSize: NextTheme.fontSize; elide: Text.ElideRight }
                            Text { width: parent.width; text: viewRow.modelData.projection + " · " + Number(viewRow.modelData.distance).toFixed(1) + " Å"; color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
                        }
                        Row {
                            id: viewButtons
                            anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                            spacing: 4
                            NextButton { text: qsTr("Recall"); onClicked: panel.recallView(viewRow.modelData.name) }
                            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Delete"); onClicked: panel.run("view delete --name " + panel.quote(viewRow.modelData.name)) }
                        }
                    }
                    Text { anchors.centerIn: parent; visible: viewList.count === 0; text: qsTr("no stored views"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    NextButton { text: qsTr("Reset orientation"); onClicked: panel.run("view reset") }
                    NextButton { text: qsTr("Copy view (PyMOL)"); onClicked: panel.run("view export-pymol") }
                    NextButton { text: qsTr("Import view…"); onClicked: panel.formRequested("view import-pymol", ({})) }
                    NextButton { text: qsTr("Delete all"); enabled: viewList.count > 0; onClicked: panel.run("view clear") }
                }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: NextTheme.line }

            // ---- scenes --------------------------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 12
                Layout.topMargin: 0
                spacing: 8
                Text { text: qsTr("Scenes"); color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: Font.DemiBold }
                Caption { text: qsTr("A scene stores camera, representations, colors and visibility."); Layout.fillWidth: true; wrapMode: Text.Wrap }
                RowLayout {
                    spacing: 6
                    Field { id: sceneName; objectName: "sceneName"; Layout.fillWidth: true; placeholderText: qsTr("scene name"); onAccepted: storeScene.clicked() }
                    NextButton {
                        id: storeScene
                        objectName: "storeNamedSceneButton"
                        text: qsTr("Store")
                        enabled: sceneName.text.trim().length > 0
                        onClicked: { panel.run("scene store --name " + panel.quote(sceneName.text.trim())); sceneName.text = "" }
                    }
                }
                Row {
                    spacing: 6
                    NextButton { text: "⏮"; Accessible.name: qsTr("First scene"); onClicked: panel.run("scene first") }
                    NextButton { text: "◀"; Accessible.name: qsTr("Previous scene"); onClicked: panel.run("scene previous") }
                    NextButton { text: "▶"; Accessible.name: qsTr("Next scene"); onClicked: panel.run("scene next") }
                    NextButton { text: "⏭"; Accessible.name: qsTr("Last scene"); onClicked: panel.run("scene last") }
                    NextButton { text: qsTr("Update current"); enabled: sceneList.count > 0; onClicked: panel.run("scene update") }
                }
                ListView {
                    id: sceneList
                    objectName: "namedSceneList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: panel.scenes
                    ScrollBar.vertical: ScrollBar { }
                    delegate: Rectangle {
                        id: sceneRow
                        required property var modelData
                        width: sceneList.width
                        height: 34
                        color: modelData.name === panel.selectedScene ? NextTheme.activeSoft : (sceneHover.hovered ? NextTheme.chrome : "transparent")
                        HoverHandler { id: sceneHover }
                        TapHandler { onTapped: panel.selectedScene = sceneRow.modelData.name; onDoubleTapped: panel.run("scene recall --name " + panel.quote(sceneRow.modelData.name)) }
                        Rectangle {
                            visible: sceneRow.modelData.current
                            x: 0; y: 6; width: 3; height: parent.height - 12; radius: 1; color: NextTheme.active
                        }
                        Column {
                            anchors { left: parent.left; leftMargin: 10; right: sceneButtons.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
                            Text { width: parent.width; text: sceneRow.modelData.name; color: NextTheme.text; font.pixelSize: NextTheme.fontSize; font.weight: sceneRow.modelData.current ? Font.DemiBold : Font.Normal; elide: Text.ElideRight }
                            Text {
                                width: parent.width
                                text: qsTr("%1 objects").arg(sceneRow.modelData.objectCount) + (sceneRow.modelData.volumeCount > 0 ? " · " + qsTr("%1 volumes").arg(sceneRow.modelData.volumeCount) : "")
                                color: NextTheme.muted
                                font.pixelSize: NextTheme.smallFont
                            }
                        }
                        Row {
                            id: sceneButtons
                            anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                            spacing: 4
                            NextButton { text: qsTr("Recall"); onClicked: panel.run("scene recall --name " + panel.quote(sceneRow.modelData.name)) }
                            NextButton { text: qsTr("Rename…"); onClicked: panel.formRequested("scene rename", ({"name": sceneRow.modelData.name})) }
                            NextButton { text: "✕"; outlined: false; Accessible.name: qsTr("Delete"); onClicked: panel.run("scene delete --name " + panel.quote(sceneRow.modelData.name)) }
                        }
                    }
                    Text { anchors.centerIn: parent; visible: sceneList.count === 0; text: qsTr("no stored scenes"); color: NextTheme.muted; font.pixelSize: NextTheme.smallFont }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    NextButton {
                        text: qsTr("Move to top")
                        enabled: panel.selectedScene.length > 0
                        onClicked: panel.run("scene order --names " + panel.quote(panel.selectedScene) + " --location top")
                    }
                    NextButton {
                        text: qsTr("Move to bottom")
                        enabled: panel.selectedScene.length > 0
                        onClicked: panel.run("scene order --names " + panel.quote(panel.selectedScene) + " --location bottom")
                    }
                    NextButton { text: qsTr("Sort by name"); enabled: sceneList.count > 1; onClicked: panel.run("scene order --sort true") }
                    NextButton { text: qsTr("Delete all"); enabled: sceneList.count > 0; onClicked: panel.run("scene clear") }
                }
            }
        }
    }
}
