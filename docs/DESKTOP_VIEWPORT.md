# Desktop viewport prototype

## Language

Desktop은 영어와 한국어 Qt Linguist catalog를 application resource에 포함한다. OS locale을 기본으로 사용하며
toolbar 또는 `--language=en|ko|system`으로 전환한다. 선택한 toolbar 언어는 application setting에 저장되고
runtime 전환 시 QML binding을 다시 번역한다. Operation ID, command, JSON field와 scientific provenance는
재현성을 위해 번역하지 않는다. 현재 typed operation에서 오는 일부 동적 status/error detail은 영어이며
완전한 frontend message mapping은 후속 UX 단계다. Catalog 추가 및 검증 절차는
[Localization](LOCALIZATION.md)에 둔다.

## Multi-file open

Open dialog은 여러 molecular structure를 동시에 선택할 수 있다. Desktop은 file stem으로 충돌 없는
기본 object name을 만들고 `load batch` canonical operation을 호출한다. 따라서 CLI와 Python과 같은
failure-atomic transaction을 사용하며 중간 input 오류 시 일부 object가 panel에 남지 않는다. Scalar
volume은 현재 한 번에 하나씩 연다. Batch progress는 공용 `TaskContext` status로 표시된다.

## Settings editor

Viewport toolbar의 **Settings**는 render setting editor를 연다. Setting name/value, scope와
atom/bond target을 입력하고 Set, Get, Unset, Reset을 수행할 수 있다. Editor는
Desktop 전용 state를 직접 바꾸지 않고 CLI/Python과 같은 canonical operation을 호출한다.
Atom/bond target은 topology의 64-bit stable ID이며 실패한 적용은 현재 scene packet을 유지한다.

## Analyze panel

Toolbar의 **Analyze**는 centroid/COM, atom distance, contacts와 trajectory RMSD 계산 및 persistent
result browser를 연다. 각 row에서 provenance detail, overlay show/hide, JSON/CSV export와 delete를
사용할 수 있다. Center marker와 distance endpoint/dashed line은 QRhi packet에 포함되고 label은 같은
world anchor를 Qt Quick screen-facing text로 투영한다. 계산과 lifecycle control은 Desktop-owned
shortcut이 아니라 CLI/Python과 동일한 canonical operation이다. 상세 계약은
[Persistent analysis results](ANALYSIS_RESULTS.md)에 둔다.

## Object panel

Object panel은 visibility, activation, inline rename, 위/아래 reorder와 delete를 제공한다.
Delete는 첫 클릭에서 3초 confirmation 상태(`?`)로 바뀌고 두 번째 클릭에서만
실행된다. 모든 control은 `object rename/delete/reorder` canonical operation을 호출하며
실패한 mutation은 panel order, active object와 render packet을 변경하지 않는다.

The optional desktop target uses Qt Quick 6.8.3 and `QQuickRhiItem`/QRhi. Qt
selects Metal on macOS, Direct3D on Windows, and Vulkan or OpenGL on Linux.
MolShredder pins the exact Qt minor because QRhi is exposed through
`Qt6::GuiPrivate` and does not promise source or binary compatibility across
minor versions.

```bash
conda activate molshredder
cmake --preset desktop
cmake --build --preset desktop
ctest --preset desktop -R desktop.gpu_smoke --output-on-failure
```

Local staging install과 대표 workflow smoke:

```bash
cmake --install build/desktop --prefix build/stage
./build/stage/molshredder_desktop.app/Contents/MacOS/molshredder_desktop \
  --daily-workflow-smoke --open=topology.pdb --trajectory=run.dcd \
  --trajectory-unit=angstrom --representation=spheres
```

Daily smoke는 open, representation, GPU pick을 통한 canonical selection, COM/distance result, rapid seek/play,
task cancellation, JSON/CSV result export와 current-frame structure save를 한 Workspace에서 검증한다. 이는 local
regression harness이며 novice/expert 사용성 연구나 signed installer 검증을 대신하지 않는다.

Open PDB/PDBx-mmCIF/BinaryCIF/PQR/MOL/SDF/MOL2/PSF/GRO/G96/VTF/XYZ/XMol or an
OpenDX or MRC2014/CCP4 scalar volume from the toolbar or at startup:

