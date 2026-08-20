# Desktop viewport prototype

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

The representation toolbar uses the same registry, dispatcher, GUI action and
`Workspace` operations as the non-visual GUI adapter. Status shows the active
object, representation, atom count and primitive count. Left drag orbits,
right drag pans, the wheel dollies and double click reframes through the core
`scene::Camera` implementation. Toolbar presets send canonical `show
--replace true`; ordinary CLI/Python `show` remains additive.
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
The same panel exposes stereo enable, side-by-side/cross-eye/wall-eye/anaglyph
mode, eye swap, objective-distance shift percentage, angular scale and five
anaglyph color policies. QRhi draws adjacent modes with two viewports and
anaglyph through two offscreen eye targets plus a fullscreen color compositor.
macOS Metal captures verify both paths; Linux Vulkan/OpenGL and Windows D3D
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
seek track. Every control invokes the shared `traj` operations; QML does not
calculate playback transitions or decode frames.
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
analysis/sequence/representation panels, asynchronous file and cache-miss
decoding, transparency,
trajectory coordinate-only updates, culling/LOD and 10k/100k/1M GPU benchmarks
remain open.
Volume slice/direct-volume rendering, volume picking and a volume object panel are still open. The current contour
panel offers bounded step/midpoint controls but not editable numeric entry, color ramps or multiple contour rows.
The object panel itself still lacks hierarchy, rename/delete/reorder, transform,
solo/fixed/lock and per-representation child rows. The trajectory panel still
lacks editable first/last/stride, physical-time display, topology mapping UI,
multi-object synchronization, background rapid seek and frame-drop policy.
Windows D3D and Linux Vulkan/OpenGL have not yet been executed. The offscreen
texture item adds a render pass; an underlay/direct render-pass path should be
benchmarked before the final viewport composition strategy is fixed.

Qt is dynamically linked. Installer work must preserve LGPL/GPL notices,
corresponding-source and replacement/relinking rights and use artifact-derived
Qt SBOM data. See `DEPENDENCIES.md` and `THIRD_PARTY_NOTICES.md`.
