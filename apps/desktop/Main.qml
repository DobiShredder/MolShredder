import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import MolShredder.Desktop

Window {
    id: root
    property string trajectoryCoordinateUnit: "angstrom"
    property string trajectoryMapping: "exact"
    property string trajectoryAtomMap: ""
    property url pendingScriptUrl
    property var systemInfoData: ({})
    property string systemInfoPanelSourceJson: ""
    property string systemInfoPanelError: ""
    property var chemicalSemanticsData: ({})
    property string chemicalSemanticsError: ""
    property var chemicalPerceptionData: ({})
    property string chemicalPerceptionError: ""
    property var editHistoryData: ({"undo_count": 0, "redo_count": 0,
                                    "memory_used_bytes": 0,
                                    "memory_budget_bytes": 0})
    property var analysisExportResultId: 0
    property string analysisExportFormat: "json"
    property url currentSessionUrl
    readonly property var fileOpenMetadata: localization.actionMetadata("file.open")
    readonly property var fileSaveMetadata: localization.actionMetadata("file.save")
    readonly property var fileOpenSessionMetadata: localization.actionMetadata("file.open-session")
    readonly property var fileSaveSessionMetadata: localization.actionMetadata("file.save-session")
    readonly property var editUndoMetadata: localization.actionMetadata("edit.undo")
    readonly property var editRedoMetadata: localization.actionMetadata("edit.redo")
    readonly property var editAtomPositionMetadata: localization.actionMetadata("edit.atom-position")
    readonly property var editAtomPropertiesMetadata: localization.actionMetadata("edit.atom-properties")
    readonly property var editResiduePropertiesMetadata: localization.actionMetadata("edit.residue-properties")
    readonly property var editBondOrderMetadata: localization.actionMetadata("edit.bond-order")
    readonly property var moleculeBuilderMetadata: localization.actionMetadata("build.molecule")
    readonly property var trajectoryAttachMetadata: localization.actionMetadata("trajectory.attach")
    readonly property var runScriptMetadata: localization.actionMetadata("tools.run-script")
    readonly property var representLinesMetadata: localization.actionMetadata("represent.lines")
    readonly property var representSticksMetadata: localization.actionMetadata("represent.sticks")
    readonly property var representSpheresMetadata: localization.actionMetadata("represent.spheres")
    readonly property var representRibbonMetadata: localization.actionMetadata("represent.ribbon")
    readonly property var representCartoonMetadata: localization.actionMetadata("represent.cartoon")
    readonly property var representSurfaceMetadata: localization.actionMetadata("represent.surface")
    readonly property var renderSettingsMetadata: localization.actionMetadata("represent.settings")
    readonly property var volumeSliceMetadata: localization.actionMetadata("represent.volume-slice")
    readonly property var directVolumeMetadata: localization.actionMetadata("represent.volume-render")
    readonly property var analyzePanelMetadata: localization.actionMetadata("analyze.open-panel")
    readonly property var sceneViewsMetadata: localization.actionMetadata("scene.views")
    readonly property var namedScenesMetadata: localization.actionMetadata("scene.named-scenes")
    readonly property var movieMetadata: localization.actionMetadata("scene.movie")
    readonly property var systemInformationMetadata: localization.actionMetadata("help.system-information")
    readonly property var objectPanelMetadata: localization.actionMetadata("object.panel")
    readonly property var objectChemistryMetadata: localization.actionMetadata("object.chemistry")
    readonly property var selectExpressionMetadata: localization.actionMetadata("select.expression")
    readonly property var selectAllMetadata: localization.actionMetadata("select.all")
    readonly property var trajectoryPlaybackMetadata: localization.actionMetadata("trajectory.play-pause")
    readonly property var representShowMetadata: localization.actionMetadata("represent.show")
    readonly property var representHideMetadata: localization.actionMetadata("represent.hide")
    readonly property var representAsMetadata: localization.actionMetadata("represent.as")
    readonly property var representToggleMetadata: localization.actionMetadata("represent.toggle")
    readonly property bool hasWorkspace: viewport.objectItems.length > 0

    function systemInfoValue(group, key) {
        if (!root.systemInfoData || !root.systemInfoData[group])
            return qsTr("Not reported")
        const value = root.systemInfoData[group][key]
        return value === null || value === undefined || value === ""
               ? qsTr("Not reported") : String(value)
    }

    function graphicsInfoValue(key) {
        if (!root.systemInfoData || !root.systemInfoData.runtime ||
                !root.systemInfoData.runtime.graphics)
            return qsTr("Not reported")
        const value = root.systemInfoData.runtime.graphics[key]
        return value === null || value === undefined || value === ""
               ? qsTr("Not reported") : String(value)
    }

    function openSystemInfo() {
        root.systemInfoPanelSourceJson = viewport.systemInfoJson()
        try {
            const envelope = JSON.parse(root.systemInfoPanelSourceJson)
            if (envelope.status !== "ok" || !envelope.data)
                throw new Error("system info operation did not return data")
            root.systemInfoData = envelope.data
            root.systemInfoPanelError = ""
        } catch (error) {
            root.systemInfoData = ({})
            root.systemInfoPanelError = String(error)
        }
        systemInfoOverlay.visible = true
    }

    function openChemicalSemantics() {
        try {
            const envelope = JSON.parse(viewport.chemicalSemanticsJson())
            if (envelope.status !== "ok" || !envelope.data)
                throw new Error("chemical semantics operation did not return data")
            root.chemicalSemanticsData = envelope.data
            root.chemicalSemanticsError = ""
        } catch (error) {
            root.chemicalSemanticsData = ({})
            root.chemicalSemanticsError = String(error)
        }
        chemicalSemanticsOverlay.visible = true
    }

    function runChemicalPerception(apply) {
        try {
            const envelope = JSON.parse(viewport.chemicalPerceptionJson(apply))
            if (envelope.status !== "ok" || !envelope.data)
                throw new Error("chemical perception operation did not return data")
            root.chemicalPerceptionData = envelope.data
            root.chemicalPerceptionError = ""
            if (envelope.data.applied) {
                const chemistryEnvelope = JSON.parse(viewport.chemicalSemanticsJson())
                if (chemistryEnvelope.status === "ok" && chemistryEnvelope.data)
                    root.chemicalSemanticsData = chemistryEnvelope.data
            }
        } catch (error) {
            root.chemicalPerceptionData = ({})
            root.chemicalPerceptionError = String(error)
        }
    }

    function openViews() {
        const projection = viewport.projectionModeText()
        viewsOverlay.projectionModeIndex =
            viewsOverlay.projectionModes.indexOf(projection)
        if (viewsOverlay.projectionModeIndex < 0)
            viewsOverlay.projectionModeIndex = 0
        projectionFovInput.text = String(viewport.fieldOfViewDegrees())
        viewsOverlay.stereoEnabled = viewport.stereoEnabled()
        const stereoMode = viewport.stereoModeText()
        viewsOverlay.stereoModeIndex =
            viewsOverlay.stereoModes.indexOf(stereoMode)
        if (viewsOverlay.stereoModeIndex < 0)
            viewsOverlay.stereoModeIndex = 0
        viewsOverlay.stereoSwapEyes = viewport.stereoSwapEyes()
        const anaglyphMode = viewport.anaglyphModeText()
        viewsOverlay.anaglyphModeIndex =
            viewsOverlay.anaglyphModes.indexOf(anaglyphMode)
        if (viewsOverlay.anaglyphModeIndex < 0)
            viewsOverlay.anaglyphModeIndex = 4
        stereoShiftInput.text = String(viewport.stereoShiftPercent())
        stereoAngleInput.text = String(viewport.stereoAngleScale())
        viewsOverlay.visible = true
        viewsOverlay.clipRange = viewport.clipRangeText()
        viewNameInput.forceActiveFocus()
    }

    function openRenderSettings() {
        renderSettingsOverlay.visible = true
        renderSettingNameInput.forceActiveFocus()
        renderSettingResult.text = qsTr("Choose a P0 setting, scope and value. Atom/bond targets are 1-based.")
    }

    function refreshEditHistory() {
        try {
            const envelope = JSON.parse(viewport.editHistoryJson())
            if (envelope.status === "ok" && envelope.data)
                root.editHistoryData = envelope.data
        } catch (error) {
            root.editHistoryData = ({"undo_count": 0, "redo_count": 0,
                                     "memory_used_bytes": 0,
                                     "memory_budget_bytes": 0})
        }
    }

    function openCoordinateEditor() {
        root.refreshEditHistory()
        coordinateEditOverlay.visible = true
        coordinateAtomInput.forceActiveFocus()
    }

    function openTopologyEditor(mode) {
        topologyEditOverlay.modeIndex = mode
        root.refreshEditHistory()
        topologyEditOverlay.visible = true
        if (mode === 2)
            topologyBondIdInput.forceActiveFocus()
        else
            topologyAtomIdInput.forceActiveFocus()
    }

    function openMoleculeBuilder() {
        moleculeBuilderOverlay.visible = true
        moleculeBuilderNameInput.forceActiveFocus()
    }

    function openAnalyze() {
        analysisOverlay.visible = true
        analysisPrimaryInput.forceActiveFocus()
    }

    function openSelectionEditor() {
        selectionExpressionOverlay.visible = true
        selectionExpressionInput.forceActiveFocus()
    }

    function applySelectionEditor() {
        if (viewport.defineSelection(selectionNameInput.text,
                                     selectionExpressionInput.text,
                                     selectionDynamicCheck.checked))
            selectionExpressionOverlay.visible = false
    }

    function openCommandPalette() {
        commandPaletteQuery.text = ""
        commandPaletteOverlay.visible = true
        commandPaletteQuery.forceActiveFocus()
    }

    function sessionVisiblePanels() {
        const panels = []
        if (selectionExpressionOverlay.visible) panels.push("selection")
        if (analysisOverlay.visible) panels.push("analysis")
        if (coordinateEditOverlay.visible) panels.push("coordinate-edit")
        if (topologyEditOverlay.visible) panels.push("topology-edit")
        if (moleculeBuilderOverlay.visible) panels.push("molecule-builder")
        if (renderSettingsOverlay.visible) panels.push("render-settings")
        if (viewsOverlay.visible) panels.push("views")
        if (chemicalSemanticsOverlay.visible) panels.push("chemistry")
        if (systemInfoOverlay.visible) panels.push("system-information")
        if (trajectoryImportOverlay.visible) panels.push("trajectory-import")
        if (surfacePanel.visible) panels.push("surface")
        if (volumePanel.visible) panels.push("volume")
        return panels.join(",")
    }

    function restoreSessionVisiblePanels() {
        selectionExpressionOverlay.visible = false
        analysisOverlay.visible = false
        coordinateEditOverlay.visible = false
        topologyEditOverlay.visible = false
        moleculeBuilderOverlay.visible = false
        renderSettingsOverlay.visible = false
        viewsOverlay.visible = false
        chemicalSemanticsOverlay.visible = false
        systemInfoOverlay.visible = false
        trajectoryImportOverlay.visible = false
        surfacePanel.visible = false
        volumePanel.visible = false
        const panels = viewport.sessionVisiblePanels.split(",")
        for (let index = 0; index < panels.length; ++index) {
            switch (panels[index]) {
            case "selection": root.openSelectionEditor(); break
            case "analysis": root.openAnalyze(); break
            case "coordinate-edit": root.openCoordinateEditor(); break
            case "topology-edit": root.openTopologyEditor(0); break
            case "molecule-builder": root.openMoleculeBuilder(); break
            case "render-settings": root.openRenderSettings(); break
            case "views": root.openViews(); break
            case "chemistry": root.openChemicalSemantics(); break
            case "system-information": root.openSystemInfo(); break
            case "trajectory-import": trajectoryImportOverlay.visible = true; break
            case "surface": surfacePanel.visible = true; break
            case "volume": volumePanel.visible = true; break
            }
        }
    }
    width: 1080
    height: 720
    visible: true
    color: "#050812"
    title: qsTr("MolShredder Molecular Viewer")

    Action {
        id: fileOpenAction
        objectName: "fileOpenAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.fileOpenMetadata.label)
        }
        shortcut: root.fileOpenMetadata.shortcut === "standard.open"
                  ? StandardKey.Open : ""
        onTriggered: structureDialog.open()
    }

    Action {
        id: fileSaveAction
        objectName: "fileSaveAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.fileSaveMetadata.label)
        }
        enabled: root.hasWorkspace
        shortcut: root.fileSaveMetadata.shortcut === "standard.save"
                  ? StandardKey.Save : ""
        onTriggered: saveDialog.open()
    }

    Action {
        id: fileOpenSessionAction
        objectName: "fileOpenSessionAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.fileOpenSessionMetadata.label)
        }
        onTriggered: sessionOpenDialog.open()
    }

    Action {
        id: fileSaveSessionAction
        objectName: "fileSaveSessionAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.fileSaveSessionMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: sessionSaveDialog.open()
    }

    Action {
        id: trajectoryAttachAction
        objectName: "trajectoryAttachAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.trajectoryAttachMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: trajectoryImportOverlay.visible = true
    }

    Action {
        id: runScriptAction
        objectName: "runScriptAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.runScriptMetadata.label)
        }
        onTriggered: scriptDialog.open()
    }

    Action {
        id: representLinesAction
        objectName: "representLinesAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representLinesMetadata.label)
        }
        enabled: root.hasWorkspace
        checkable: true
        checked: viewport.representation === "lines"
        onTriggered: viewport.setRepresentation("lines")
    }

    Action {
        id: representSticksAction
        objectName: "representSticksAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representSticksMetadata.label)
        }
        enabled: root.hasWorkspace
        checkable: true
        checked: viewport.representation === "sticks"
        onTriggered: viewport.setRepresentation("sticks")
    }

    Action {
        id: representSpheresAction
        objectName: "representSpheresAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representSpheresMetadata.label)
        }
        enabled: root.hasWorkspace
        checkable: true
        checked: viewport.representation === "spheres"
        onTriggered: viewport.setRepresentation("spheres")
    }

    Action {
        id: representRibbonAction
        objectName: "representRibbonAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representRibbonMetadata.label)
        }
        enabled: root.hasWorkspace
        checkable: true
        checked: viewport.representation === "ribbon"
        onTriggered: viewport.setRepresentation("ribbon")
    }

    Action {
        id: representCartoonAction
        objectName: "representCartoonAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representCartoonMetadata.label)
        }
        enabled: root.hasWorkspace
        checkable: true
        checked: viewport.representation === "cartoon"
        onTriggered: viewport.setRepresentation("cartoon")
    }

    Action {
        id: representSurfaceAction
        objectName: "representSurfaceAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representSurfaceMetadata.label)
        }
        enabled: root.hasWorkspace
        checkable: true
        checked: viewport.representation === "surface"
        onTriggered: {
            if (viewport.representation !== "surface") {
                surfacePanel.visible = true
                surfaceSelectionInput.forceActiveFocus()
            } else {
                surfacePanel.visible = false
                viewport.hideMolecularSurface()
            }
        }
    }

    Action {
        id: renderSettingsAction
        objectName: "renderSettingsAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.renderSettingsMetadata.label)
        }
        onTriggered: root.openRenderSettings()
    }

    Action {
        id: coordinateEditAction
        objectName: "coordinateEditAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.editAtomPositionMetadata.label)
        }
        enabled: root.hasWorkspace && !viewport.hasTrajectory
        onTriggered: root.openCoordinateEditor()
    }

    Action {
        id: atomPropertiesAction
        objectName: "atomPropertiesAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.editAtomPropertiesMetadata.label)
        }
        enabled: root.hasWorkspace && !viewport.hasTrajectory
        onTriggered: root.openTopologyEditor(0)
    }

    Action {
        id: residuePropertiesAction
        objectName: "residuePropertiesAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.editResiduePropertiesMetadata.label)
        }
        enabled: root.hasWorkspace && !viewport.hasTrajectory
        onTriggered: root.openTopologyEditor(1)
    }

    Action {
        id: bondOrderAction
        objectName: "bondOrderAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.editBondOrderMetadata.label)
        }
        enabled: root.hasWorkspace && !viewport.hasTrajectory
        onTriggered: root.openTopologyEditor(2)
    }

    Action {
        id: undoEditAction
        objectName: "undoEditAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.editUndoMetadata.label)
        }
        enabled: root.hasWorkspace
        shortcut: StandardKey.Undo
        onTriggered: {
            viewport.undoEdit()
            root.refreshEditHistory()
        }
    }

    Action {
        id: moleculeBuilderAction
        objectName: "moleculeBuilderAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.moleculeBuilderMetadata.label)
        }
        onTriggered: root.openMoleculeBuilder()
    }

    Action {
        id: redoEditAction
        objectName: "redoEditAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.editRedoMetadata.label)
        }
        enabled: root.hasWorkspace
        shortcut: StandardKey.Redo
        onTriggered: {
            viewport.redoEdit()
            root.refreshEditHistory()
        }
    }

    Action {
        id: volumeSliceAction
        objectName: "volumeSliceAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.volumeSliceMetadata.label)
        }
        enabled: viewport.hasVolume
        onTriggered: viewport.setVolumeSlice(viewport.volumeSliceAxis,
                                             viewport.volumeSliceIndex)
    }

    Action {
        id: directVolumeAction
        objectName: "directVolumeAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.directVolumeMetadata.label)
        }
        enabled: viewport.hasVolume
        checkable: true
        checked: viewport.volumeMode === "direct"
        onTriggered: viewport.volumeMode === "direct"
                     ? viewport.hideDirectVolume()
                     : viewport.setDirectVolume(volumePanel.rampPreset,
                                                volumePanel.samplingStep,
                                                4096, 256, 536870912)
    }

    Action {
        id: analyzePanelAction
        objectName: "analyzePanelAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.analyzePanelMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: root.openAnalyze()
    }

    Action {
        id: sceneViewsAction
        objectName: "sceneViewsAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.sceneViewsMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: root.openViews()
    }

    Action {
        id: systemInformationAction
        objectName: "systemInformationAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.systemInformationMetadata.label)
        }
        onTriggered: root.openSystemInfo()
    }

    Action {
        id: objectPanelAction
        objectName: "objectPanelAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.objectPanelMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: objectPanel.forceActiveFocus()
    }

    Action {
        id: objectChemistryAction
        objectName: "objectChemistryAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.objectChemistryMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: root.openChemicalSemantics()
    }

    Action {
        id: selectExpressionAction
        objectName: "selectExpressionAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.selectExpressionMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: root.openSelectionEditor()
    }

    Action {
        id: selectAllAction
        objectName: "selectAllAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.selectAllMetadata.label)
        }
        enabled: root.hasWorkspace
        shortcut: root.selectAllMetadata.shortcut === "standard.select-all"
                  ? StandardKey.SelectAll : ""
        onTriggered: viewport.selectAll()
    }

    Action {
        id: trajectoryPlaybackAction
        objectName: "trajectoryPlaybackAction"
        text: viewport.trajectoryPlaying ? qsTr("Pause") : qsTr("Play")
        enabled: viewport.hasTrajectory
        checkable: true
        checked: viewport.trajectoryPlaying
        shortcut: root.trajectoryPlaybackMetadata.shortcut ===
                  "standard.media-toggle-play-pause" ? "Space" : ""
        onTriggered: viewport.setTrajectoryPlaying(!viewport.trajectoryPlaying)
    }

    Action {
        id: namedScenesAction
        objectName: "namedScenesAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.namedScenesMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: root.openViews()
    }

    Action {
        id: movieTimelineAction
        objectName: "movieTimelineAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.movieMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: root.openViews()
    }

    Action {
        id: representShowAction
        objectName: "representShowAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representShowMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: viewport.applyRepresentationVisibility("show", "all")
    }

    Action {
        id: representHideAction
        objectName: "representHideAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representHideMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: viewport.applyRepresentationVisibility("hide", "all")
    }

    Action {
        id: representAsAction
        objectName: "representAsAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representAsMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: viewport.applyRepresentationVisibility("as", "all")
    }

    Action {
        id: representToggleAction
        objectName: "representToggleAction"
        text: {
            localization.currentLanguage
            return localization.translateUi(root.representToggleMetadata.label)
        }
        enabled: root.hasWorkspace
        onTriggered: viewport.applyRepresentationVisibility("toggle", "all")
    }

    Action {
        id: showCommandPaletteAction
        objectName: "showCommandPaletteAction"
        text: qsTr("Command Palette")
        shortcut: Qt.platform.os === "osx" ? "Meta+Shift+P" : "Ctrl+Shift+P"
        onTriggered: root.openCommandPalette()
    }

    MenuBar {
        id: mainMenuBar
        objectName: "mainMenuBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        z: 50

        Menu {
            id: fileMenu
            objectName: "fileMenu"
            title: qsTr("File")

            MenuItem {
                objectName: "fileOpenMenuItem"
                action: fileOpenAction
            }

            MenuItem {
                objectName: "fileSaveMenuItem"
                action: fileSaveAction
            }
            MenuSeparator {}
            MenuItem {
                objectName: "fileOpenSessionMenuItem"
                action: fileOpenSessionAction
            }
            MenuItem {
                objectName: "fileSaveSessionMenuItem"
                action: fileSaveSessionAction
            }
        }

        Menu {
            id: editMenu
            objectName: "editMenu"
            title: qsTr("Edit")

            MenuItem {
                objectName: "editUndoMenuItem"
                action: undoEditAction
            }
            MenuItem {
                objectName: "editRedoMenuItem"
                action: redoEditAction
            }
            MenuSeparator {}
            MenuItem {
                objectName: "editAtomCoordinatesMenuItem"
                action: coordinateEditAction
            }
            MenuItem {
                objectName: "editAtomPropertiesMenuItem"
                action: atomPropertiesAction
            }
            MenuItem {
                objectName: "editResiduePropertiesMenuItem"
                action: residuePropertiesAction
            }
            MenuItem {
                objectName: "editBondOrderMenuItem"
                action: bondOrderAction
            }
            MenuItem {
                objectName: "editMoleculeBuilderMenuItem"
                action: moleculeBuilderAction
            }
        }

        Menu {
            id: objectMenu
            objectName: "objectMenu"
            title: qsTr("Object")

            MenuItem {
                objectName: "objectPanelMenuItem"
                action: objectPanelAction
            }
            MenuItem {
                objectName: "objectChemistryMenuItem"
                action: objectChemistryAction
            }
        }

        Menu {
            id: selectMenu
            objectName: "selectMenu"
            title: qsTr("Select")

            MenuItem {
                objectName: "selectExpressionMenuItem"
                action: selectExpressionAction
            }
            MenuItem {
                objectName: "selectAllMenuItem"
                action: selectAllAction
            }
        }

        Menu {
            id: representMenu
            objectName: "representMenu"
            title: qsTr("Represent")

            MenuItem {
                objectName: "representLinesMenuItem"
                action: representLinesAction
            }
            MenuItem {
                objectName: "representSticksMenuItem"
                action: representSticksAction
            }
            MenuItem {
                objectName: "representSpheresMenuItem"
                action: representSpheresAction
            }
            MenuItem {
                objectName: "representRibbonMenuItem"
                action: representRibbonAction
            }
            MenuItem {
                objectName: "representCartoonMenuItem"
                action: representCartoonAction
            }
            MenuItem {
                objectName: "representSurfaceMenuItem"
                action: representSurfaceAction
            }
            MenuSeparator {}
            MenuItem {
                objectName: "renderSettingsMenuItem"
                action: renderSettingsAction
            }
            MenuItem {
                objectName: "volumeSliceMenuItem"
                action: volumeSliceAction
            }
            MenuItem {
                objectName: "directVolumeMenuItem"
                action: directVolumeAction
            }
            MenuSeparator {}
            MenuItem {
                objectName: "representShowMenuItem"
                action: representShowAction
            }
            MenuItem {
                objectName: "representHideMenuItem"
                action: representHideAction
            }
            MenuItem {
                objectName: "representAsMenuItem"
                action: representAsAction
            }
            MenuItem {
                objectName: "representToggleMenuItem"
                action: representToggleAction
            }
        }

        Menu {
            id: analyzeMenu
            objectName: "analyzeMenu"
            title: qsTr("Analyze")

            MenuItem {
                objectName: "analyzePanelMenuItem"
                action: analyzePanelAction
            }
        }

        Menu {
            id: trajectoryMenu
            objectName: "trajectoryMenu"
            title: qsTr("Trajectory")

            MenuItem {
                objectName: "trajectoryAttachMenuItem"
                action: trajectoryAttachAction
            }
            MenuItem {
                objectName: "trajectoryPlaybackMenuItem"
                action: trajectoryPlaybackAction
            }
        }

        Menu {
            id: sceneMenu
            objectName: "sceneMenu"
            title: qsTr("Scene")

            MenuItem {
                objectName: "sceneViewsMenuItem"
                action: sceneViewsAction
            }
            MenuItem {
                objectName: "namedScenesMenuItem"
                action: namedScenesAction
            }
            MenuItem {
                objectName: "movieTimelineMenuItem"
                action: movieTimelineAction
            }
        }

        Menu {
            id: toolsMenu
            objectName: "toolsMenu"
            title: qsTr("Tools")

            MenuItem {
                objectName: "runScriptMenuItem"
                action: runScriptAction
            }
        }

        Menu {
            id: helpMenu
            objectName: "helpMenu"
            title: qsTr("Help")

            MenuItem {
                objectName: "systemInformationMenuItem"
                action: systemInformationAction
            }
            MenuSeparator {}
            Menu {
                objectName: "languageMenu"
                title: qsTr("Language")
                MenuItem {
                    objectName: "languageEnglishMenuItem"
                    text: qsTr("English")
                    checkable: true
                    checked: localization.currentLanguage === "en"
                    onTriggered: localization.setLanguage("en")
                }
                MenuItem {
                    objectName: "languageKoreanMenuItem"
                    text: qsTr("Korean")
                    checkable: true
                    checked: localization.currentLanguage === "ko"
                    onTriggered: localization.setLanguage("ko")
                }
            }
        }
    }

    Rectangle {
        id: commandPaletteOverlay
        objectName: "commandPaletteOverlay"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: mainMenuBar.bottom
        anchors.topMargin: 18
        width: Math.min(560, parent.width - 40)
        height: Math.min(commandPaletteEntries.height + 66,
                         root.height - mainMenuBar.height - 40)
        visible: false
        radius: 10
        color: "#f2101827"
        border.color: "#69aef0"
        z: 60

        TextInput {
            id: commandPaletteQuery
            objectName: "commandPaletteQuery"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            height: 34
            color: "#eef6ff"
            selectByMouse: true
            font.pixelSize: 15
            Keys.onEscapePressed: commandPaletteOverlay.visible = false

            Text {
                anchors.fill: parent
                text: qsTr("Search actions")
                visible: parent.text.length === 0
                color: "#71849c"
                font.pixelSize: 15
            }
        }

        Flickable {
            id: commandPaletteScroll
            objectName: "commandPaletteScroll"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: commandPaletteQuery.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: 10
            anchors.bottomMargin: 12
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            contentWidth: width
            contentHeight: commandPaletteEntries.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: commandPaletteEntries
                width: commandPaletteScroll.width
                spacing: 4

            CommandPaletteEntry {
                objectName: "commandPaletteFileOpen"
                width: parent.width
                metadata: root.fileOpenMetadata
                commandAction: fileOpenAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteFileSave"
                width: parent.width
                metadata: root.fileSaveMetadata
                commandAction: fileSaveAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteFileOpenSession"
                width: parent.width
                metadata: root.fileOpenSessionMetadata
                commandAction: fileOpenSessionAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteFileSaveSession"
                width: parent.width
                metadata: root.fileSaveSessionMetadata
                commandAction: fileSaveSessionAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteEditUndo"
                width: parent.width
                metadata: root.editUndoMetadata
                commandAction: undoEditAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteEditRedo"
                width: parent.width
                metadata: root.editRedoMetadata
                commandAction: redoEditAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteEditAtomPosition"
                width: parent.width
                metadata: root.editAtomPositionMetadata
                commandAction: coordinateEditAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteEditAtomProperties"
                width: parent.width
                metadata: root.editAtomPropertiesMetadata
                commandAction: atomPropertiesAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteEditResidueProperties"
                width: parent.width
                metadata: root.editResiduePropertiesMetadata
                commandAction: residuePropertiesAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteEditBondOrder"
                width: parent.width
                metadata: root.editBondOrderMetadata
                commandAction: bondOrderAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteMoleculeBuilder"
                width: parent.width
                metadata: root.moleculeBuilderMetadata
                commandAction: moleculeBuilderAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteTrajectoryAttach"
                width: parent.width
                metadata: root.trajectoryAttachMetadata
                commandAction: trajectoryAttachAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRunScript"
                width: parent.width
                metadata: root.runScriptMetadata
                commandAction: runScriptAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentLines"
                width: parent.width
                metadata: root.representLinesMetadata
                commandAction: representLinesAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentSticks"
                width: parent.width
                metadata: root.representSticksMetadata
                commandAction: representSticksAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentSpheres"
                width: parent.width
                metadata: root.representSpheresMetadata
                commandAction: representSpheresAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentRibbon"
                width: parent.width
                metadata: root.representRibbonMetadata
                commandAction: representRibbonAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentCartoon"
                width: parent.width
                metadata: root.representCartoonMetadata
                commandAction: representCartoonAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentSurface"
                width: parent.width
                metadata: root.representSurfaceMetadata
                commandAction: representSurfaceAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRenderSettings"
                width: parent.width
                metadata: root.renderSettingsMetadata
                commandAction: renderSettingsAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteVolumeSlice"
                width: parent.width
                metadata: root.volumeSliceMetadata
                commandAction: volumeSliceAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteDirectVolume"
                width: parent.width
                metadata: root.directVolumeMetadata
                commandAction: directVolumeAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteAnalyzePanel"
                width: parent.width
                metadata: root.analyzePanelMetadata
                commandAction: analyzePanelAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteSceneViews"
                width: parent.width
                metadata: root.sceneViewsMetadata
                commandAction: sceneViewsAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteNamedScenes"
                width: parent.width
                metadata: root.namedScenesMetadata
                commandAction: namedScenesAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteMovieTimeline"
                width: parent.width
                metadata: root.movieMetadata
                commandAction: movieTimelineAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteSystemInformation"
                width: parent.width
                metadata: root.systemInformationMetadata
                commandAction: systemInformationAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteObjectPanel"
                width: parent.width
                metadata: root.objectPanelMetadata
                commandAction: objectPanelAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteObjectChemistry"
                width: parent.width
                metadata: root.objectChemistryMetadata
                commandAction: objectChemistryAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteSelectExpression"
                width: parent.width
                metadata: root.selectExpressionMetadata
                commandAction: selectExpressionAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteSelectAll"
                width: parent.width
                metadata: root.selectAllMetadata
                commandAction: selectAllAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteTrajectoryPlayback"
                width: parent.width
                metadata: root.trajectoryPlaybackMetadata
                commandAction: trajectoryPlaybackAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentShow"
                width: parent.width
                metadata: root.representShowMetadata
                commandAction: representShowAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentHide"
                width: parent.width
                metadata: root.representHideMetadata
                commandAction: representHideAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentAs"
                width: parent.width
                metadata: root.representAsMetadata
                commandAction: representAsAction
                query: commandPaletteQuery.text
            }
            CommandPaletteEntry {
                objectName: "commandPaletteRepresentToggle"
                width: parent.width
                metadata: root.representToggleMetadata
                commandAction: representToggleAction
                query: commandPaletteQuery.text
            }
            }
        }
    }

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
                else if (!dragged && mouse.button === Qt.RightButton)
                    viewportContextMenu.popup(mouse.x, mouse.y)
            }
            onDoubleClicked: viewport.resetViewAnimated(0.35, 1)
        }
    }

    Menu {
        id: viewportContextMenu
        objectName: "viewportContextMenu"

        MenuItem {
            objectName: "selectAllContextMenuItem"
            action: selectAllAction
        }
        MenuItem {
            objectName: "trajectoryPlaybackContextMenuItem"
            action: trajectoryPlaybackAction
        }
        MenuSeparator {}
        MenuItem {
            objectName: "representShowContextMenuItem"
            action: representShowAction
        }
        MenuItem {
            objectName: "representHideContextMenuItem"
            action: representHideAction
        }
        MenuItem {
            objectName: "representAsContextMenuItem"
            action: representAsAction
        }
        MenuItem {
            objectName: "representToggleContextMenuItem"
            action: representToggleAction
        }
    }

    Repeater {
        model: viewport.analysisLabelItems
        Text {
            required property var modelData
            x: modelData.x + 8
            y: modelData.y - height - 6
            text: modelData.text
            color: modelData.color
            font.pixelSize: 13
            font.bold: true
            style: Text.Outline
            styleColor: "#cc050812"
            z: 20
        }
    }

    FileDialog {
        id: structureDialog
        title: qsTr("Open molecular structure or scalar volume")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Molecular data (*.pdb *.ent *.cif *.mmcif *.bcif *.pqr *.mol *.mol2 *.psf *.prmtop *.parm7 *.top *.sdf *.sd *.gro *.g96 *.vtf *.xyz *.dx *.mrc *.map *.ccp4 *.mrcs)"),
                      qsTr("OpenDX scalar volumes (*.dx)"),
                      qsTr("MRC/CCP4 scalar volumes (*.mrc *.map *.ccp4 *.mrcs)"),
                      qsTr("All files (*)")]
        onAccepted: viewport.loadStructures(selectedFiles)
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save active molecular coordinates")
        fileMode: FileDialog.SaveFile
        defaultSuffix: selectedNameFilter.index === 1 ? "pdb"
                       : selectedNameFilter.index === 2 ? "cif"
                       : selectedNameFilter.index === 3 ? "gro"
                       : selectedNameFilter.index === 4 ? "g96"
                       : selectedNameFilter.index === 5 ? "pqr"
                       : selectedNameFilter.index === 6 ? "mol"
                       : selectedNameFilter.index === 7 ? "mol2"
                       : selectedNameFilter.index === 8 ? "psf"
                       : selectedNameFilter.index === 9 ? "sdf" : "xyz"
        nameFilters: [qsTr("XYZ coordinates (*.xyz)"),
                      qsTr("Protein Data Bank 3.3 (*.pdb *.ent)"),
                      qsTr("PDBx/mmCIF (*.cif *.mmcif)"),
                      qsTr("BinaryCIF (*.bcif)"),
                      qsTr("GROMACS structure/trajectory (*.gro)"),
                      qsTr("GROMOS-96 structure/trajectory (*.g96)"),
                      qsTr("PQR electrostatics (*.pqr)"),
                      qsTr("MDL MOL V2000 (*.mol)"),
                      qsTr("Tripos MOL2 (*.mol2)"),
                      qsTr("CHARMM/NAMD PSF topology (*.psf)"),
                      qsTr("SDF V2000 record (*.sdf *.sd)")]
        onAccepted: viewport.saveStructure(selectedFile, false)
    }

    FileDialog {
        id: trajectoryDialog
        title: qsTr("Attach molecular dynamics trajectory")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("MD trajectories/restarts (*.dcd *.xtc *.trr *.mdcrd *.crd *.nc *.ncdf *.netcdf *.h5md *.rst7 *.restrt *.inpcrd *.inprst *.lammpstrj *.lammpstraj *.dump *.binpos)"),
                      qsTr("All files (*)")]
        onAccepted: viewport.loadTrajectory(selectedFile,
                                              root.trajectoryCoordinateUnit,
                                              root.trajectoryMapping,
                                              root.trajectoryAtomMap)
    }

    FileDialog {
        id: sessionOpenDialog
        title: qsTr("Open MolShredder session")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("MolShredder sessions (*.msess)"),
                      qsTr("All files (*)")]
        onAccepted: {
            if (viewport.loadSession(selectedFile)) {
                root.currentSessionUrl = selectedFile
                root.restoreSessionVisiblePanels()
            }
        }
    }

    FileDialog {
        id: sessionSaveDialog
        title: qsTr("Save MolShredder session")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "msess"
        nameFilters: [qsTr("MolShredder sessions (*.msess)")]
        onAccepted: {
            if (viewport.saveSession(selectedFile,
                                     root.sessionVisiblePanels()))
                root.currentSessionUrl = selectedFile
        }
    }

    Timer {
        id: sessionAutosaveTimer
        interval: 120000
        repeat: true
        running: root.currentSessionUrl.toString().length > 0 && root.hasWorkspace
        onTriggered: viewport.autosaveSession(
                         root.currentSessionUrl.toString() + ".autosave",
                         root.currentSessionUrl.toString() + ".autosave.previous",
                         root.sessionVisiblePanels())
    }

    Timer {
        id: moviePlaybackTimer
        interval: viewport.movieState.configured && viewport.movieState.fps > 0
                  ? Math.max(4, Math.round(1000 / viewport.movieState.fps))
                  : 33
        repeat: true
        running: viewport.movieState.configured && viewport.movieState.playing
        onTriggered: viewport.stepMovie(1)
    }

    FileDialog {
        id: scriptDialog
        title: qsTr("Run a local Python script")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Python scripts (*.py)")]
        onAccepted: {
            root.pendingScriptUrl = selectedFile
            scriptTrustOverlay.visible = true
        }
    }


    FileDialog {
        id: analysisExportDialog
        title: qsTr("Export persistent analysis result")
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.analysisExportFormat
        nameFilters: root.analysisExportFormat === "csv"
                     ? [qsTr("CSV table (*.csv)")] : [qsTr("JSON result (*.json)")]
        onAccepted: viewport.exportAnalysisResult(
                        root.analysisExportResultId, selectedFile,
                        root.analysisExportFormat)
    }

    component ToolbarButton: Rectangle {
        id: toolbarButtonRoot
        required property string label
        required property var action
        property string actionId: ""
        property string toolTip: ""
        property bool selected: false
        width: buttonText.implicitWidth + 22
        height: 34
        radius: 7
        opacity: enabled ? 1.0 : 0.55
        color: selected ? "#285f99" : mouse.containsMouse && enabled ? "#24354c" : "#172235"
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
            enabled: toolbarButtonRoot.enabled
            onClicked: {
                if (typeof toolbarButtonRoot.action === "function")
                    toolbarButtonRoot.action()
                else
                    toolbarButtonRoot.action.trigger()
            }
        }

        ToolTip.visible: mouse.containsMouse && toolbarButtonRoot.toolTip.length > 0
        ToolTip.text: toolbarButtonRoot.toolTip
    }

    component CommandPaletteEntry: Rectangle {
        id: paletteEntryRoot
        required property var metadata
        required property var commandAction
        required property string query
        readonly property string actionId: metadata.id
        readonly property string translatedStatus: {
            localization.currentLanguage
            const source = commandAction.enabled || metadata.unavailable.length === 0
                         ? metadata.status : metadata.unavailable
            return localization.translateUi(source)
        }
        height: visible ? 56 : 0
        radius: 7
        visible: query.length === 0 ||
                 commandAction.text.toLowerCase().indexOf(query.toLowerCase()) >= 0 ||
                 actionId.indexOf(query.toLowerCase()) >= 0 ||
                 metadata.keywords.toLowerCase().indexOf(query.toLowerCase()) >= 0
        opacity: commandAction.enabled ? 1.0 : 0.58
        color: paletteEntryMouse.containsMouse && commandAction.enabled
               ? "#284767" : "#172235"
        border.color: "#405270"

        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 12
            anchors.topMargin: 8
            text: paletteEntryRoot.commandAction.text
            color: "#eef6ff"
            font.pixelSize: 14
            font.bold: true
        }

        Text {
            anchors.left: parent.left
            anchors.right: shortcutText.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            anchors.bottomMargin: 7
            text: paletteEntryRoot.translatedStatus
            color: "#91a8c2"
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Text {
            id: shortcutText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 12
            text: paletteEntryRoot.commandAction.shortcut || ""
            color: "#91a8c2"
            font.pixelSize: 12
        }

        MouseArea {
            id: paletteEntryMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: paletteEntryRoot.commandAction.enabled
            onClicked: {
                commandPaletteOverlay.visible = false
                paletteEntryRoot.commandAction.trigger()
            }
        }
    }

    component InfoRow: Item {
        required property string label
        required property string value
        width: 548
        height: 27

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 155
            text: parent.label
            color: "#91a8c2"
            font.pixelSize: 13
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 165
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: parent.value
            color: "#e5f1ff"
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 20
        anchors.topMargin: 58
        width: toolbar.width + 28
        height: toolbar.height + 22
        radius: 8
        color: "#cc101827"
        border.color: "#405270"

        Row {
            id: toolbar
            objectName: "compactToolbar"
            readonly property var actionIds: [
                root.fileOpenMetadata.id,
                root.trajectoryAttachMetadata.id,
                root.fileSaveMetadata.id,
                root.sceneViewsMetadata.id,
                root.analyzePanelMetadata.id,
                root.representLinesMetadata.id,
                root.representSticksMetadata.id,
                root.representSpheresMetadata.id,
                root.representCartoonMetadata.id
            ]
            readonly property string actionIdSequence: actionIds.join(",")
            readonly property int projectedActionCount: children.length
            anchors.centerIn: parent
            spacing: 8

            ToolbarButton {
                objectName: "fileOpenToolbarButton"
                actionId: root.fileOpenMetadata.id
                label: fileOpenAction.text
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(root.fileOpenMetadata.status)
                }
                action: fileOpenAction
            }
            ToolbarButton {
                objectName: "trajectoryAttachToolbarButton"
                actionId: root.trajectoryAttachMetadata.id
                label: trajectoryAttachAction.text
                enabled: trajectoryAttachAction.enabled
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(
                        enabled ? root.trajectoryAttachMetadata.status
                                : root.trajectoryAttachMetadata.unavailable)
                }
                action: trajectoryAttachAction
            }
            ToolbarButton {
                objectName: "fileSaveToolbarButton"
                actionId: root.fileSaveMetadata.id
                label: fileSaveAction.text
                enabled: fileSaveAction.enabled
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(
                        enabled ? root.fileSaveMetadata.status
                                : root.fileSaveMetadata.unavailable)
                }
                action: fileSaveAction
            }
            ToolbarButton {
                objectName: "sceneViewsToolbarButton"
                actionId: root.sceneViewsMetadata.id
                label: sceneViewsAction.text
                enabled: sceneViewsAction.enabled
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(
                        enabled ? root.sceneViewsMetadata.status
                                : root.sceneViewsMetadata.unavailable)
                }
                action: sceneViewsAction
            }
            ToolbarButton {
                objectName: "analyzePanelToolbarButton"
                actionId: root.analyzePanelMetadata.id
                label: analyzePanelAction.text
                enabled: analyzePanelAction.enabled
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(
                        enabled ? root.analyzePanelMetadata.status
                                : root.analyzePanelMetadata.unavailable)
                }
                action: analyzePanelAction
            }
            ToolbarButton {
                objectName: "representLinesToolbarButton"
                actionId: root.representLinesMetadata.id
                label: representLinesAction.text
                enabled: representLinesAction.enabled
                selected: representLinesAction.checked
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(
                        enabled ? root.representLinesMetadata.status
                                : root.representLinesMetadata.unavailable)
                }
                action: representLinesAction
            }
            ToolbarButton {
                objectName: "representSticksToolbarButton"
                actionId: root.representSticksMetadata.id
                label: representSticksAction.text
                enabled: representSticksAction.enabled
                selected: representSticksAction.checked
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(
                        enabled ? root.representSticksMetadata.status
                                : root.representSticksMetadata.unavailable)
                }
                action: representSticksAction
            }
            ToolbarButton {
                objectName: "representSpheresToolbarButton"
                actionId: root.representSpheresMetadata.id
                label: representSpheresAction.text
                enabled: representSpheresAction.enabled
                selected: representSpheresAction.checked
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(
                        enabled ? root.representSpheresMetadata.status
                                : root.representSpheresMetadata.unavailable)
                }
                action: representSpheresAction
            }
            ToolbarButton {
                objectName: "representCartoonToolbarButton"
                actionId: root.representCartoonMetadata.id
                label: representCartoonAction.text
                enabled: representCartoonAction.enabled
                selected: representCartoonAction.checked
                toolTip: {
                    localization.currentLanguage
                    return localization.translateUi(
                        enabled ? root.representCartoonMetadata.status
                                : root.representCartoonMetadata.unavailable)
                }
                action: representCartoonAction
            }
        }
    }

    Rectangle {
        id: scriptOutputPanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 20
        anchors.topMargin: 88
        width: Math.min(540, parent.width - 330)
        height: 150
        visible: viewport.scriptOutput.length > 0
        radius: 8
        color: "#ee101827"
        border.color: "#506889"
        z: 5

        Text {
            anchors.left: parent.left
            anchors.right: closeScriptOutput.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 12
            text: viewport.scriptOutput
            color: "#d8e8fa"
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 8
            elide: Text.ElideRight
        }

        Text {
            id: closeScriptOutput
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            text: "×"
            color: closeScriptMouse.containsMouse ? "#ffffff" : "#9fb3cc"
            font.pixelSize: 20

            MouseArea {
                id: closeScriptMouse
                anchors.fill: parent
                anchors.margins: -8
                hoverEnabled: true
                onClicked: viewport.clearScriptOutput()
            }
        }
    }

    Rectangle {
        id: trajectoryPanel
        objectName: "trajectoryPanel"
        property string actionId: root.trajectoryPlaybackMetadata.id
        property var entryAction: trajectoryPlaybackAction
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: Math.min(parent.width - 420, 610)
        height: 120
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
                objectName: "trajectoryPlaybackPanelButton"
                actionId: root.trajectoryPlaybackMetadata.id
                label: trajectoryPlaybackAction.text
                enabled: trajectoryPlaybackAction.enabled
                selected: trajectoryPlaybackAction.checked
                toolTip: localization.translateUi(
                    root.trajectoryPlaybackMetadata.status)
                action: trajectoryPlaybackAction
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
                label: viewport.playbackDirection === "forward" ? qsTr("Forward") : qsTr("Reverse")
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
            ToolbarButton {
                objectName: "trajectoryTaskCancelButton"
                visible: viewport.trajectoryTaskRunning
                label: qsTr("Cancel")
                action: function() { viewport.cancelTrajectoryTask() }
            }
        }

        Text {
            id: frameLabel
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 13
            anchors.topMargin: 14
            text: viewport.trajectoryTaskRunning ?
                  viewport.trajectoryTaskStage + " " +
                  Math.round(viewport.trajectoryTaskProgress * 100) + "%" :
                  qsTr("Frame %1 / %2").arg(viewport.trajectoryFrame).arg(
                      Math.max(0, viewport.trajectoryFrameCount - 1))
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
                objectName: "trajectoryTaskProgress"
                visible: viewport.trajectoryTaskRunning
                anchors.left: parent.left
                anchors.top: parent.top
                height: 3
                width: parent.width * viewport.trajectoryTaskProgress
                radius: 2
                color: "#7ad7ff"
                z: 3
            }

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
        id: surfacePanel
        objectName: "surfacePanel"
        property string actionId: root.representSurfaceMetadata.id
        property var entryAction: representSurfaceAction
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: 700
        height: 154
        visible: false
        radius: 9
        color: "#ee101827"
        border.color: "#506889"
        z: 4

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Row {
                spacing: 8
                Text {
                    width: 128
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Molecular Surface")
                    color: "#eef6ff"
                    font.pixelSize: 15
                    font.bold: true
                }
                ComboBox {
                    id: surfaceKindInput
                    objectName: "surfaceKindInput"
                    width: 180
                    model: [qsTr("Solvent accessible"), qsTr("van der Waals")]
                }
                TextField {
                    id: surfaceSelectionInput
                    objectName: "surfaceSelectionInput"
                    width: 180
                    text: "all"
                    placeholderText: qsTr("Selection")
                }
            }

            Row {
                spacing: 8
                TextField {
                    id: surfaceProbeInput
                    objectName: "surfaceProbeInput"
                    width: 150
                    text: surfaceKindInput.currentIndex === 0 ? "1.4" : "0"
                    enabled: surfaceKindInput.currentIndex === 0
                    placeholderText: qsTr("Probe radius (Å)")
                    validator: DoubleValidator { bottom: 0.0 }
                }
                TextField {
                    id: surfaceSpacingInput
                    objectName: "surfaceSpacingInput"
                    width: 150
                    text: "0.7"
                    placeholderText: qsTr("Grid spacing (Å)")
                    validator: DoubleValidator { bottom: 0.01 }
                }
                TextField {
                    id: surfaceVoxelBudgetInput
                    objectName: "surfaceVoxelBudgetInput"
                    width: 150
                    text: "8388608"
                    placeholderText: qsTr("Voxel budget")
                    validator: IntValidator { bottom: 1 }
                }
                TextField {
                    id: surfaceMemoryBudgetInput
                    objectName: "surfaceMemoryBudgetInput"
                    width: 180
                    text: "536870912"
                    placeholderText: qsTr("Memory budget (bytes)")
                    validator: DoubleValidator { bottom: 1.0 }
                }
            }

            Row {
                spacing: 8
                Button {
                    objectName: "surfaceApplyButton"
                    text: qsTr("Apply")
                    onClicked: {
                        const kind = surfaceKindInput.currentIndex === 0 ? "sas" : "vdw"
                        const probe = kind === "sas" ? Number(surfaceProbeInput.text) : 0
                        if (viewport.setMolecularSurface(
                                kind, surfaceSelectionInput.text, probe,
                                Number(surfaceSpacingInput.text),
                                Number(surfaceVoxelBudgetInput.text),
                                Number(surfaceMemoryBudgetInput.text)))
                            surfacePanel.visible = false
                    }
                }
                Button {
                    objectName: "surfaceHideButton"
                    text: qsTr("Hide")
                    enabled: viewport.representation === "surface"
                    onClicked: {
                        viewport.hideMolecularSurface()
                        surfacePanel.visible = false
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Coarser spacing reduces memory and computation.")
                    color: "#9fb5cf"
                    font.pixelSize: 12
                }
            }
        }
    }

    Rectangle {
        id: volumePanel
        objectName: "volumePanel"
        property string actionId: root.volumeSliceMetadata.id
        property var entryAction: volumeSliceAction
        property string rampPreset: "density"
        property real samplingStep: 0.5
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: 540
        height: 154
        visible: viewport.hasVolume && !viewport.hasTrajectory
        radius: 9
        color: "#e6101827"
        border.color: "#506889"
        z: 3

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Row {
                spacing: 8

                Text {
                    width: 210
                    anchors.verticalCenter: parent.verticalCenter
                    text: viewport.volumeTaskRunning ?
                              qsTr("Preparing direct volume %1%").arg(
                                  Math.round(viewport.volumeTaskProgress * 100)) :
                          viewport.volumeMode === "direct" ?
                              qsTr("Direct Volume") :
                          viewport.volumeMode === "slice" ?
                              qsTr("Slice %1 %2").arg(
                                  viewport.volumeSliceAxis.toUpperCase()).arg(
                                  viewport.volumeSliceIndex) :
                              qsTr("Contour %1").arg(
                                  Number(viewport.volumeLevel).toPrecision(5))
                    color: "#d8e8fa"
                    font.pixelSize: 14
                }
                ToolbarButton {
                    label: qsTr("Surface")
                    selected: viewport.volumeMode === "isosurface"
                    action: function() {
                        viewport.setVolumeIsosurface(viewport.volumeLevel)
                    }
                }
                ToolbarButton {
                    label: qsTr("Direct")
                    selected: viewport.volumeMode === "direct"
                    action: function() {
                        viewport.setDirectVolume(volumePanel.rampPreset,
                                                 volumePanel.samplingStep,
                                                 4096, 256, 536870912)
                    }
                }
                Repeater {
                    model: ["x", "y", "z"]
                    delegate: ToolbarButton {
                        required property string modelData
                        label: modelData.toUpperCase()
                        selected: viewport.volumeMode === "slice" &&
                                  viewport.volumeSliceAxis === modelData
                        action: function() {
                            viewport.setVolumeSlice(modelData,
                                Math.floor(viewport.volumeSliceMaximum / 2))
                        }
                    }
                }
            }

            Row {
                spacing: 8

                ToolbarButton {
                    label: "−"
                    action: function() {
                        if (viewport.volumeMode === "slice") {
                            viewport.setVolumeSlice(viewport.volumeSliceAxis,
                                Math.max(0, viewport.volumeSliceIndex - 1))
                        } else if (viewport.volumeMode === "direct") {
                            volumePanel.samplingStep = Math.max(
                                0.1, volumePanel.samplingStep - 0.1)
                            viewport.setDirectVolume(volumePanel.rampPreset,
                                volumePanel.samplingStep, 4096, 256, 536870912)
                        } else {
                            const step = (viewport.volumeMaximum - viewport.volumeMinimum) / 20
                            viewport.setVolumeIsosurface(
                                Math.max(viewport.volumeMinimum,
                                         viewport.volumeLevel - step))
                        }
                    }
                }
                ToolbarButton {
                    label: qsTr("Midpoint")
                    action: function() {
                        if (viewport.volumeMode === "slice") {
                            viewport.setVolumeSlice(viewport.volumeSliceAxis,
                                Math.floor(viewport.volumeSliceMaximum / 2))
                        } else if (viewport.volumeMode === "direct") {
                            volumePanel.samplingStep = 0.5
                            viewport.setDirectVolume(volumePanel.rampPreset,
                                volumePanel.samplingStep, 4096, 256, 536870912)
                        } else {
                            viewport.setVolumeIsosurface(
                                (viewport.volumeMinimum + viewport.volumeMaximum) / 2)
                        }
                    }
                }
                ToolbarButton {
                    label: "+"
                    action: function() {
                        if (viewport.volumeMode === "slice") {
                            viewport.setVolumeSlice(viewport.volumeSliceAxis,
                                Math.min(viewport.volumeSliceMaximum,
                                         viewport.volumeSliceIndex + 1))
                        } else if (viewport.volumeMode === "direct") {
                            volumePanel.samplingStep = Math.min(
                                2.0, volumePanel.samplingStep + 0.1)
                            viewport.setDirectVolume(volumePanel.rampPreset,
                                volumePanel.samplingStep, 4096, 256, 536870912)
                        } else {
                            const step = (viewport.volumeMaximum - viewport.volumeMinimum) / 20
                            viewport.setVolumeIsosurface(
                                Math.min(viewport.volumeMaximum,
                                         viewport.volumeLevel + step))
                        }
                    }
                }
            }
            Row {
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Transfer")
                    color: "#d8e8fa"
                    font.pixelSize: 13
                }
                ComboBox {
                    id: volumeRampInput
                    objectName: "volumeRampInput"
                    width: 150
                    model: ["density", "fire", "grayscale", "ice", "spectrum"]
                    onActivated: {
                        volumePanel.rampPreset = currentText
                        if (viewport.volumeMode === "direct")
                            viewport.setDirectVolume(volumePanel.rampPreset,
                                volumePanel.samplingStep, 4096, 256, 536870912)
                    }
                }
                Text {
                    objectName: "directVolumeGpuState"
                    visible: viewport.volumeMode === "direct"
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("GPU %1").arg(viewport.volumeGpuState)
                    color: viewport.volumeGpuState === "ready" ? "#77dda0" :
                           viewport.volumeGpuState === "failed" ||
                           viewport.volumeGpuState === "unavailable" ?
                               "#ff9b9b" : "#9fb5cf"
                    font.pixelSize: 12
                    ToolTip.visible: gpuStateMouse.containsMouse
                    ToolTip.text: viewport.volumeGpuMessage
                    MouseArea {
                        id: gpuStateMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Step %1").arg(volumePanel.samplingStep.toFixed(1))
                    color: "#9fb5cf"
                    font.pixelSize: 12
                }
                ToolbarButton {
                    objectName: "directVolumeTaskCancelButton"
                    visible: viewport.volumeTaskRunning
                    label: qsTr("Cancel")
                    action: function() {
                        viewport.cancelDirectVolumeTask()
                    }
                }
            }
        }
    }

    Rectangle {
        id: objectPanel
        objectName: "objectPanel"
        property string actionId: root.objectPanelMetadata.id
        property var entryAction: objectPanelAction
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 20
        anchors.topMargin: 90
        width: 430
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
            text: qsTr("Objects")
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
                        required property int index
                        property bool editingName: false
                        property bool deleteArmed: false
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
                            id: objectName
                            anchors.left: visibilityToggle.right
                            anchors.right: renameButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 9
                            anchors.rightMargin: 5
                            text: parent.modelData.name + " · " +
                                  qsTr("%1 atoms").arg(parent.modelData.atoms)
                            visible: !parent.editingName
                            color: parent.modelData.visible ? "#e5f1ff" : "#8291a6"
                            elide: Text.ElideRight
                            font.pixelSize: 13
                        }

                        Rectangle {
                            id: objectNameEditorContainer
                            objectName: "objectNameEditor"
                            anchors.left: visibilityToggle.right
                            anchors.right: renameButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 6
                            anchors.rightMargin: 4
                            height: 30
                            visible: parent.editingName
                            radius: 5
                            color: "#101827"
                            border.color: "#69aef0"
                            z: 3
                            TextInput {
                                id: objectNameEditor
                                anchors.fill: parent
                                anchors.margins: 6
                                text: objectNameEditorContainer.parent.modelData.name
                                color: "#eef6ff"
                                selectByMouse: true
                                onAccepted: {
                                    if (viewport.renameObject(objectNameEditorContainer.parent.modelData.id, text))
                                        objectNameEditorContainer.parent.editingName = false
                                }
                            }
                        }

                        Rectangle {
                            id: renameButton
                            objectName: "objectRenameButton"
                            anchors.right: upButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34
                            height: 28
                            radius: 5
                            color: "#28445f"
                            border.color: "#58799a"
                            z: 3
                            Text { anchors.centerIn: parent; text: "✎"; color: "#eef6ff" }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    renameButton.parent.editingName = true
                                    objectNameEditor.forceActiveFocus()
                                    objectNameEditor.selectAll()
                                }
                            }
                        }

                        Rectangle {
                            id: upButton
                            objectName: "objectMoveUpButton"
                            anchors.right: downButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 28
                            enabled: parent.index > 0
                            opacity: enabled ? 1.0 : 0.35
                            radius: 5
                            color: "#28445f"
                            border.color: "#58799a"
                            z: 3
                            Text { anchors.centerIn: parent; text: "↑"; color: "#eef6ff" }
                            MouseArea {
                                anchors.fill: parent
                                enabled: upButton.enabled
                                onClicked: viewport.reorderObject(upButton.parent.modelData.id,
                                                                  upButton.parent.index)
                            }
                        }

                        Rectangle {
                            id: downButton
                            objectName: "objectMoveDownButton"
                            anchors.right: deleteButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 28
                            enabled: parent.index + 1 < viewport.objectItems.length
                            opacity: enabled ? 1.0 : 0.35
                            radius: 5
                            color: "#28445f"
                            border.color: "#58799a"
                            z: 3
                            Text { anchors.centerIn: parent; text: "↓"; color: "#eef6ff" }
                            MouseArea {
                                anchors.fill: parent
                                enabled: downButton.enabled
                                onClicked: viewport.reorderObject(downButton.parent.modelData.id,
                                                                  downButton.parent.index + 2)
                            }
                        }

                        Rectangle {
                            id: deleteButton
                            objectName: "objectDeleteButton"
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 28
                            radius: 5
                            color: parent.deleteArmed ? "#a13e4f" : "#63313b"
                            border.color: "#a85b6a"
                            z: 3
                            Text { anchors.centerIn: parent; text: deleteButton.parent.deleteArmed ? "?" : "×"; color: "#fff0f2" }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (deleteButton.parent.deleteArmed)
                                        viewport.deleteObject(deleteButton.parent.modelData.id)
                                    else {
                                        deleteButton.parent.deleteArmed = true
                                        deleteReset.restart()
                                    }
                                }
                            }
                            Timer {
                                id: deleteReset
                                interval: 3000
                                onTriggered: deleteButton.parent.deleteArmed = false
                            }
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
        anchors.bottomMargin: viewport.hasTrajectory ? 132 : viewport.hasVolume ? 108 : 20
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
        anchors.bottomMargin: viewport.hasTrajectory ? 132 : viewport.hasVolume ? 108 : 20
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

    Rectangle {
        id: selectionExpressionOverlay
        objectName: "selectionExpressionOverlay"
        property string actionId: root.selectExpressionMetadata.id
        property var entryAction: selectExpressionAction
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 75

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: 620
            height: 330
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                Row {
                    width: parent.width
                    spacing: 12
                    Text {
                        width: 500
                        text: qsTr("Select by Expression")
                        color: "#eef6ff"
                        font.pixelSize: 20
                        font.bold: true
                    }
                    ToolbarButton {
                        label: qsTr("Close")
                        action: function() { selectionExpressionOverlay.visible = false }
                    }
                }

                Text {
                    width: parent.width
                    text: qsTr("Create a reusable named selection. Dynamic selections are reevaluated when the current trajectory frame changes.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Text { text: qsTr("Name"); color: "#91a8c2"; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 36; radius: 6
                    color: "#172235"; border.color: "#506889"
                    TextInput {
                        id: selectionNameInput
                        objectName: "selectionNameInput"
                        anchors.fill: parent; anchors.margins: 9
                        text: "selection1"
                        color: "#eef6ff"; selectByMouse: true; clip: true
                    }
                }

                Text { text: qsTr("Expression"); color: "#91a8c2"; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 36; radius: 6
                    color: "#172235"; border.color: "#506889"
                    TextInput {
                        id: selectionExpressionInput
                        objectName: "selectionExpressionInput"
                        anchors.fill: parent; anchors.margins: 9
                        text: "polymer.protein and b < 30"
                        color: "#eef6ff"; selectByMouse: true; clip: true
                        onAccepted: root.applySelectionEditor()
                    }
                }

                Row {
                    spacing: 12
                    CheckBox {
                        id: selectionDynamicCheck
                        objectName: "selectionDynamicCheck"
                        text: qsTr("Update with trajectory frame")
                        checked: true
                    }
                    Button {
                        id: selectionApplyButton
                        objectName: "selectionApplyButton"
                        text: qsTr("Create selection")
                        enabled: selectionNameInput.text.trim().length > 0 &&
                                 selectionExpressionInput.text.trim().length > 0
                        onClicked: root.applySelectionEditor()
                    }
                }
            }
        }
    }

    Rectangle {
        id: analysisOverlay
        objectName: "analysisOverlay"
        property string actionId: root.analyzePanelMetadata.id
        property var entryAction: analyzePanelAction
        property int modeIndex: 0
        property string pbcPolicy: "raw"
        property var plotData: null
        property var modes: [qsTr("Centroid"), qsTr("Center of mass"),
                             qsTr("Distance"), qsTr("Angle"),
                             qsTr("Dihedral"), qsTr("SASA"), qsTr("RDF"), qsTr("Contacts"),
                             qsTr("Trajectory RMSD"), qsTr("RMSD Matrix")]
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 75

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: 760
            height: 650
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 11

                Row {
                    width: parent.width
                    spacing: 12
                    Text {
                        width: 620
                        text: qsTr("Analyze · persistent results")
                        color: "#eef6ff"
                        font.pixelSize: 20
                        font.bold: true
                    }
                    ToolbarButton {
                        label: qsTr("Close")
                        action: function() { analysisOverlay.visible = false }
                    }
                }

                Text {
                    width: parent.width
                    text: qsTr("Computations, CLI commands and Python calls share one canonical operation. Geometry endpoints must each select exactly one atom.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Row {
                    spacing: 9
                    ToolbarButton {
                        label: analysisOverlay.modes[analysisOverlay.modeIndex]
                        selected: true
                        action: function() {
                            analysisOverlay.modeIndex =
                                (analysisOverlay.modeIndex + 1) % analysisOverlay.modes.length
                        }
                    }
                    Rectangle {
                        width: 180; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisPrimaryInput
                            anchors.fill: parent; anchors.margins: 9
                            text: analysisOverlay.modeIndex >= 2 && analysisOverlay.modeIndex <= 4
                                  ? qsTr("index 1") : qsTr("all")
                            color: "#eef6ff"; selectByMouse: true; clip: true
                        }
                    }
                    Rectangle {
                        width: 160; height: 36; radius: 6
                        visible: (analysisOverlay.modeIndex >= 2 &&
                                  analysisOverlay.modeIndex <= 4) ||
                                 analysisOverlay.modeIndex === 6 ||
                                 analysisOverlay.modeIndex === 7
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisSecondaryInput
                            anchors.fill: parent; anchors.margins: 9
                            text: qsTr("index 2")
                            color: "#eef6ff"; selectByMouse: true; clip: true
                        }
                    }
                    Rectangle {
                        width: 130; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisResultNameInput
                            anchors.fill: parent; anchors.margins: 9
                            text: ""
                            color: "#eef6ff"; selectByMouse: true; clip: true
                            Text { anchors.fill: parent; text: qsTr("optional name");
                                   visible: parent.text.length === 0;
                                   color: "#71849c"; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                Row {
                    spacing: 9
                    visible: analysisOverlay.modeIndex === 5
                    Text {
                        text: qsTr("Samples per atom")
                        color: "#91a8c2"; font.pixelSize: 12; height: 36
                        verticalAlignment: Text.AlignVCenter
                    }
                    Rectangle {
                        width: 100; height: 34; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisSasaSamplesInput
                            anchors.fill: parent; anchors.margins: 8
                            text: "960"; color: "#eef6ff"
                            validator: IntValidator { bottom: 4 }
                        }
                    }
                    Text {
                        text: qsTr("Evaluation budget")
                        color: "#91a8c2"; font.pixelSize: 12; height: 36
                        verticalAlignment: Text.AlignVCenter
                    }
                    Rectangle {
                        width: 150; height: 34; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisSasaBudgetInput
                            anchors.fill: parent; anchors.margins: 8
                            text: "100000000"; color: "#eef6ff"
                            validator: IntValidator { bottom: 1 }
                        }
                    }
                }

                Row {
                    spacing: 9
                    visible: analysisOverlay.modeIndex === 6
                    Text { text: qsTr("Bin width"); color: "#91a8c2"; height: 36; verticalAlignment: Text.AlignVCenter }
                    Rectangle {
                        width: 90; height: 34; radius: 6; color: "#172235"; border.color: "#506889"
                        TextInput { id: analysisRdfBinWidthInput; anchors.fill: parent; anchors.margins: 8; text: "0.1"; color: "#eef6ff"; validator: DoubleValidator { bottom: 0 } }
                    }
                    Text { text: qsTr("Normalization"); color: "#91a8c2"; height: 36; verticalAlignment: Text.AlignVCenter }
                    ToolbarButton {
                        id: analysisRdfNormalizationButton
                        property string value: "count"
                        label: value === "count" ? qsTr("Pair count") : qsTr("g(r)")
                        selected: value === "g-r"
                        action: function() { value = value === "count" ? "g-r" : "count" }
                    }
                    Text { text: qsTr("Evaluation budget"); color: "#91a8c2"; height: 36; verticalAlignment: Text.AlignVCenter }
                    Rectangle {
                        width: 140; height: 34; radius: 6; color: "#172235"; border.color: "#506889"
                        TextInput { id: analysisRdfBudgetInput; anchors.fill: parent; anchors.margins: 8; text: "100000000"; color: "#eef6ff"; validator: IntValidator { bottom: 1 } }
                    }
                }

                Row {
                    spacing: 9
                    visible: analysisOverlay.modeIndex === 9
                    Text { text: qsTr("Frame-pair budget"); color: "#91a8c2"; height: 36; verticalAlignment: Text.AlignVCenter }
                    Rectangle {
                        width: 150; height: 34; radius: 6; color: "#172235"; border.color: "#506889"
                        TextInput { id: analysisMatrixBudgetInput; anchors.fill: parent; anchors.margins: 8; text: "1000000"; color: "#eef6ff"; validator: IntValidator { bottom: 1 } }
                    }
                    Text { text: qsTr("All selected trajectory frames are compared without mutating coordinates."); color: "#91a8c2"; height: 36; verticalAlignment: Text.AlignVCenter }
                }

                Row {
                    spacing: 9
                    visible: (analysisOverlay.modeIndex >= 2 &&
                              analysisOverlay.modeIndex <= 4) ||
                             analysisOverlay.modeIndex === 6 ||
                             analysisOverlay.modeIndex === 7
                    Rectangle {
                        width: 160; height: 36; radius: 6
                        visible: analysisOverlay.modeIndex === 3 ||
                                 analysisOverlay.modeIndex === 4
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisThirdInput
                            anchors.fill: parent; anchors.margins: 9
                            text: qsTr("index 3")
                            color: "#eef6ff"; selectByMouse: true; clip: true
                        }
                    }
                    Rectangle {
                        width: 160; height: 36; radius: 6
                        visible: analysisOverlay.modeIndex === 4
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisFourthInput
                            anchors.fill: parent; anchors.margins: 9
                            text: qsTr("index 4")
                            color: "#eef6ff"; selectByMouse: true; clip: true
                        }
                    }
                    Text {
                        text: qsTr("Periodic boundary")
                        color: "#91a8c2"; font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                        height: 36
                    }
                    ToolbarButton {
                        label: analysisOverlay.pbcPolicy === "raw"
                               ? qsTr("Raw coordinates")
                               : qsTr("Minimum image")
                        selected: analysisOverlay.pbcPolicy === "minimum-image"
                        action: function() {
                            analysisOverlay.pbcPolicy =
                                analysisOverlay.pbcPolicy === "raw"
                                ? "minimum-image" : "raw"
                        }
                    }
                }

                Row {
                    spacing: 9
                    Text {
                        width: 265
                        text: analysisOverlay.modeIndex === 0 || analysisOverlay.modeIndex === 1
                              ? qsTr("Selection")
                              : analysisOverlay.modeIndex === 2
                                ? qsTr("From / To selections")
                                : analysisOverlay.modeIndex === 3
                                  ? qsTr("First / vertex / third selections")
                                  : analysisOverlay.modeIndex === 4
                                    ? qsTr("Four ordered atom selections")
                                    : analysisOverlay.modeIndex === 5
                                      ? qsTr("Selection / probe radius")
                                    : analysisOverlay.modeIndex === 6 || analysisOverlay.modeIndex === 7
                                      ? qsTr("First / optional second selection")
                                    : analysisOverlay.modeIndex === 9
                                      ? qsTr("Selection / all trajectory frames")
                                      : qsTr("Selection / reference frame")
                        color: "#91a8c2"; font.pixelSize: 12
                    }
                    Rectangle {
                        width: 85; height: 34; radius: 6
                        visible: analysisOverlay.modeIndex >= 5 && analysisOverlay.modeIndex <= 8
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: analysisCutoffInput
                            anchors.fill: parent; anchors.margins: 8
                            text: analysisOverlay.modeIndex === 8 ? "0"
                                  : analysisOverlay.modeIndex === 5 ? "1.4"
                                  : analysisOverlay.modeIndex === 6 ? "10.0" : "4.0"
                            color: "#eef6ff"; validator: DoubleValidator { bottom: 0 }
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Compute and store")
                        enabled: !viewport.analysisTaskRunning
                        action: function() {
                            if (analysisOverlay.modeIndex === 0)
                                viewport.analyzeCenter(analysisPrimaryInput.text, "centroid", analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 1)
                                viewport.analyzeCenter(analysisPrimaryInput.text, "com", analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 2)
                                viewport.analyzeDistance(analysisPrimaryInput.text, analysisSecondaryInput.text, analysisOverlay.pbcPolicy, analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 3)
                                viewport.analyzeAngle(analysisPrimaryInput.text, analysisSecondaryInput.text, analysisThirdInput.text, analysisOverlay.pbcPolicy, analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 4)
                                viewport.analyzeDihedral(analysisPrimaryInput.text, analysisSecondaryInput.text, analysisThirdInput.text, analysisFourthInput.text, analysisOverlay.pbcPolicy, analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 5)
                                viewport.analyzeSasa(analysisPrimaryInput.text, Number(analysisCutoffInput.text), Number(analysisSasaSamplesInput.text), Number(analysisSasaBudgetInput.text), analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 6)
                                viewport.analyzeRdf(analysisPrimaryInput.text, analysisSecondaryInput.text, Number(analysisCutoffInput.text), Number(analysisRdfBinWidthInput.text), analysisRdfNormalizationButton.value, analysisOverlay.pbcPolicy, Number(analysisRdfBudgetInput.text), analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 7)
                                viewport.analyzeContacts(analysisPrimaryInput.text, analysisSecondaryInput.text, Number(analysisCutoffInput.text), analysisOverlay.pbcPolicy, analysisResultNameInput.text)
                            else if (analysisOverlay.modeIndex === 8)
                                viewport.analyzeTrajectoryRmsd(analysisPrimaryInput.text, Math.max(0, Number(analysisCutoffInput.text)), analysisResultNameInput.text)
                            else
                                viewport.analyzeTrajectoryRmsdMatrix(analysisPrimaryInput.text, Number(analysisMatrixBudgetInput.text), analysisResultNameInput.text)
                        }
                    }
                    ToolbarButton {
                        objectName: "analysisTaskCancelButton"
                        visible: viewport.analysisTaskRunning
                        label: qsTr("Cancel")
                        action: function() { viewport.cancelAnalysisTask() }
                    }
                    Text {
                        objectName: "analysisTaskProgress"
                        visible: viewport.analysisTaskRunning
                        text: viewport.analysisTaskStage + " " +
                              Math.round(viewport.analysisTaskProgress * 100) + "%"
                        color: "#7ad7ff"; height: 36
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Text {
                    text: qsTr("Results (click a row for provenance)")
                    color: "#dceaff"; font.pixelSize: 14; font.bold: true
                }

                Flickable {
                    width: parent.width
                    height: 190
                    contentHeight: analysisResultColumn.height
                    clip: true

                    Column {
                        id: analysisResultColumn
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: viewport.analysisItems
                            Rectangle {
                                required property var modelData
                                width: analysisResultColumn.width
                                height: 42
                                radius: 6
                                color: "#172235"
                                border.color: modelData.sourceStatus === "current" ? "#405270" : "#b27843"

                                Text {
                                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 10; width: 330
                                    text: "#" + parent.modelData.id + " · " + parent.modelData.name +
                                          " · " + parent.modelData.kind + " · " + parent.modelData.sourceStatus
                                    color: "#eef6ff"; elide: Text.ElideRight
                                }
                                Row {
                                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                    anchors.rightMargin: 7; spacing: 6
                                    ToolbarButton {
                                        label: parent.parent.modelData.visible ? qsTr("Hide") : qsTr("Show")
                                        action: function() { viewport.setAnalysisResultVisible(
                                            parent.parent.modelData.id, !parent.parent.modelData.visible) }
                                    }
                                    ToolbarButton {
                                        label: "JSON"
                                        action: function() {
                                            root.analysisExportResultId = parent.parent.modelData.id
                                            root.analysisExportFormat = "json"
                                            analysisExportDialog.open()
                                        }
                                    }
                                    ToolbarButton {
                                        label: "CSV"
                                        action: function() {
                                            root.analysisExportResultId = parent.parent.modelData.id
                                            root.analysisExportFormat = "csv"
                                            analysisExportDialog.open()
                                        }
                                    }
                                    ToolbarButton {
                                        label: qsTr("Delete")
                                        action: function() { viewport.deleteAnalysisResult(parent.parent.modelData.id) }
                                    }
                                }
                                MouseArea {
                                    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                                    width: 340
                                    onClicked: {
                                        const detail = viewport.analysisResultJson(parent.modelData.id)
                                        analysisResultDetail.text = detail
                                        try {
                                            const parsed = JSON.parse(detail)
                                            analysisOverlay.plotData = parsed.data.plot || null
                                        } catch (error) {
                                            analysisOverlay.plotData = null
                                        }
                                        analysisPlotCanvas.requestPaint()
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width; height: 105; radius: 6
                    color: "#0b1320"; border.color: "#405270"
                    Text {
                        id: analysisResultDetail
                        anchors.left: parent.left; anchors.top: parent.top
                        anchors.bottom: parent.bottom; anchors.margins: 9
                        width: analysisOverlay.plotData ? parent.width - 230 : parent.width - 18
                        text: qsTr("Select a result to inspect algorithm, units, PBC, missing-data policy and source status.")
                        color: "#a9bdd5"; font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere; elide: Text.ElideRight
                    }
                    Canvas {
                        id: analysisPlotCanvas
                        objectName: "analysisPlotCanvas"
                        anchors.right: parent.right; anchors.top: parent.top
                        anchors.bottom: parent.bottom; anchors.margins: 8
                        width: 210; visible: analysisOverlay.plotData !== null
                        onPaint: {
                            const context = getContext("2d")
                            context.clearRect(0, 0, width, height)
                            context.fillStyle = "#101c2d"
                            context.fillRect(0, 0, width, height)
                            const plot = analysisOverlay.plotData
                            if (!plot || !plot.samples || plot.samples.length === 0) return
                            const pad = 12
                            if (plot.kind === "heatmap") {
                                let minimum = Number(plot.samples[0][2])
                                let maximum = minimum
                                let minCellX = Number(plot.samples[0][0]), maxCellX = minCellX
                                let minCellY = Number(plot.samples[0][1]), maxCellY = minCellY
                                for (const sample of plot.samples) {
                                    minimum = Math.min(minimum, Number(sample[2]))
                                    maximum = Math.max(maximum, Number(sample[2]))
                                    minCellX = Math.min(minCellX, Number(sample[0])); maxCellX = Math.max(maxCellX, Number(sample[0]))
                                    minCellY = Math.min(minCellY, Number(sample[1])); maxCellY = Math.max(maxCellY, Number(sample[1]))
                                }
                                const count = Math.max(1, Math.round(Math.sqrt(plot.samples.length * 2)))
                                const cellWidth = (width - 2 * pad) / count
                                const cellHeight = (height - 2 * pad) / count
                                for (const sample of plot.samples) {
                                    const fraction = maximum > minimum ? (Number(sample[2]) - minimum) / (maximum - minimum) : 0
                                    context.fillStyle = Qt.rgba(0.15 + 0.75 * fraction, 0.65 - 0.45 * fraction, 0.95 - 0.55 * fraction, 1)
                                    const cellX = maxCellX > minCellX ? Math.round((Number(sample[0]) - minCellX) / (maxCellX - minCellX) * (count - 1)) : 0
                                    const cellY = maxCellY > minCellY ? Math.round((Number(sample[1]) - minCellY) / (maxCellY - minCellY) * (count - 1)) : 0
                                    const x = pad + cellX * cellWidth
                                    const y = height - pad - (cellY + 1) * cellHeight
                                    context.fillRect(x, y, cellWidth, cellHeight)
                                    if (cellX !== cellY) context.fillRect(pad + cellY * cellWidth, height - pad - (cellX + 1) * cellHeight, cellWidth, cellHeight)
                                }
                                return
                            }
                            let minX = Number(plot.samples[0][0]), maxX = minX
                            let minY = Number(plot.samples[0][1]), maxY = minY
                            for (const sample of plot.samples) {
                                minX = Math.min(minX, Number(sample[0])); maxX = Math.max(maxX, Number(sample[0]))
                                minY = Math.min(minY, Number(sample[1])); maxY = Math.max(maxY, Number(sample[1]))
                            }
                            context.strokeStyle = "#69aef0"; context.lineWidth = 2; context.beginPath()
                            for (let index = 0; index < plot.samples.length; ++index) {
                                const sample = plot.samples[index]
                                const x = pad + (maxX > minX ? (Number(sample[0]) - minX) / (maxX - minX) : 0.5) * (width - 2 * pad)
                                const y = height - pad - (maxY > minY ? (Number(sample[1]) - minY) / (maxY - minY) : 0.5) * (height - 2 * pad)
                                if (index === 0) context.moveTo(x, y); else context.lineTo(x, y)
                            }
                            context.stroke()
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: topologyEditOverlay
        objectName: "topologyEditOverlay"
        property int modeIndex: 0
        property string actionId: modeIndex === 0
                                  ? root.editAtomPropertiesMetadata.id
                                  : modeIndex === 1
                                    ? root.editResiduePropertiesMetadata.id
                                    : root.editBondOrderMetadata.id
        property var entryAction: modeIndex === 0
                                  ? atomPropertiesAction
                                  : modeIndex === 1
                                    ? residuePropertiesAction
                                    : bondOrderAction
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 74

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: 650
            height: 500
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 13

                Text {
                    text: qsTr("Molecular Property Editor")
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: qsTr("Edit stable atom and bond IDs through validated transactions. Revisions, scene rebuild, result invalidation and undo history are managed automatically.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Row {
                    spacing: 9
                    ToolbarButton {
                        objectName: "topologyAtomModeButton"
                        label: qsTr("Atom")
                        selected: topologyEditOverlay.modeIndex === 0
                        action: function() { topologyEditOverlay.modeIndex = 0 }
                    }
                    ToolbarButton {
                        objectName: "topologyResidueModeButton"
                        label: qsTr("Residue")
                        selected: topologyEditOverlay.modeIndex === 1
                        action: function() { topologyEditOverlay.modeIndex = 1 }
                    }
                    ToolbarButton {
                        objectName: "topologyBondModeButton"
                        label: qsTr("Bond")
                        selected: topologyEditOverlay.modeIndex === 2
                        action: function() { topologyEditOverlay.modeIndex = 2 }
                    }
                }
                Row {
                    visible: topologyEditOverlay.modeIndex !== 2
                    spacing: 9
                    Column {
                        spacing: 4
                        Text { text: qsTr("Atom ID"); color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 115; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: topologyAtomIdInput; objectName: "topologyAtomIdInput"; anchors.fill: parent; anchors.margins: 9; text: "1"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                    Column {
                        spacing: 4
                        Text { text: qsTr("Name"); color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 145; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: topologyNameInput; objectName: "topologyNameInput"; anchors.fill: parent; anchors.margins: 9; text: topologyEditOverlay.modeIndex === 0 ? "C1" : "LIG"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                    Column {
                        visible: topologyEditOverlay.modeIndex === 0
                        spacing: 4
                        Text { text: qsTr("Atomic number"); color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 130; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: topologyAtomicNumberInput; objectName: "topologyAtomicNumberInput"; anchors.fill: parent; anchors.margins: 9; text: "6"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                    Column {
                        visible: topologyEditOverlay.modeIndex === 0
                        spacing: 4
                        Text { text: qsTr("Formal charge"); color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 125; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: topologyFormalChargeInput; objectName: "topologyFormalChargeInput"; anchors.fill: parent; anchors.margins: 9; text: "0"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                }
                Row {
                    visible: topologyEditOverlay.modeIndex === 1
                    spacing: 9
                    Column {
                        spacing: 4
                        Text { text: qsTr("Chain"); color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 160; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: topologyChainInput; objectName: "topologyChainInput"; anchors.fill: parent; anchors.margins: 9; text: "A"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                    Column {
                        spacing: 4
                        Text { text: qsTr("Residue number"); color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 160; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: topologyResidueNumberInput; objectName: "topologyResidueNumberInput"; anchors.fill: parent; anchors.margins: 9; text: "1"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                }
                Row {
                    visible: topologyEditOverlay.modeIndex === 2
                    spacing: 9
                    Column {
                        spacing: 4
                        Text { text: qsTr("Bond ID"); color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 150; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: topologyBondIdInput; objectName: "topologyBondIdInput"; anchors.fill: parent; anchors.margins: 9; text: "1"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                    Column {
                        spacing: 4
                        Text { text: qsTr("Bond order"); color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 220; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: topologyBondOrderInput; objectName: "topologyBondOrderInput"; anchors.fill: parent; anchors.margins: 9; text: "single"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                }
                Text {
                    width: parent.width
                    text: topologyEditOverlay.modeIndex === 2
                          ? qsTr("Bond order must be single, double, triple, aromatic, or amide.")
                          : qsTr("Blank optional fields are left unchanged. Residues are identified by any stable atom ID that belongs to them.")
                    color: "#a9bdd5"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Row {
                    spacing: 9
                    ToolbarButton {
                        objectName: "topologyEditApplyButton"
                        label: qsTr("Apply")
                        selected: true
                        action: function() {
                            let edited = false
                            if (topologyEditOverlay.modeIndex === 0)
                                edited = viewport.editAtomProperties(
                                    Number(topologyAtomIdInput.text),
                                    topologyNameInput.text,
                                    topologyAtomicNumberInput.text,
                                    topologyFormalChargeInput.text)
                            else if (topologyEditOverlay.modeIndex === 1)
                                edited = viewport.editResidueProperties(
                                    Number(topologyAtomIdInput.text),
                                    topologyNameInput.text,
                                    topologyChainInput.text,
                                    topologyResidueNumberInput.text)
                            else
                                edited = viewport.editBondOrder(
                                    Number(topologyBondIdInput.text),
                                    topologyBondOrderInput.text)
                            if (edited)
                                root.refreshEditHistory()
                        }
                    }
                    ToolbarButton {
                        objectName: "topologyEditUndoButton"
                        label: qsTr("Undo")
                        action: function() { undoEditAction.trigger() }
                    }
                    ToolbarButton {
                        objectName: "topologyEditRedoButton"
                        label: qsTr("Redo")
                        action: function() { redoEditAction.trigger() }
                    }
                    ToolbarButton {
                        objectName: "topologyEditCloseButton"
                        label: qsTr("Close")
                        action: function() { topologyEditOverlay.visible = false }
                    }
                }
                Text {
                    objectName: "topologyEditHistoryText"
                    width: parent.width
                    text: qsTr("Undo: %1 · Redo: %2 · History: %3 / %4 bytes")
                          .arg(root.editHistoryData.undo_count || 0)
                          .arg(root.editHistoryData.redo_count || 0)
                          .arg(root.editHistoryData.memory_used_bytes || 0)
                          .arg(root.editHistoryData.memory_budget_bytes || 0)
                    color: "#c8d8ea"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Rectangle {
        id: moleculeBuilderOverlay
        objectName: "moleculeBuilderOverlay"
        property string actionId: root.moleculeBuilderMetadata.id
        property var entryAction: moleculeBuilderAction
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 74

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: 650
            height: 500
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 12
                Text { text: qsTr("Molecule Builder"); color: "#eef6ff"; font.pixelSize: 20; font.bold: true }
                Text {
                    width: parent.width
                    text: qsTr("Create one validated ligand residue. Atom rows use name,atomic-number,x,y,z,formal-charge; bond rows use first,second,order.")
                    color: "#a9bdd5"; font.pixelSize: 13; wrapMode: Text.WordWrap
                }
                Row {
                    spacing: 9
                    Rectangle { width: 180; height: 36; radius: 6; color: "#172235"; border.color: "#506889"; TextInput { id: moleculeBuilderNameInput; objectName: "moleculeBuilderNameInput"; anchors.fill: parent; anchors.margins: 9; text: "ligand"; color: "#eef6ff"; selectByMouse: true } }
                    Rectangle { width: 120; height: 36; radius: 6; color: "#172235"; border.color: "#506889"; TextInput { id: moleculeBuilderResidueInput; objectName: "moleculeBuilderResidueInput"; anchors.fill: parent; anchors.margins: 9; text: "LIG"; color: "#eef6ff"; selectByMouse: true } }
                    Rectangle { width: 80; height: 36; radius: 6; color: "#172235"; border.color: "#506889"; TextInput { id: moleculeBuilderChainInput; objectName: "moleculeBuilderChainInput"; anchors.fill: parent; anchors.margins: 9; text: "A"; color: "#eef6ff"; selectByMouse: true } }
                    Rectangle { width: 90; height: 36; radius: 6; color: "#172235"; border.color: "#506889"; TextInput { id: moleculeBuilderResidueNumberInput; objectName: "moleculeBuilderResidueNumberInput"; anchors.fill: parent; anchors.margins: 9; text: "1"; color: "#eef6ff"; selectByMouse: true } }
                }
                Text { text: qsTr("Atoms"); color: "#a9bdd5"; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 72; radius: 6
                    color: "#172235"; border.color: "#506889"
                    TextArea { id: moleculeBuilderAtomsInput; objectName: "moleculeBuilderAtomsInput"; anchors.fill: parent; text: "C,6,0,0,0,0;O,8,1.2,0,0,0"; color: "#eef6ff"; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere }
                }
                Text { text: qsTr("Bonds"); color: "#a9bdd5"; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 58; radius: 6
                    color: "#172235"; border.color: "#506889"
                    TextArea { id: moleculeBuilderBondsInput; objectName: "moleculeBuilderBondsInput"; anchors.fill: parent; text: "1,2,double"; color: "#eef6ff"; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere }
                }
                Text {
                    width: parent.width
                    text: qsTr("Bond order: single, double, triple, aromatic, or amide. Coordinates are in Å.")
                    color: "#a9bdd5"; font.pixelSize: 12; wrapMode: Text.WordWrap
                }
                Row {
                    spacing: 9
                    ToolbarButton {
                        objectName: "moleculeBuilderApplyButton"
                        label: qsTr("Build")
                        selected: true
                        action: function() {
                            if (viewport.buildMolecule(
                                    moleculeBuilderNameInput.text,
                                    moleculeBuilderAtomsInput.text,
                                    moleculeBuilderBondsInput.text,
                                    moleculeBuilderResidueInput.text,
                                    moleculeBuilderChainInput.text,
                                    Number(moleculeBuilderResidueNumberInput.text),
                                    "angstrom", 268435456))
                                moleculeBuilderOverlay.visible = false
                        }
                    }
                    ToolbarButton { objectName: "moleculeBuilderCloseButton"; label: qsTr("Close"); action: function() { moleculeBuilderOverlay.visible = false } }
                }
            }
        }
    }

    Rectangle {
        id: coordinateEditOverlay
        objectName: "coordinateEditOverlay"
        property string actionId: root.editAtomPositionMetadata.id
        property var entryAction: coordinateEditAction
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 74

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: 570
            height: 355
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                Text {
                    text: qsTr("Atom Coordinate Editor")
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: qsTr("Move a stable atom ID in all static coordinate states. Attached trajectories are protected from editing.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Row {
                    spacing: 9
                    Column {
                        spacing: 4
                        Text { text: qsTr("Atom ID"); color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 115; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: coordinateAtomInput; objectName: "coordinateAtomInput"; anchors.fill: parent; anchors.margins: 9; text: "1"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                    Column {
                        spacing: 4
                        Text { text: "X"; color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 115; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: coordinateXInput; objectName: "coordinateXInput"; anchors.fill: parent; anchors.margins: 9; text: "0"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                    Column {
                        spacing: 4
                        Text { text: "Y"; color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 115; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: coordinateYInput; objectName: "coordinateYInput"; anchors.fill: parent; anchors.margins: 9; text: "0"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                    Column {
                        spacing: 4
                        Text { text: "Z"; color: "#a9bdd5"; font.pixelSize: 12 }
                        Rectangle {
                            width: 115; height: 36; radius: 6
                            color: "#172235"; border.color: "#506889"
                            TextInput { id: coordinateZInput; objectName: "coordinateZInput"; anchors.fill: parent; anchors.margins: 9; text: "0"; color: "#eef6ff"; selectByMouse: true }
                        }
                    }
                }
                Row {
                    spacing: 9
                    ToolbarButton {
                        objectName: "coordinateApplyButton"
                        label: qsTr("Apply")
                        selected: true
                        action: function() {
                            if (viewport.editAtomPosition(Number(coordinateAtomInput.text),
                                                          Number(coordinateXInput.text),
                                                          Number(coordinateYInput.text),
                                                          Number(coordinateZInput.text)))
                                root.refreshEditHistory()
                        }
                    }
                    ToolbarButton {
                        objectName: "coordinateUndoButton"
                        label: qsTr("Undo")
                        action: function() { undoEditAction.trigger() }
                    }
                    ToolbarButton {
                        objectName: "coordinateRedoButton"
                        label: qsTr("Redo")
                        action: function() { redoEditAction.trigger() }
                    }
                    ToolbarButton {
                        objectName: "coordinateCloseButton"
                        label: qsTr("Close")
                        action: function() { coordinateEditOverlay.visible = false }
                    }
                }
                Text {
                    objectName: "coordinateHistoryText"
                    width: parent.width
                    text: qsTr("Undo: %1 · Redo: %2 · History: %3 / %4 bytes")
                          .arg(root.editHistoryData.undo_count || 0)
                          .arg(root.editHistoryData.redo_count || 0)
                          .arg(root.editHistoryData.memory_used_bytes || 0)
                          .arg(root.editHistoryData.memory_budget_bytes || 0)
                    color: "#c8d8ea"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Rectangle {
        id: renderSettingsOverlay
        objectName: "renderSettingsOverlay"
        property string actionId: root.renderSettingsMetadata.id
        property var entryAction: renderSettingsAction
        property int scopeIndex: 0
        property var scopes: ["global", "object", "state", "atom", "bond"]
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 74

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: 590
            height: 390
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                Text {
                    text: qsTr("Render Settings")
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }

                Text {
                    width: parent.width
                    text: qsTr("The editor calls the same typed setting operation as CLI and Python. State is current; atom and bond IDs are 1-based.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Row {
                    spacing: 10

                    Rectangle {
                        width: 210; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: renderSettingNameInput
                            anchors.fill: parent; anchors.margins: 9
                            text: "sphere_scale"
                            color: "#eef6ff"; selectByMouse: true
                        }
                    }
                    Rectangle {
                        width: 125; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: renderSettingValueInput
                            anchors.fill: parent; anchors.margins: 9
                            text: "1.0"
                            color: "#eef6ff"; selectByMouse: true
                        }
                    }
                    Rectangle {
                        width: 125; height: 36; radius: 6
                        color: "#172235"; border.color: "#506889"
                        TextInput {
                            id: renderSettingTargetInput
                            anchors.fill: parent; anchors.margins: 9
                            text: "1"
                            color: "#eef6ff"; selectByMouse: true
                        }
                    }
                }

                Row {
                    spacing: 9

                    ToolbarButton {
                        label: qsTr("Scope: %1").arg(renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex])
                        action: function() {
                            renderSettingsOverlay.scopeIndex =
                                (renderSettingsOverlay.scopeIndex + 1) % renderSettingsOverlay.scopes.length
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Set")
                        selected: true
                        action: function() {
                            viewport.applyRenderSetting("set", renderSettingNameInput.text,
                                                        renderSettingValueInput.text,
                                                        renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                                        renderSettingTargetInput.text)
                            renderSettingResult.text = viewport.renderSettingJson(
                                renderSettingNameInput.text,
                                renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                renderSettingTargetInput.text)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Get")
                        action: function() {
                            renderSettingResult.text = viewport.renderSettingJson(
                                renderSettingNameInput.text,
                                renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                renderSettingTargetInput.text)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Unset")
                        action: function() {
                            viewport.applyRenderSetting("unset", renderSettingNameInput.text, "",
                                                        renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                                        renderSettingTargetInput.text)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Reset scope")
                        action: function() {
                            viewport.applyRenderSetting("reset", "", "",
                                                        renderSettingsOverlay.scopes[renderSettingsOverlay.scopeIndex],
                                                        renderSettingTargetInput.text)
                        }
                    }
                }

                Rectangle {
                    width: parent.width; height: 92; radius: 7
                    color: "#111b2a"; border.color: "#405270"
                    Text {
                        id: renderSettingResult
                        objectName: "renderSettingResult"
                        anchors.fill: parent; anchors.margins: 10
                        color: "#bcd4ec"; font.pixelSize: 12
                        wrapMode: Text.WrapAnywhere; elide: Text.ElideRight
                    }
                }

                ToolbarButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    label: qsTr("Close")
                    action: function() { renderSettingsOverlay.visible = false }
                }
            }
        }
    }

    Rectangle {
        id: viewsOverlay
        objectName: "viewsOverlay"
        property string actionId: root.sceneViewsMetadata.id
        property var entryAction: sceneViewsAction
        property bool clearArmed: false
        property int clipModeIndex: 0
        property var clipModes: ["atoms", "slab", "near", "far", "move", "near-set", "far-set"]
        property string clipRange: ""
        property int navigationAxisIndex: 0
        property var navigationAxes: ["x", "y", "z"]
        property int projectionModeIndex: 0
        property var projectionModes: ["perspective", "orthographic"]
        property bool preserveProjectionScale: true
        property bool stereoEnabled: false
        property int stereoModeIndex: 0
        property var stereoModes: ["side_by_side", "crosseye", "walleye", "anaglyph",
                                   "row_interleaved", "column_interleaved", "checkerboard"]
        property bool stereoSwapEyes: false
        property int anaglyphModeIndex: 4
        property var anaglyphModes: ["true", "gray", "color", "half_color", "optimized"]
        property int cameraStateModeIndex: 0
        property var cameraStateModes: ["current", "all", "explicit"]
        function cameraStateArgument() {
            return cameraStateModes[cameraStateModeIndex] === "explicit"
                    ? cameraStateInput.text
                    : cameraStateModes[cameraStateModeIndex]
        }
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 75

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(680, parent.width - 40)
            height: Math.min(720, parent.height - 40)
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Flickable {
                id: viewsScroller
                anchors.fill: parent
                anchors.margins: 24
                contentHeight: viewsColumn.implicitHeight
                clip: true

                Column {
                    id: viewsColumn
                    width: viewsScroller.width
                    spacing: 12

                Text {
                    text: qsTr("Camera & Named Views")
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }

                Text {
                    width: parent.width
                    text: qsTr("Frame a selection or change its rotation pivot. The same actions are available from CLI and Python.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Row {
                    spacing: 10

                    Rectangle {
                        width: 290
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: cameraSelectionInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: cameraSelectionInput
                            objectName: "cameraSelectionInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "all"
                            color: "#eef6ff"
                            selectionColor: "#285f99"
                            selectedTextColor: "#ffffff"
                            font.pixelSize: 14
                            clip: true
                        }
                    }

                    ToolbarButton {
                        objectName: "cameraStateModeButton"
                        label: qsTr("State %1").arg(viewsOverlay.cameraStateModes[viewsOverlay.cameraStateModeIndex])
                        action: function() {
                            viewsOverlay.cameraStateModeIndex =
                                (viewsOverlay.cameraStateModeIndex + 1) % viewsOverlay.cameraStateModes.length
                        }
                    }

                    Rectangle {
                        width: 70
                        height: 36
                        visible: viewsOverlay.cameraStateModes[viewsOverlay.cameraStateModeIndex] === "explicit"
                        radius: 6
                        color: "#111b2b"
                        border.color: cameraStateInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: cameraStateInput
                            objectName: "cameraStateInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "1"
                            color: "#eef6ff"
                            validator: IntValidator { bottom: 1 }
                            font.pixelSize: 14
                        }
                    }
                }

                Row {
                    spacing: 10

                    ToolbarButton {
                        label: qsTr("Center")
                        action: function() {
                            viewport.centerSelection(cameraSelectionInput.text,
                                                     true, viewsOverlay.cameraStateArgument(),
                                                     0.35, 1)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Fit")
                        action: function() {
                            viewport.zoomSelection(cameraSelectionInput.text,
                                                   0.0, true,
                                                   viewsOverlay.cameraStateArgument(),
                                                   0.35, 1)
                        }
                    }
                    ToolbarButton {
                        objectName: "cameraOrientButton"
                        label: qsTr("Orient")
                        action: function() {
                            viewport.orientSelection(
                                cameraSelectionInput.text,
                                viewsOverlay.cameraStateArgument(), 0.35, 1)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Set pivot")
                        action: function() {
                            viewport.setOriginSelection(
                                cameraSelectionInput.text,
                                viewsOverlay.cameraStateArgument())
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Reset all")
                        action: function() { viewport.resetViewAnimated(0.35, 1) }
                    }
                }

                Row {
                    spacing: 8

                    Rectangle {
                        width: 150
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: objectOriginInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: objectOriginInput
                            objectName: "objectOriginInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "current"
                            color: "#eef6ff"
                            font.pixelSize: 14
                            clip: true
                        }
                    }

                    Rectangle {
                        width: 58
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: objectOriginX.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: objectOriginX
                            objectName: "objectOriginX"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "0"
                            color: "#eef6ff"
                            validator: DoubleValidator {}
                            font.pixelSize: 14
                            horizontalAlignment: TextInput.AlignHCenter
                        }
                    }
                    Rectangle {
                        width: 58
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: objectOriginY.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: objectOriginY
                            objectName: "objectOriginY"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "0"
                            color: "#eef6ff"
                            validator: DoubleValidator {}
                            font.pixelSize: 14
                            horizontalAlignment: TextInput.AlignHCenter
                        }
                    }
                    Rectangle {
                        width: 58
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: objectOriginZ.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: objectOriginZ
                            objectName: "objectOriginZ"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "0"
                            color: "#eef6ff"
                            validator: DoubleValidator {}
                            font.pixelSize: 14
                            horizontalAlignment: TextInput.AlignHCenter
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Object pivot")
                        action: function() {
                            viewport.setObjectOriginSelection(
                                objectOriginInput.text, cameraSelectionInput.text,
                                viewsOverlay.cameraStateArgument())
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Reset object")
                        action: function() {
                            viewport.resetObjectTransform(objectOriginInput.text)
                        }
                    }
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        label: qsTr("Set XYZ camera")
                        action: function() {
                            viewport.setOriginPosition(
                                Number(objectOriginX.text), Number(objectOriginY.text),
                                Number(objectOriginZ.text), "")
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Object accepts current, all (reset only), name, or ID")
                        color: "#8ea5bf"
                        font.pixelSize: 12
                    }
                }

                Text {
                    text: qsTr("Stereo · adjacent-eye and anaglyph presentation")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        objectName: "stereoEnabledButton"
                        label: viewsOverlay.stereoEnabled ? qsTr("Stereo on") : qsTr("Stereo off")
                        selected: viewsOverlay.stereoEnabled
                        action: function() {
                            viewsOverlay.stereoEnabled = !viewsOverlay.stereoEnabled
                        }
                    }
                    ToolbarButton {
                        objectName: "stereoModeButton"
                        label: viewsOverlay.stereoModes[viewsOverlay.stereoModeIndex]
                        action: function() {
                            viewsOverlay.stereoModeIndex =
                                (viewsOverlay.stereoModeIndex + 1) % viewsOverlay.stereoModes.length
                        }
                    }
                    ToolbarButton {
                        objectName: "stereoSwapButton"
                        label: viewsOverlay.stereoSwapEyes ? qsTr("Eyes swapped") : qsTr("Eye order")
                        selected: viewsOverlay.stereoSwapEyes
                        action: function() {
                            viewsOverlay.stereoSwapEyes = !viewsOverlay.stereoSwapEyes
                        }
                    }
                }

                Row {
                    spacing: 8

                    Rectangle {
                        width: 90
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: stereoShiftInput.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: stereoShiftInput
                            objectName: "stereoShiftInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "2.0"
                            color: "#eef6ff"
                            validator: DoubleValidator { bottom: 0; top: 100 }
                            font.pixelSize: 14
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "% shift"
                        color: "#8ea5bf"
                        font.pixelSize: 12
                    }
                    Rectangle {
                        width: 90
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: stereoAngleInput.activeFocus ? "#69aef0" : "#405270"
                        TextInput {
                            id: stereoAngleInput
                            objectName: "stereoAngleInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "2.1"
                            color: "#eef6ff"
                            validator: DoubleValidator { bottom: 0; top: 20 }
                            font.pixelSize: 14
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("angle scale")
                        color: "#8ea5bf"
                        font.pixelSize: 12
                    }
                    ToolbarButton {
                        objectName: "anaglyphModeButton"
                        label: qsTr("Anaglyph %1").arg(viewsOverlay.anaglyphModes[viewsOverlay.anaglyphModeIndex])
                        action: function() {
                            viewsOverlay.anaglyphModeIndex =
                                (viewsOverlay.anaglyphModeIndex + 1) % viewsOverlay.anaglyphModes.length
                        }
                    }
                    ToolbarButton {
                        objectName: "stereoApplyButton"
                        label: qsTr("Apply stereo")
                        action: function() {
                            const shift = Number(stereoShiftInput.text)
                            const angle = Number(stereoAngleInput.text)
                            if (Number.isFinite(shift) && Number.isFinite(angle))
                                viewport.setStereo(
                                    viewsOverlay.stereoEnabled,
                                    viewsOverlay.stereoModes[viewsOverlay.stereoModeIndex],
                                    viewsOverlay.stereoSwapEyes, shift, angle,
                                    viewsOverlay.anaglyphModes[viewsOverlay.anaglyphModeIndex])
                        }
                    }
                }

                Text {
                    text: qsTr("Projection · preserve target-plane scale by default")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        objectName: "projectionModeButton"
                        label: viewsOverlay.projectionModes[viewsOverlay.projectionModeIndex]
                        action: function() {
                            viewsOverlay.projectionModeIndex =
                                (viewsOverlay.projectionModeIndex + 1) % viewsOverlay.projectionModes.length
                        }
                    }

                    Rectangle {
                        width: 90
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: projectionFovInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: projectionFovInput
                            objectName: "projectionFovInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "45"
                            color: "#eef6ff"
                            validator: DoubleValidator { bottom: 0.000001; top: 179.999999 }
                            font.pixelSize: 14
                        }
                    }

                    ToolbarButton {
                        objectName: "projectionPreserveButton"
                        label: viewsOverlay.preserveProjectionScale ? qsTr("Scale locked") : qsTr("Raw switch")
                        selected: viewsOverlay.preserveProjectionScale
                        action: function() {
                            viewsOverlay.preserveProjectionScale =
                                !viewsOverlay.preserveProjectionScale
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Apply projection")
                        action: function() {
                            const fov = Number(projectionFovInput.text)
                            if (Number.isFinite(fov))
                                viewport.setProjection(
                                    viewsOverlay.projectionModes[viewsOverlay.projectionModeIndex],
                                    fov, viewsOverlay.preserveProjectionScale)
                        }
                    }
                }

                Text {
                    text: qsTr("Axis navigation · camera-local move / pivoted turn")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 8

                    ToolbarButton {
                        objectName: "navigationAxisButton"
                        label: qsTr("Axis %1").arg(viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex].toUpperCase())
                        action: function() {
                            viewsOverlay.navigationAxisIndex =
                                (viewsOverlay.navigationAxisIndex + 1) % viewsOverlay.navigationAxes.length
                        }
                    }

                    Rectangle {
                        width: 70
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: navigationStepInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: navigationStepInput
                            objectName: "navigationStepInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "5"
                            color: "#eef6ff"
                            validator: DoubleValidator { bottom: 0.000001 }
                            font.pixelSize: 14
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Move −")
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.moveCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], -step)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Move +")
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.moveCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], step)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Turn −")
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.turnCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], -step)
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Turn +")
                        action: function() {
                            const step = Number(navigationStepInput.text)
                            if (Number.isFinite(step))
                                viewport.turnCamera(
                                    viewsOverlay.navigationAxes[viewsOverlay.navigationAxisIndex], step)
                        }
                    }
                }

                Text {
                    text: qsTr("Clipping · click mode to change · %1").arg(viewsOverlay.clipRange)
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 10

                    ToolbarButton {
                        objectName: "clipModeButton"
                        label: viewsOverlay.clipModes[viewsOverlay.clipModeIndex]
                        action: function() {
                            viewsOverlay.clipModeIndex =
                                (viewsOverlay.clipModeIndex + 1) % viewsOverlay.clipModes.length
                        }
                    }

                    Rectangle {
                        width: 90
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: clipDistanceInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: clipDistanceInput
                            objectName: "clipDistanceInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: "5"
                            color: "#eef6ff"
                            validator: DoubleValidator {}
                            font.pixelSize: 14
                        }
                    }

                    Rectangle {
                        width: 205
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: clipSelectionInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: clipSelectionInput
                            objectName: "clipSelectionInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            text: cameraSelectionInput.text
                            color: "#eef6ff"
                            font.pixelSize: 14
                            clip: true
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Apply")
                        action: function() {
                            const distance = Number(clipDistanceInput.text)
                            if (Number.isFinite(distance) && viewport.clipCamera(
                                    viewsOverlay.clipModes[viewsOverlay.clipModeIndex],
                                    distance, clipSelectionInput.text,
                                    viewsOverlay.cameraStateArgument()))
                                viewsOverlay.clipRange = viewport.clipRangeText()
                        }
                    }
                }

                Text {
                    text: qsTr("Named views")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 10

                    Rectangle {
                        width: 350
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: viewNameInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: viewNameInput
                            objectName: "viewNameInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            color: "#eef6ff"
                            selectionColor: "#285f99"
                            selectedTextColor: "#ffffff"
                            font.pixelSize: 14
                            clip: true
                            onAccepted: {
                                if (text.trim().length > 0 && viewport.storeNamedView(text))
                                    text = ""
                            }

                            Text {
                                anchors.fill: parent
                                text: qsTr("View name")
                                color: "#667b94"
                                font.pixelSize: 14
                                visible: viewNameInput.text.length === 0 && !viewNameInput.activeFocus
                            }
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Store")
                        action: function() {
                            if (viewNameInput.text.trim().length > 0 &&
                                    viewport.storeNamedView(viewNameInput.text))
                                viewNameInput.text = ""
                        }
                    }
                }

                Text {
                    visible: viewport.viewItems.length === 0
                    text: qsTr("No named views stored yet.")
                    color: "#8291a6"
                    font.pixelSize: 13
                }

                ListView {
                    width: parent.width
                    height: 55
                    spacing: 7
                    clip: true
                    model: viewport.viewItems

                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 42
                        radius: 6
                        color: "#172235"
                        border.color: "#354a66"

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            width: 275
                            text: parent.modelData.name + " · " +
                                  parent.modelData.projection
                            color: "#e5f1ff"
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        ToolbarButton {
                            anchors.right: deleteViewButton.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            label: qsTr("Recall")
                            action: function() {
                                viewport.recallNamedViewAnimated(
                                    parent.modelData.name, 0.35, 1)
                            }
                        }

                        ToolbarButton {
                            id: deleteViewButton
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            label: qsTr("Delete")
                            action: function() {
                                viewport.deleteNamedView(parent.modelData.name)
                            }
                        }
                    }
                }

                Text {
                    text: qsTr("Named scenes")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    spacing: 10

                    Rectangle {
                        width: 350
                        height: 36
                        radius: 6
                        color: "#111b2b"
                        border.color: sceneNameInput.activeFocus ? "#69aef0" : "#405270"

                        TextInput {
                            id: sceneNameInput
                            objectName: "sceneNameInput"
                            anchors.fill: parent
                            anchors.margins: 9
                            color: "#eef6ff"
                            font.pixelSize: 14
                            clip: true
                            onAccepted: {
                                if (text.trim().length > 0 && viewport.storeNamedScene(text))
                                    text = ""
                            }
                            Text {
                                anchors.fill: parent
                                text: qsTr("Scene name")
                                color: "#667b94"
                                font.pixelSize: 14
                                visible: sceneNameInput.text.length === 0 && !sceneNameInput.activeFocus
                            }
                        }
                    }

                    ToolbarButton {
                        objectName: "storeNamedSceneButton"
                        label: qsTr("Store scene")
                        action: function() {
                            if (sceneNameInput.text.trim().length > 0 &&
                                    viewport.storeNamedScene(sceneNameInput.text))
                                sceneNameInput.text = ""
                        }
                    }
                }

                Text {
                    visible: viewport.sceneItems.length === 0
                    text: qsTr("No named scenes stored yet.")
                    color: "#8291a6"
                    font.pixelSize: 13
                }

                ListView {
                    objectName: "namedSceneList"
                    width: parent.width
                    height: 92
                    spacing: 7
                    clip: true
                    model: viewport.sceneItems

                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 42
                        radius: 6
                        color: modelData.current ? "#203a56" : "#172235"
                        border.color: modelData.current ? "#69aef0" : "#354a66"

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            width: 260
                            text: parent.modelData.name + " · " +
                                  qsTr("%1 objects, %2 volumes")
                                      .arg(parent.modelData.objectCount)
                                      .arg(parent.modelData.volumeCount)
                            color: "#e5f1ff"
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        ToolbarButton {
                            anchors.right: deleteSceneButton.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            label: qsTr("Recall")
                            action: function() {
                                viewport.recallNamedScene(parent.modelData.name)
                            }
                        }

                        ToolbarButton {
                            id: deleteSceneButton
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            label: qsTr("Delete")
                            action: function() {
                                viewport.deleteNamedScene(parent.modelData.name)
                            }
                        }
                    }
                }

                Text {
                    text: qsTr("Movie timeline")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Text {
                    text: viewport.movieState.configured
                          ? qsTr("Frame %1 / %2 · %3 fps · %4 keyframes")
                                .arg(viewport.movieState.currentFrame)
                                .arg(viewport.movieState.frameCount)
                                .arg(viewport.movieState.fps)
                                .arg(viewport.movieState.keyframeCount)
                          : qsTr("No movie timeline configured.")
                    color: "#9db0c8"
                    font.pixelSize: 12
                }

                Row {
                    spacing: 8
                    Rectangle {
                        width: 90; height: 36; radius: 6
                        color: "#111b2b"; border.color: "#405270"
                        TextInput {
                            id: movieFrameCountInput
                            objectName: "movieFrameCountInput"
                            anchors.fill: parent; anchors.margins: 9
                            text: "120"; color: "#eef6ff"
                            validator: IntValidator { bottom: 1; top: 1000000 }
                        }
                    }
                    Rectangle {
                        width: 90; height: 36; radius: 6
                        color: "#111b2b"; border.color: "#405270"
                        TextInput {
                            id: movieFpsInput
                            objectName: "movieFpsInput"
                            anchors.fill: parent; anchors.margins: 9
                            text: "30"; color: "#eef6ff"
                            validator: DoubleValidator { bottom: 0.01; top: 240 }
                        }
                    }
                    CheckBox {
                        id: movieLoopCheck
                        objectName: "movieLoopCheck"
                        text: qsTr("Loop")
                    }
                    ToolbarButton {
                        objectName: "configureMovieButton"
                        label: qsTr("Configure")
                        action: function() {
                            viewport.configureMovie(Number(movieFrameCountInput.text),
                                                    Number(movieFpsInput.text),
                                                    movieLoopCheck.checked)
                        }
                    }
                }

                Row {
                    spacing: 8
                    Rectangle {
                        width: 70; height: 36; radius: 6
                        color: "#111b2b"; border.color: "#405270"
                        TextInput {
                            id: movieKeyFrameInput
                            objectName: "movieKeyFrameInput"
                            anchors.fill: parent; anchors.margins: 9
                            text: "1"; color: "#eef6ff"
                            validator: IntValidator { bottom: 1 }
                        }
                    }
                    Rectangle {
                        width: 190; height: 36; radius: 6
                        color: "#111b2b"; border.color: "#405270"
                        TextInput {
                            id: movieKeySceneInput
                            objectName: "movieKeySceneInput"
                            anchors.fill: parent; anchors.margins: 9
                            color: "#eef6ff"; clip: true
                            Text {
                                anchors.fill: parent
                                text: qsTr("Named scene")
                                color: "#667b94"
                                visible: movieKeySceneInput.text.length === 0 && !movieKeySceneInput.activeFocus
                            }
                        }
                    }
                    Rectangle {
                        width: 110; height: 36; radius: 6
                        color: "#111b2b"; border.color: "#405270"
                        TextInput {
                            id: movieKeyTrajectoryInput
                            objectName: "movieKeyTrajectoryInput"
                            anchors.fill: parent; anchors.margins: 9
                            color: "#eef6ff"
                            validator: IntValidator { bottom: 0 }
                            Text {
                                anchors.fill: parent
                                text: qsTr("Trajectory frame")
                                color: "#667b94"; font.pixelSize: 11
                                visible: movieKeyTrajectoryInput.text.length === 0 && !movieKeyTrajectoryInput.activeFocus
                            }
                        }
                    }
                    ToolbarButton {
                        objectName: "storeMovieKeyframeButton"
                        label: qsTr("Set key")
                        action: function() {
                            viewport.setMovieKeyframe(
                                Number(movieKeyFrameInput.text),
                                movieKeySceneInput.text,
                                movieKeyTrajectoryInput.text.length > 0
                                    ? Number(movieKeyTrajectoryInput.text) : -1)
                        }
                    }
                }

                Row {
                    spacing: 8
                    Rectangle {
                        width: 70; height: 36; radius: 6
                        color: "#111b2b"; border.color: "#405270"
                        TextInput {
                            id: movieSeekInput
                            objectName: "movieSeekInput"
                            anchors.fill: parent; anchors.margins: 9
                            text: viewport.movieState.configured
                                  ? String(viewport.movieState.currentFrame) : "1"
                            color: "#eef6ff"
                            validator: IntValidator { bottom: 1 }
                        }
                    }
                    ToolbarButton {
                        objectName: "seekMovieButton"
                        label: qsTr("Seek")
                        enabled: viewport.movieState.configured
                        action: function() { viewport.seekMovie(Number(movieSeekInput.text)) }
                    }
                    ToolbarButton {
                        objectName: "playMovieButton"
                        label: viewport.movieState.playing ? qsTr("Pause") : qsTr("Play")
                        enabled: viewport.movieState.configured
                        action: function() {
                            viewport.setMoviePlaying(!viewport.movieState.playing)
                        }
                    }
                    ToolbarButton {
                        objectName: "stepMovieButton"
                        label: qsTr("Step")
                        enabled: viewport.movieState.configured
                        action: function() { viewport.stepMovie(1) }
                    }
                    ToolbarButton {
                        objectName: "clearMovieButton"
                        label: qsTr("Clear movie")
                        enabled: viewport.movieState.configured
                        action: function() { viewport.clearMovie() }
                    }
                }

                Text {
                    text: qsTr("PyMOL 18-value camera")
                    color: "#dceaff"
                    font.pixelSize: 13
                    font.bold: true
                }

                Rectangle {
                    width: parent.width
                    height: 50
                    radius: 6
                    color: "#111b2b"
                    border.color: pymolViewInput.activeFocus ? "#69aef0" : "#405270"

                    TextEdit {
                        id: pymolViewInput
                        objectName: "pymolViewInput"
                        anchors.fill: parent
                        anchors.margins: 8
                        color: "#dceaff"
                        selectionColor: "#285f99"
                        selectedTextColor: "#ffffff"
                        font.pixelSize: 11
                        wrapMode: TextEdit.WrapAnywhere
                        clip: true

                        Text {
                            anchors.fill: parent
                            text: qsTr("Paste cmd.get_view() values here, or export the current camera.")
                            color: "#667b94"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            visible: pymolViewInput.text.length === 0 && !pymolViewInput.activeFocus
                        }
                    }
                }

                Row {
                    spacing: 10

                    ToolbarButton {
                        label: qsTr("Export current")
                        action: function() {
                            pymolViewInput.text = viewport.pymolViewText()
                            pymolViewInput.forceActiveFocus()
                            pymolViewInput.selectAll()
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Import values")
                        action: function() {
                            viewport.importPymolViewAnimated(
                                pymolViewInput.text, 0.35, 1)
                        }
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: viewsOverlay.clearArmed
                    text: qsTr("This removes every stored camera view. Click confirm to continue.")
                    color: "#e7b56f"
                    font.pixelSize: 12
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    ToolbarButton {
                        label: viewsOverlay.clearArmed ? qsTr("Confirm clear all") : qsTr("Clear all")
                        selected: viewsOverlay.clearArmed
                        action: function() {
                            if (!viewsOverlay.clearArmed) {
                                viewsOverlay.clearArmed = true
                                return
                            }
                            viewport.clearNamedViews()
                            viewsOverlay.clearArmed = false
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Close")
                        action: function() {
                            viewsOverlay.clearArmed = false
                            viewsOverlay.visible = false
                        }
                    }
                }
                }
            }
        }
    }

    Rectangle {
        id: chemicalSemanticsOverlay
        objectName: "chemicalSemanticsOverlay"
        property string actionId: root.objectChemistryMetadata.id
        property var entryAction: objectChemistryAction
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 80

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 40)
            height: Math.min(460, parent.height - 40)
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 9

                Text {
                    text: qsTr("Chemical Semantics")
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }
                Text {
                    objectName: "chemicalSemanticsSummary"
                    width: parent.width
                    text: root.chemicalSemanticsError.length > 0
                          ? root.chemicalSemanticsError
                          : qsTr("Normalized chemistry reported by the canonical object operation.")
                    color: root.chemicalSemanticsError.length > 0 ? "#ff9f9f" : "#a9bdd5"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Rectangle { width: parent.width; height: 1; color: "#405270" }
                InfoRow {
                    width: parent.width
                    label: qsTr("Object")
                    value: root.chemicalSemanticsData.object_name === undefined
                           ? qsTr("Not reported")
                           : String(root.chemicalSemanticsData.object_name)
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Atoms / residues / bonds")
                    value: String(root.chemicalSemanticsData.atom_count || 0) + " / " +
                           String(root.chemicalSemanticsData.residue_count || 0) + " / " +
                           String(root.chemicalSemanticsData.bond_count || 0)
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Formal / partial charges")
                    value: String(root.chemicalSemanticsData.formal_charge_present_count || 0) + " / " +
                           String(root.chemicalSemanticsData.partial_charge_present_count || 0)
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Isotopes / radicals")
                    value: String(root.chemicalSemanticsData.isotope_atom_count || 0) + " / " +
                           String(root.chemicalSemanticsData.radical_atom_count || 0)
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Topology version / schema")
                    value: String(root.chemicalSemanticsData.topology_version || 0) + " / " +
                           String(root.chemicalSemanticsData.chemical_semantics_schema_version || 0)
                }
                InfoRow {
                    width: parent.width
                    visible: root.chemicalPerceptionData.rule_set !== undefined
                    label: qsTr("Proposed bonds / ring atoms")
                    value: String(root.chemicalPerceptionData.proposed_bond_count || 0) + " / " +
                           String(root.chemicalPerceptionData.ring_atom_count || 0)
                }
                InfoRow {
                    width: parent.width
                    visible: root.chemicalPerceptionData.rule_set !== undefined
                    label: qsTr("Proposed residue classifications")
                    value: String(root.chemicalPerceptionData.proposed_residue_count || 0)
                }
                Text {
                    width: parent.width
                    visible: root.chemicalPerceptionError.length > 0
                    text: root.chemicalPerceptionError
                    color: "#ff9f9f"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: qsTr("Full machine-readable report: molshredder object chemistry --format json")
                    color: "#7fb9ec"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12
                    ToolbarButton {
                        label: qsTr("Propose chemistry")
                        action: function() { root.runChemicalPerception(false) }
                    }
                    ToolbarButton {
                        visible: ((root.chemicalPerceptionData.proposed_bond_count || 0) > 0 ||
                                  (root.chemicalPerceptionData.proposed_bond_order_change_count || 0) > 0 ||
                                  (root.chemicalPerceptionData.proposed_residue_count || 0) > 0) &&
                                 !root.chemicalPerceptionData.applied
                        label: qsTr("Apply proposal")
                        action: function() { root.runChemicalPerception(true) }
                    }
                    ToolbarButton {
                        label: qsTr("Close")
                        action: function() { chemicalSemanticsOverlay.visible = false }
                    }
                }
            }
        }
    }

    Rectangle {
        id: systemInfoOverlay
        objectName: "systemInfoOverlay"
        property string actionId: root.systemInformationMetadata.id
        property var entryAction: systemInformationAction
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 80

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(620, parent.width - 40)
            height: Math.min(510, parent.height - 40)
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 7

                Text {
                    text: qsTr("MolShredder System Information")
                    color: "#eef6ff"
                    font.pixelSize: 20
                    font.bold: true
                }

                Text {
                    objectName: "systemInfoSummary"
                    width: parent.width
                    text: root.systemInfoPanelError.length > 0
                          ? root.systemInfoPanelError
                          : qsTr("Build and graphics runtime reported by the canonical system info operation.")
                    color: root.systemInfoPanelError.length > 0 ? "#ff9f9f" : "#a9bdd5"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Rectangle { width: parent.width; height: 1; color: "#405270" }

                InfoRow {
                    width: parent.width
                    label: qsTr("Version / configuration")
                    value: root.systemInfoData.project_version === undefined
                           ? qsTr("Not reported")
                           : String(root.systemInfoData.project_version) + " / " +
                             String(root.systemInfoData.build_configuration)
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Platform")
                    value: root.systemInfoValue("platform", "operating_system") + " / " +
                           root.systemInfoValue("platform", "architecture")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Compiler")
                    value: root.systemInfoValue("toolchain", "compiler_id") + " " +
                           root.systemInfoValue("toolchain", "compiler_version") +
                           " · C++" + root.systemInfoValue("toolchain", "cxx_standard")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Python / HDF5 / netCDF")
                    value: root.systemInfoValue("dependencies", "python") + " / " +
                           root.systemInfoValue("dependencies", "hdf5") + " / " +
                           root.systemInfoValue("dependencies", "netcdf")
                }

                Rectangle { width: parent.width; height: 1; color: "#405270" }

                InfoRow {
                    width: parent.width
                    label: qsTr("Graphics status")
                    value: root.graphicsInfoValue("status")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("API / backend")
                    value: root.graphicsInfoValue("api") + " / " +
                           root.graphicsInfoValue("backend")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Device")
                    value: root.graphicsInfoValue("device_name") + " / " +
                           root.graphicsInfoValue("device_type")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Driver version")
                    value: root.graphicsInfoValue("driver_version")
                }
                InfoRow {
                    width: parent.width
                    label: qsTr("Failure reason")
                    value: root.graphicsInfoValue("failure_reason")
                }

                Text {
                    width: parent.width
                    text: qsTr("Full machine-readable report: molshredder system info --format json")
                    color: "#7fb9ec"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                ToolbarButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    label: qsTr("Close")
                    action: function() { systemInfoOverlay.visible = false }
                }
            }
        }
    }

    Rectangle {
        id: trajectoryImportOverlay
        objectName: "trajectoryImportOverlay"
        property string actionId: root.trajectoryAttachMetadata.id
        property var entryAction: trajectoryAttachAction
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 85

        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(520, parent.width - 40)
            height: 430
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14

                Text {
                    width: parent.width
                    text: qsTr("Trajectory Import Settings")
                    color: "#eef6ff"
                    font.pixelSize: 19
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: localization.translateUi(root.trajectoryAttachMetadata.status)
                    color: "#a9bdd5"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Text { text: qsTr("Coordinate unit"); color: "#d8e8fa" }
                ComboBox {
                    objectName: "trajectoryCoordinateUnitSelector"
                    width: parent.width
                    model: [qsTr("Ångström"), qsTr("Nanometer")]
                    currentIndex: root.trajectoryCoordinateUnit === "angstrom" ? 0 : 1
                    onActivated: function(index) {
                        root.trajectoryCoordinateUnit = index === 0 ? "angstrom" : "nanometer"
                    }
                }
                Text { text: qsTr("Atom mapping"); color: "#d8e8fa" }
                ComboBox {
                    objectName: "trajectoryMappingSelector"
                    width: parent.width
                    model: [qsTr("Exact stable IDs"), qsTr("Index order"),
                            qsTr("Explicit stable-ID map")]
                    currentIndex: root.trajectoryMapping === "exact" ? 0 :
                                  root.trajectoryMapping === "index" ? 1 : 2
                    onActivated: function(index) {
                        root.trajectoryMapping = index === 0 ? "exact" :
                                                 index === 1 ? "index" : "explicit"
                    }
                }
                TextField {
                    objectName: "trajectoryAtomMapInput"
                    width: parent.width
                    visible: root.trajectoryMapping === "explicit"
                    placeholderText: qsTr("Stable IDs, for example: 3,2,1")
                    text: root.trajectoryAtomMap
                    onTextChanged: root.trajectoryAtomMap = text
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12
                    ToolbarButton {
                        objectName: "trajectoryBrowseButton"
                        label: qsTr("Choose Trajectory…")
                        selected: true
                        action: function() {
                            trajectoryImportOverlay.visible = false
                            trajectoryDialog.open()
                        }
                    }
                    ToolbarButton {
                        label: qsTr("Close")
                        action: function() { trajectoryImportOverlay.visible = false }
                    }
                }
            }
        }
    }

    Rectangle {
        id: scriptTrustOverlay
        anchors.fill: parent
        visible: false
        color: "#99050812"
        z: 90

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(540, parent.width - 40)
            height: 315
            radius: 12
            color: "#f0101827"
            border.color: "#e4a24a"

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 13

                Text {
                    width: parent.width
                    text: qsTr("Run trusted Python code?")
                    color: "#eef6ff"
                    font.pixelSize: 19
                    font.bold: true
                }

                CheckBox {
                    id: isolatedScriptCheck
                    objectName: "isolatedScriptCheck"
                    checked: true
                    text: qsTr("Run in an isolated child process (recommended)")
                }

                Text {
                    width: parent.width
                    text: isolatedScriptCheck.checked
                          ? qsTr("Isolation protects the viewer state and enables hard cancellation, but it is not an operating-system sandbox.")
                          : qsTr("In-process scripts can call MolShredder operations and may leave partial viewer changes.")
                    color: "#e7b56f"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: qsTr("Python scripts run with the same permissions as MolShredder. Review the selected file before continuing.")
                    color: "#d8e8fa"
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: qsTr("Scripts can read files, use the network, start processes, and may leave partial viewer changes if they fail.")
                    color: "#e7b56f"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    ToolbarButton {
                        label: isolatedScriptCheck.checked
                               ? qsTr("Run isolated script") : qsTr("Run script")
                        selected: true
                        action: function() {
                            scriptTrustOverlay.visible = false
                            viewport.runPythonScript(root.pendingScriptUrl,
                                                     isolatedScriptCheck.checked)
                            root.pendingScriptUrl = ""
                        }
                    }

                    ToolbarButton {
                        label: qsTr("Cancel")
                        action: function() {
                            scriptTrustOverlay.visible = false
                            root.pendingScriptUrl = ""
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: viewport.scriptRunning
        color: "#99050812"
        z: 100

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.centerIn: parent
            width: 410
            height: 154
            radius: 12
            color: "#f0101827"
            border.color: "#69aef0"

            Column {
                anchors.centerIn: parent
                spacing: 16

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Python script is running")
                    color: "#eef6ff"
                    font.pixelSize: 18
                    font.bold: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Viewer editing is paused to protect the workspace.")
                    color: "#a9bdd5"
                    font.pixelSize: 13
                }

                ToolbarButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    label: qsTr("Request cancellation")
                    action: function() { viewport.cancelPythonScript() }
                }
            }
        }
    }
}