```bash
./build/desktop/molshredder_desktop.app/Contents/MacOS/molshredder_desktop \
  --open=structure.cif --representation=cartoon
```

After opening topology, attach DCD/XTC/TRR/MDCRD/Amber NetCDF/H5MD/RST7/LAMMPS dump/BINPOS with the Trajectory button or at
startup:

```bash
./build/desktop/molshredder_desktop.app/Contents/MacOS/molshredder_desktop \
  --open=topology.pdb --trajectory=run.lammpstrj \
  --trajectory-unit=angstrom --representation=sticks
```

The adjacent `Traj Å`/`Traj nm` toggle supplies the explicit coordinate unit
required by unitless formats such as LAMMPS text dump. Encoded-unit formats
retain their native reader semantics.

The `Map exact`/`Map index`/`Map IDs` control selects the mandatory topology
mapping policy. Exact is the default and succeeds only when the format carries
usable atom identity. Index order is an explicit user assertion. Map IDs shows
an inline stable-ID list; the GUI submits the current topology version with the
same canonical `traj load` action used by CLI and Python.

The representation toolbar uses the same registry, dispatcher, GUI action and
`Workspace` operations as the non-visual GUI adapter. Status shows the active
object, representation, atom count and primitive count. Left drag orbits,
right drag pans, the wheel dollies and double click reframes through the core
`scene::Camera` implementation. Toolbar presets send canonical `show
--replace true`; ordinary CLI/Python `show` remains additive.

