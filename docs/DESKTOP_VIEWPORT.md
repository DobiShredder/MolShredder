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

Open PDB/PDBx-mmCIF/BinaryCIF/PQR/MOL/SDF/MOL2/PSF/GRO/G96/VTF/XYZ or an
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
canonical `volume load` action as CLI/Python and create a typed volume scene object. The status reports grid
dimensions and voxel count, but no isosurface/slice/direct-volume geometry is rendered yet. Multi-record
SDF and multi-molecule MOL2 create one ordered Workspace object per record and activate the last one. Save invokes the same
canonical writer as CLI/Python and exports the active object as PDB, mmCIF, PQR, MOL, SDF, MOL2, PSF, GRO, G96 or XYZ based on the suffix;
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
A native OpenDX smoke opens a skewed 2×2×3 float64 scalar grid through the shared volume action, verifies the
Workspace grid geometry/value count and reaches the Metal event loop without replacing the current molecular render packet.
A native MRC smoke opens a little-endian, axis-permuted triclinic 2×2×3 float32 map with an extended header,
verifies logical dimensions/value count through the same Workspace action and reaches the Metal event loop.
A native PSF smoke loads a zero-frame topology without treating absent coordinates as a load error, clears stale scene
geometry, saves through the canonical GUI action and reloads the topology before clean shutdown. Attaching DCD/XTC/TRR/MDCRD/NetCDF/H5MD/RST7/LAMMPS/BINPOS
creates the selected representation through the ordinary shared `show` operation.
A native Amber smoke opens a four-atom PRMTOP, attaches an RST7 containing velocity/time/temperature/triclinic cell,
builds six half-bond stick instances through the canonical `show` operation and reaches the Metal render loop.
A second Amber smoke attaches a two-frame fixed-width MDCRD, uses PRMTOP `BOX_DIMENSIONS` for the cell angle and reaches
the same Metal render path.

## Current limits

This is still a prototype, not the production renderer. Hover, visual selection
highlight, additive/multi-pick gestures, hierarchical object editing,
analysis/sequence/representation panels, asynchronous file and cache-miss
decoding, transparency,
trajectory coordinate-only updates, culling/LOD and 10k/100k/1M GPU benchmarks
remain open.
Volume is currently data-only: isosurface extraction, slice/direct-volume rendering, volume picking and a volume
object panel are still open.
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
