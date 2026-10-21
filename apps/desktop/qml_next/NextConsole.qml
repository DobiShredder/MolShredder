import QtQuick
import QtQuick.Controls.Basic

// Output window and command line. `lines` is a ListModel owned by the shell so every part of the UI can log.
Rectangle {
    id: console_
    property var viewport
    property alias log: logModel
    property bool outputVisible: true
    property int outputHeight: 140
    signal outputVisibleToggled()
    signal outputHeightEdited(int height)
    color: NextTheme.panel

    readonly property int minOutput: 48
    readonly property int maxOutput: 360
    property var history: []
    property int historyIndex: -1

    function append(kind, text) {
        logModel.append({kind: kind, msg: text})
        if (logModel.count > 2000) logModel.remove(0, logModel.count - 2000)
        logView.positionViewAtEnd()
    }
    function clear() { logModel.clear() }
    function focusInput() { input.forceActiveFocus() }
    signal commandEntered(string text)

    ListModel { id: logModel }

    // splitter
    Rectangle {
        id: splitter
        visible: console_.outputVisible
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: visible ? 7 : 0
        color: NextTheme.chrome
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: NextTheme.line }
        Rectangle {
            anchors.centerIn: parent; width: 40; height: 3; radius: 2
            color: dragArea.containsMouse || dragArea.pressed || splitter.activeFocus ? NextTheme.active : NextTheme.line
        }
        activeFocusOnTab: true
        Accessible.role: Accessible.Splitter
        Accessible.name: qsTr("Resize output window")
        Keys.onUpPressed: console_.outputHeightEdited(Math.min(console_.maxOutput, console_.outputHeight + 16))
        Keys.onDownPressed: console_.outputHeightEdited(Math.max(console_.minOutput, console_.outputHeight - 16))
        MouseArea {
            id: dragArea
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            hoverEnabled: true
            property real pressY: 0
            property int pressHeight: 0
            onPressed: function(mouse) { pressY = mapToItem(null, mouse.x, mouse.y).y; pressHeight = console_.outputHeight }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const y = mapToItem(null, mouse.x, mouse.y).y
                console_.outputHeightEdited(Math.max(console_.minOutput, Math.min(console_.maxOutput, pressHeight + (pressY - y))))
            }
            onDoubleClicked: console_.outputVisibleToggled()
        }
    }

    Rectangle {
        id: head
        visible: console_.outputVisible
        anchors { left: parent.left; right: parent.right; top: splitter.bottom }
        height: visible ? 24 : 0
        color: NextTheme.panel
        Text {
            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
            text: qsTr("OUTPUT"); color: NextTheme.muted
            font.pixelSize: NextTheme.smallFont; font.letterSpacing: 1.1
        }
        Row {
            anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
            spacing: 4
            NextButton { text: qsTr("Clear"); outlined: false; pixelSize: NextTheme.smallFont; height: 20; onClicked: console_.clear() }
            NextButton { text: qsTr("▾ Hide"); outlined: false; pixelSize: NextTheme.smallFont; height: 20
                ToolTip.text: qsTr("Hide the output window (Ctrl+`)"); onClicked: console_.outputVisibleToggled() }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: NextTheme.line }
    }

    ListView {
        id: logView
        visible: console_.outputVisible
        anchors { left: parent.left; right: parent.right; top: head.bottom }
        height: console_.outputVisible ? console_.outputHeight : 0
        clip: true
        model: logModel
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }
        delegate: Item {
            id: logRow
            required property string kind
            required property string msg
            width: logView.width
            height: edit.contentHeight + 1
            TextEdit {
                id: edit
                Accessible.role: Accessible.StaticText
                Accessible.name: edit.text
                x: 10
                width: parent.width - 20
                readOnly: true
                selectByMouse: true
                wrapMode: Text.Wrap
                text: logRow.msg
                font.family: NextTheme.mono
                font.pixelSize: 12
                color: logRow.kind === "cmd" ? NextTheme.active : logRow.kind === "error" ? NextTheme.error
                     : logRow.kind === "ok" ? NextTheme.ok : logRow.kind === "dim" ? NextTheme.muted : NextTheme.text
                selectionColor: NextTheme.accentSoft
                selectedTextColor: NextTheme.text
            }
        }
    }

    Rectangle {
        id: cmdline
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 34
        color: NextTheme.panel
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: NextTheme.line }
        Text {
            id: prompt
            x: 10; anchors.verticalCenter: parent.verticalCenter
            text: "MolShredder>"; color: NextTheme.active
            font.family: NextTheme.mono; font.pixelSize: 13
        }
        TextField {
            id: input
            anchors { left: prompt.right; leftMargin: 8; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            height: 26
            font.family: NextTheme.mono
            font.pixelSize: 13
            color: NextTheme.text
            placeholderText: qsTr("show cartoon, chain A")
            placeholderTextColor: NextTheme.muted
            selectByMouse: true
            background: Rectangle { radius: NextTheme.radius; color: NextTheme.chrome; border.color: input.activeFocus ? NextTheme.active : NextTheme.line }
            Accessible.name: qsTr("Command line")
            onAccepted: {
                const text = input.text.trim()
                if (text.length === 0) return
                if (console_.history[console_.history.length - 1] !== text) console_.history.push(text)
                console_.historyIndex = -1
                input.text = ""
                console_.commandEntered(text)
            }
            Keys.onUpPressed: {
                if (console_.history.length === 0) return
                console_.historyIndex = console_.historyIndex < 0 ? console_.history.length - 1 : Math.max(0, console_.historyIndex - 1)
                input.text = console_.history[console_.historyIndex]
            }
            Keys.onDownPressed: {
                if (console_.historyIndex < 0) return
                console_.historyIndex += 1
                if (console_.historyIndex >= console_.history.length) { console_.historyIndex = -1; input.text = "" }
                else input.text = console_.history[console_.historyIndex]
            }
            Keys.onTabPressed: function(event) {
                event.accepted = true
                if (!console_.viewport) return
                const typed = input.text
                const found = console_.viewport.completeCommandLine(typed)
                if (found.length === 0) return
                if (found.length === 1) {
                    const lastSpace = typed.lastIndexOf(" ")
                    const base = found[0].startsWith("--") && lastSpace >= 0 ? typed.substring(0, lastSpace + 1) : ""
                    input.text = base + found[0] + " "
                    return
                }
                let common = found[0]
                for (const candidate of found) {
                    let i = 0
                    while (i < common.length && i < candidate.length && common[i] === candidate[i]) ++i
                    common = common.substring(0, i)
                }
                const lastSpace = typed.lastIndexOf(" ")
                const base = common.startsWith("--") && lastSpace >= 0 ? typed.substring(0, lastSpace + 1) : ""
                if (common.length > 0) input.text = base + common
                console_.append("dim", found.slice(0, 24).join("   ") + (found.length > 24 ? "   …" : ""))
            }
        }
    }
}