The `Show`, `Hide`, `As`, and `Toggle` toolbar actions apply the selected
representation to `all` atoms through the same canonical actions exposed by CLI
and Python. `As` is selection-local in the core operation; the toolbar's `all`
scope makes it an object-wide replacement. Object visibility remains independent
and does not erase representation membership.
`Run Script` opens a local `.py` picker and an explicit arbitrary-code trust
confirmation. Approved scripts execute through the same `script run` operation
as CLI/Python and use the viewport's Registry/Workspace. Captured stdout/stderr
appears in a dismissible panel, and committed object/trajectory/scene changes
are synchronized after success or partial failure. The first implementation is
executed on a dedicated worker so the Qt event loop remains responsive. An
exclusive overlay pauses viewer editing and trajectory playback while that
worker owns the mutable Workspace. Cancellation is cooperative: the button
sets the shared task token, but arbitrary Python bytecode must return before
the post-execution checkpoint can report cancellation. Worker-process
isolation and argument editing remain follow-up work.
`System` opens a modal information panel generated from the exact JSON returned
by the canonical `system info` operation. It shows build configuration,
platform, compiler, required dependency versions and the live graphics
status/API/backend/device. A field which Qt or the platform adapter does not
report is labelled `Not reported`; the panel does not infer a driver version or
GPU identity. The full machine-readable form remains available through
`molshredder system info --format json`.
`Views` opens a camera workflow with a selection field and Center, Fit, Orient, Set pivot and Reset-all controls, plus an editable named-view name,
Store, Recall, Delete and Clear-all actions. The list is scrollable and ordered
by exact view name. Mouse orbit/pan/dolly and reset commit through canonical
`view set` or `view reset`; selection controls use `view center/zoom/orient/origin`, and named-view actions use `view store/recall/delete/clear`, so the same
operations are available from CLI and Python. A named view contains camera
state only, not object/representation state; full scenes remain a separate
capability. Recall uses a 0.35-second PyMOL-compatible eased transition while
the canonical operation commits the endpoint immediately. Clear-all requires
a second explicit confirmation click.
An object-reference field accepts `current`, an exact object name or ID. The
object-pivot action evaluates the current selection/state within that object,
while reset-object clears only its local transform. Three XYZ fields also set
an explicit camera origin. These controls invoke `view origin --object`,
`view reset --object` and `view origin --position` rather than maintaining
desktop-only state.
The scrollable Views workflow also exposes projection mode, degree field of
view and an explicit `Scale locked`/`Raw switch` policy. Opening the panel
reads the current camera rather than showing stale defaults. Apply invokes
canonical `view projection`; its default keeps target-plane scale continuous
while switching perspective and orthographic cameras.
The same panel exposes stereo enable, side-by-side/cross-eye/wall-eye/anaglyph,
row/column/checkerboard interleaving, eye swap, objective-distance shift
percentage, angular scale and five anaglyph color policies. QRhi draws adjacent
modes with two viewports. Composite modes render two full-size eye targets and
then apply color matrices or global device-pixel parity in a fullscreen pass;
eye swap can calibrate display phase. macOS Metal captures verify both paths;
Linux Vulkan/OpenGL and Windows D3D
captures remain required before cross-platform stereo parity is claimed.
The same panel has a multiline PyMOL 18-value field. `Export current` fills it
through canonical `view export-pymol` and selects the text for copying;
`Import values` calls `view import-pymol`, reports validation failure in the
normal status surface and animates the renderer to the committed camera only
after success. Mouse camera interaction cancels an active transition from its
current visual camera.
Selection framing starts with a State control. It cycles through `current`,
`all` and `explicit`; explicit mode reveals a one-based state input. Center,
Fit, Orient, Set pivot and selection-based clipping pass this value to the canonical
operation. All-state scans use the shared out-of-core frame source rather than
copying the trajectory into GUI-owned memory.
The Camera panel also exposes all seven clipping modes. Its mode button cycles
through atoms, slab, near, far, move, near-set and far-set; distance and
selection remain editable. Apply invokes canonical `view clip` and refreshes
the displayed near/far range through `view get-clip`.
The axis-navigation row cycles through camera-local X/Y/Z and provides signed
Move and Turn controls with one editable step. These buttons invoke canonical
`view move` and `view turn`; the same operations are callable from CLI and
Python, and turning preserves the separately stored model-origin pivot.
Left click performs an atom, bond or residue GPU pick. The result is committed
through the canonical `select` action as the static named selection `picked`
and shown in the lower-right feedback badge.
The Objects panel lists every loaded structure. Row activation and visibility
toggles use canonical object operations; the renderer composites every
effectively-visible object's stored representations instead of discarding
inactive objects.
The trajectory panel shows a zero-based current/last frame and provides first,
previous, play/pause, next, last, once/loop/rock, forward/reverse, FPS and a
seek track. Attach와 seek는 bounded C++ worker에서 candidate를 만들고 GUI owner thread에서 commit한다.
Panel은 progress와 cancel을 제공하며 rapid seek에서는 마지막 generation만 반영한다. CLI/Python의
synchronous path와 같은 Workspace plan/build/commit kernel을 사용하고 QML은 playback transition이나
frame decode를 구현하지 않는다.
The Open dialog also accepts PQR, MOL/SDF V2000, Tripos MOL2, CHARMM/NAMD PSF, Amber PRMTOP,
concatenated GROMACS GRO,
ordered GROMOS-96 G96, VMD VTF, plain multi-frame XYZ, ASCII OpenDX and MRC/CCP4 maps. Volume files use the same
canonical `volume load` action as CLI/Python, create a typed volume scene object and immediately render the scalar
range midpoint through canonical `volume isosurface`. The bottom contour panel moves the level by 5% of the range
or restores the midpoint; every control replaces the active volume mesh through the shared action. Multi-record
SDF and multi-molecule MOL2 create one ordered Workspace object per record and activate the last one. Save invokes the same
canonical writer as CLI/Python and exports the active object as PDB, mmCIF, PQR, MOL, SDF, MOL2, PSF, GRO, G96 or XYZ/XMol based on the suffix;
the status badge reports the semantic-loss item count. All-frame trajectory
export is currently available through console/Python and startup smoke rather
than a dedicated desktop dialog option.

`MOLSHREDDER_BUILD_DESKTOP` is off by default. Core, CLI, Python and headless
tests therefore remain buildable without Qt. The desktop preset enables it and
uses the active conda prefix as `CMAKE_PREFIX_PATH`.

## Thread and resource contract

- `MolecularViewport` belongs to the GUI thread and owns a `RenderPacket`
  snapshot plus a monotonically increasing revision.
- `QQuickRhiItemRenderer::synchronize()` runs while the GUI thread is blocked.
  It copies changed mesh vertices, line/cylinder/sphere instances, indices,
  bounds and view properties; the two objects do not otherwise share mutable
  state.
