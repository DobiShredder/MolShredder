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

Open PDB/PDBx-mmCIF from the toolbar or at startup:

```bash
./build/desktop/molshredder_desktop.app/Contents/MacOS/molshredder_desktop \
  --open=structure.cif --representation=cartoon
```

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

## Current limits

This is still a prototype, not the production renderer. Hover, visual selection
highlight, additive/multi-pick gestures, hierarchical object editing,
trajectory/analysis panels, asynchronous file loading, transparency,
trajectory coordinate-only updates, culling/LOD and 10k/100k/1M GPU benchmarks
remain open.
The object panel itself still lacks hierarchy, rename/delete/reorder, transform,
solo/fixed/lock and per-representation child rows; trajectory and analysis
panels have not been added.
Windows D3D and Linux Vulkan/OpenGL have not yet been executed. The offscreen
texture item adds a render pass; an underlay/direct render-pass path should be
benchmarked before the final viewport composition strategy is fixed.

Qt is dynamically linked. Installer work must preserve LGPL/GPL notices,
corresponding-source and replacement/relinking rights and use artifact-derived
Qt SBOM data. See `DEPENDENCIES.md` and `THIRD_PARTY_NOTICES.md`.
