# Desktop UI action inventory

이 문서는 SEQ-122에서 완성한 `Main.qml`의 workflow 중심 menu, compact toolbar, context/panel과 command palette
projection을 분류하는 authoritative inventory다. `canonical`은 CLI/Python과 같은 typed operation을 호출하는 domain
action, `presentation`은 window/dialog/panel만 조작하는 UI action, `deferred`는 아직 일반 사용자 action으로 노출하면
안 되는 후속 기능을 뜻한다.

| Workflow | 현재 control family | 분류 | Canonical route 또는 UI 책임 | SEQ-122 상태 |
|---|---|---|---|---|
| File | Open structure/volume | canonical | `load`/`load batch`/`volume load` | `file.open` menu·toolbar·palette migration 완료 |
| File | Save active coordinates | canonical | `save` | `file.save` menu·toolbar·palette migration 완료 |
| File | Open/Save full session | canonical | `session load/save` | `file.open-session`/`file.save-session` menu·palette와 file dialog 완료; stable visible-panel metadata 복원 |
| Trajectory | Attach file | canonical | `traj load` | `trajectory.attach` menu·toolbar·palette·import-settings panel migration 완료 |
| Trajectory | Unit/mapping/stable-ID input | presentation parameter | `traj load` request parameter를 구성 | toolbar에서 import-settings panel로 이동 완료 |
| Trajectory | First/previous/play/next/last, mode, direction, FPS | canonical parameter family | `traj frame/play/mode/direction/speed` | `trajectory.play-pause`는 menu·panel·context·palette 공유, 나머지는 trajectory parameter panel 유지 |
| Trajectory | Cancel background load/seek | task lifecycle | shared cancellation token | 실행 중인 trajectory panel에만 나타나는 transient cancellation control로 유지 |
| Tools | Run local Python script | canonical | `script run-isolated` 기본/`script run` 선택, explicit trust confirmation | `tools.run-script` menu·palette와 isolated/in-process trust panel 완료; compact toolbar 제외 |
| Tools | Script output dismiss/cancel | presentation/task lifecycle | output panel close, shared cancellation token | script output/running overlay의 transient control로 유지 |
| Object | Activate/visibility/rename/reorder/delete | canonical parameter family | `object activate/visibility/rename/reorder/delete` | `object.panel` menu·panel·palette로 대상별 parameter editor를 노출 |
| Object | Chemical semantics inspection | canonical | `object chemistry` | `object.chemistry` menu·panel·palette와 normalized count overlay 완료 |
| Select | Select all / expression editor / GPU pick and named selection | canonical | `select` | `select.all`과 `select.expression` menu·context·palette, progressive expression panel 및 viewport pick 유지 |
| Represent | Lines/Sticks/Spheres/Ribbon/Cartoon | canonical | `show --replace true` | menu·palette migration 완료; Lines/Sticks/Spheres/Cartoon만 compact toolbar 유지 |
| Represent | Show/Hide/Show Only/Toggle Visibility | canonical | `show/hide/as/toggle` | 각 stable action을 Represent menu·context·palette에서 공유; compact toolbar 제외 |
| Analyze | Center/COM/distance/angle/dihedral/SASA/RDF/contacts/trajectory RMSD/RMSD matrix | canonical | `analyze`/`measure` registry operations | `analyze.open-panel` menu·toolbar·palette·panel entry, geometry PBC와 bounded SASA/RDF/matrix parameter 입력 완료 |
| Analyze | Result visibility/export/delete/detail | canonical | persistent result operations | Result-row context controls 유지 |
| Scene | Camera mouse/trackpad interaction | canonical | camera operation adapter | viewport interaction 유지 |
| Scene | Views/projection/stereo/clipping/named views | canonical | `view`/camera operations | `scene.views` menu·toolbar·palette·panel entry 완료 |
| Scene | Full-state named scene | canonical parameter family | `scene list/store/recall/delete/clear` | `scene.named-scenes` menu·palette가 Views의 progressive scene editor를 공유 |
| Scene | Typed movie timeline | canonical parameter family | `movie status/configure/keyframe/seek/play/pause/step/clear` | `scene.movie` menu·palette와 scene/trajectory-only keyframe panel 및 playback timer 완료 |
| Help | System information panel | canonical/presentation | `system info` 결과를 panel에 표시 | `help.system-information` menu·palette·panel entry 완료 |
| Represent | Render settings editor | canonical | `setting get/set/unset/reset` | `represent.settings` menu·palette·panel entry 완료 |
| Volume | Isosurface decrement/midpoint/increment | canonical parameter family | `volume isosurface` | volume load 뒤 자동 표시되는 numeric parameter panel에 유지 |
| Volume | X/Y/Z scalar slice 선택과 plane 이동 | canonical parameter family | `volume slice` | `represent.volume-slice` menu·panel·palette 공유; 같은 panel에서 axis/index parameter 구성 |
| Volume | Direct volume와 transfer preset | canonical parameter family | `volume render/hide`, `volume ramp set` | `represent.volume-render` menu·panel·palette 공유; background progress/cancel과 preset/step parameter 구성 |
| Represent | Molecular VDW/SAS surface | canonical parameter family | `surface show/hide` | menu·palette가 selection/kind/probe/spacing/resource budget panel을 공유 |
| UI | Language switch | presentation | Qt translator와 persisted setting | compact toolbar에서 Help > Language로 이동 완료 |
| UI | Open/Save/Trajectory/Script file dialogs | presentation parameter | 선택 경로를 해당 canonical action에 전달 | 공유 action trigger에서만 개방 |
| UI | Overlay close, confirmation arm, search focus | presentation | transient QML state only | domain action으로 위장하지 않음 |
| Editing | Atom coordinates/properties, residue properties, bond order | canonical parameter family | `edit atom-position/atom-properties/residue-properties/bond-order` | stable action별 Edit menu·property panel·palette, static-only enablement 완료 |
| Editing | Molecule builder와 undo/redo | canonical parameter family | `build molecule`, `edit undo/redo/history` | validated one-residue builder panel과 bounded history menu·palette 완료 |

## Migration invariant

- 동일 domain 기능의 menu, toolbar, context/panel과 command-palette entry는 하나의 stable action ID와 QML `Action`을
  공유한다.
- Workspace, selection, trajectory 또는 volume 요구조건은 action metadata에서 선언하고 모든 surface가 같은 enabled state와
  번역된 unavailable reason을 사용한다.
- QML은 dialog/panel 표시와 request parameter 수집만 담당하며 scientific calculation이나 Workspace mutation을 직접
  구현하지 않는다.
- `deferred` row는 구현과 canonical route가 준비될 때까지 menu나 palette에서 enabled action으로 노출하지 않는다.
- Palette에서 직접 실행하기에 필수 parameter/target이 부족한 family는 metadata의 `parameter_group` action이 해당 panel을
  열고, panel 내부 control이 typed request parameter를 구성한다. Transient close/search/confirm control은 searchable domain
  action으로 계산하지 않는다.
- 실행 중 task에만 존재하는 cancellation은 새 domain command가 아니라 기존 `TaskContext` cancellation token의 lifecycle
  control이다. 따라서 항상 실행 가능한 palette action으로 만들지 않고 해당 progress overlay에서만 노출한다.