- QRhi buffers, bindings, pipelines, uploads and draw calls stay on the render
  thread. A changed QRhi device, render-target sample count or texture format
  invalidates the corresponding resources.
- GLSL 440 shaders are compiled by Qt ShaderTools into packaged `.qsb` files.
  Cartoon uses an indexed mesh. Spheres and cylinders reuse static unit meshes
  with per-instance position/radius/color data. Lines reuse a four-corner quad
  and expand it in clip space so width remains measured in device pixels.
- Solids share deterministic directional diffuse lighting. Lines are submitted
  last with `LessOrEqual` depth comparison and no depth write, avoiding
  coplanar z-fighting without drawing through foreground solids.
- Picking uses a separate single-sample RGBA8/depth pass only after a click.
  Arbitrary 64-bit packet IDs are remapped to dense 32-bit frame-local handles,
  so the shader does not truncate semantic identity. The selected pixel is
  copied to a 1×1 transfer texture and read back asynchronously. Request and
  packet revisions discard stale results after a later click or packet change.
- A few bounded render pulses follow a click because an otherwise static Qt
  Quick scene may not submit the readback completion until a later frame. QRhi
  resources remain on the render thread; only the decoded semantic ID is queued
  back to the GUI thread.
- A precise 16 ms GUI timer measures elapsed wall time and sends only that
  duration to canonical `traj tick`. The core fractional clock owns FPS,
  catch-up and once/loop/rock transitions. A tick that does not cross a frame
  boundary does not replace or upload the composite render packet.
- Trajectory attach/seek uses a two-worker, two-entry bounded queue and a
  fixed memory reservation budget. Progress is marshalled to the GUI thread;
  cancellation and stale generations cannot publish Workspace state.
- Stable trajectory packet topology updates only dynamic vertex/instance
  buffers. Primitive cardinality, pick identity or material changes select a
  deterministic full-buffer rebuild fallback.
- Trusted Python scripts run on one owned worker thread. The GUI blocks
  Workspace editing, stops playback before dispatch, and only rebuilds the
  immutable render packet after the completion is queued back to the GUI
  thread. Viewport destruction requests cancellation and joins the worker.
- Scene-graph initialization snapshots the selected `QSGRendererInterface`
  API and QRhi backend/device identity into the shared, Qt-independent runtime
  diagnostics service. `system info` therefore reports the actual Metal,
  Direct3D, Vulkan or OpenGL selection from the Desktop Registry; invalidation
  and scene-graph errors replace stale readiness with explicit lifecycle state.

The current demo combines the real `build_representation()` cartoon, lines,
sticks and spheres paths into one valid packet. The macOS arm64 smoke selected
Apple M5 Metal, created a 2160×1440 device-pixel target at DPR 2 and rendered
576 mesh triangles, 38 line instances, 38 cylinder instances and 20 sphere
instances. The smoke only reports readiness after all four pipelines and their
buffers exist and draw submission has occurred. A captured screenshot was
visually checked for geometry, depth and solid shading.

Separate startup smoke tests load a synthetic multi-model PDB as sticks and a
PDBx/mmCIF as spheres. They verify registry-backed load/show, packet replacement
and core camera orbit/pan/dolly/reset before the Metal render loop exits.
An additional protein-scale startup smoke loads the unmodified RCSB PDB 1UBQ
fixture as spheres and requires all 660 coordinate records (602 protein atoms and
58 waters) to reach the Desktop camera/render packet path. Its source, CC0 data
license, citation and SHA-256 are recorded in `tests/io/fixtures/README.md`.
A single-atom PDB smoke renders a centered sphere, reads back its GPU ID,
resolves `AtomIndex(0)`, and verifies the shared Workspace receives
`picked = index 1`. The lower-right selection badge was also checked in an
actual Metal screenshot.
A two-object smoke verifies four visible sphere instances before interaction,
then activates object 1, hides object 2, and requires the composite packet to
contain only object 1's sphere. A screenshot confirms active/hidden panel
styling and the visible scene result.
A generated four-frame, three-atom DCD smoke attaches to the matching PDB,
checks deterministic loop wrap and rock direction reversal, then requires an
actual Qt timer to change the frame before clean shutdown. A Metal screenshot
was checked for molecular geometry, object state, timeline controls and
non-overlapping status/selection badges.
A native PQR smoke loads partial-charge/radius properties, uses the PQR radius
for sphere instances, exports one frame, reloads the result and checks the
electrostatics columns before clean shutdown.
A native PDB save smoke loads two models with altLoc/insertion/segment identity, a triclinic cell and `CONECT`,
exports both models through the canonical GUI action and reloads the result before clean shutdown.
A native SDF smoke loads two records atomically, renders the active record as sticks, exports it as V2000,
reloads the output and checks atom/frame counts before clean shutdown.
A native MOL2 smoke loads two molecules atomically, renders the active molecule as sticks, exports it with
explicit SYBYL types and reloads the output before clean shutdown.
A native GRO smoke loads two concatenated frames with velocity/time/cell metadata, renders the first frame as sticks,
exports it through the canonical GUI `save` action and reloads the output before clean shutdown.
A native G96 smoke loads two ordered block frames, renders sticks, exports all frames through the canonical GUI `save`
action and reloads the output before clean shutdown.
A native VTF Open smoke loads combined topology, chain bonds, a triclinic cell and sparse inherited trajectory frames
through the same GUI action/Workspace operation used by CLI and Python, then renders sticks with the Metal backend.
A native OpenDX smoke opens a skewed 2×2×3 float64 scalar grid through the shared volume action, creates a midpoint
isosurface and submits its indexed mesh through the Metal pipeline.
A native MRC smoke opens a little-endian, axis-permuted triclinic 2×2×3 float32 map with an extended header,
verifies logical dimensions/value count, creates its midpoint isosurface through the same Workspace action and
reaches the Metal event loop.
A native PSF smoke loads a zero-frame topology without treating absent coordinates as a load error, clears stale scene
geometry, saves through the canonical GUI action and reloads the topology before clean shutdown. Attaching DCD/XTC/TRR/MDCRD/NetCDF/H5MD/RST7/LAMMPS/BINPOS
creates the selected representation through the ordinary shared `show` operation.
A native Amber smoke opens a four-atom PRMTOP, attaches an RST7 containing velocity/time/temperature/triclinic cell,
builds six half-bond stick instances through the canonical `show` operation and reaches the Metal render loop.
A desktop Python smoke runs a trusted local script asynchronously through the
GUI adapter, loads a three-atom object into the viewport Workspace, rebuilds
the scene and verifies captured stdout before the Metal render loop exits. A
second smoke requests cancellation while Python sleeps, waits for its return,
and verifies the post-execution cancelled result and captured pre-cancel output.
A second Amber smoke attaches a two-frame fixed-width MDCRD, uses PRMTOP `BOX_DIMENSIONS` for the cell angle and reaches
the same Metal render path.

## Current limits

This is still a prototype, not the production renderer. Hover, visual selection
highlight, additive/multi-pick gestures, hierarchical object editing,
analysis/sequence/representation panels, general asynchronous structure/volume
file decoding, transparency, culling/LOD and 10k/100k/1M GPU benchmarks
remain open.
Volume slice/direct-volume rendering, volume picking and a volume object panel are still open. The current contour
panel offers bounded step/midpoint controls but not editable numeric entry, color ramps or multiple contour rows.
The object panel itself still lacks hierarchy, transform, solo/fixed/lock and
per-representation child rows. The trajectory panel still lacks editable
first/last/stride, physical-time display and multi-object synchronization.
The manual cross-platform checkpoint workflow is configured to reject a null or unexpected backend and to run the
installed daily workflow on Metal, Linux Vulkan/OpenGL and Windows Direct3D. Windows D3D and Linux Vulkan/OpenGL
have not yet been executed, so configuration alone is not support evidence. The offscreen
texture item adds a render pass; an underlay/direct render-pass path should be
benchmarked before the final viewport composition strategy is fixed.

Qt is dynamically linked. Installer work must preserve LGPL/GPL notices,
corresponding-source and replacement/relinking rights and use artifact-derived
Qt SBOM data. See `DEPENDENCIES.md` and `THIRD_PARTY_NOTICES.md`.
