# Representation and reference rendering

현재 구현은 lines, sticks, spheres, ribbon, cartoon의 backend-neutral representation packet과 headless CPU reference
renderer를 제공한다. 이 경계는 molecular semantics와 향후 GPU backend를 분리하고 동일 packet을
correctness test, picking test와 backend benchmark에 재사용하기 위한 foundation이다.

## Representation packet

`build_representation()`은 immutable `Topology`와 `CoordinateFrame`, atom별 color/radius, optional
selection mask 및 style을 받아 `RenderPacket`을 만든다. Packet은 topology version, frame index,
scene node ID, world-space primitive, bounds와 deterministic pick-ID mapping을 보존한다.

- `spheres`: present하고 selected인 atom당 sphere 하나를 생성한다.
- `lines`: visible bond를 midpoint에서 둘로 나눠 각 atom 색상의 screen-space line 두 개로 만든다.
- `sticks`: 같은 방식으로 atom 색상의 finite cylinder 두 개를 만든다.
- `ribbon`: selected residue의 Cα Catmull-Rom backbone을 일정 폭의 indexed solid mesh로 만든다.
- `cartoon`: 독립 STRIDE-method v0 state에 따라 helix/sheet/coil 폭과 sheet arrow profile을 적용한다.
- Bond는 양 끝 atom이 모두 present하고 selected일 때만 생성한다.
- 두 half primitive는 하나의 bond pick ID를 공유한다. Sphere pick은 stable `AtomIndex`, backbone
  triangle pick은 stable `ResidueIndex`를 가리킨다.
- Coordinate buffer가 float32여도 packet world coordinate는 float64로 승격한다.

Backbone frame은 carbonyl O 방향을 tangent에 직교 투영하고, O가 없으면 이전 frame을 parallel
transport한다. 인접 frame의 normal 부호를 맞춰 ribbon flip을 막는다. Chain/segment 변경, selection
공백, missing Cα 및 기본 4.5 Å보다 큰 Cα 간격에서는 mesh를 절단한다. 폭과 절단 거리는 Å style로
정의하고 nm coordinate frame에서는 0.1배로 변환한다. Packet provenance는 자동 assignment method와
exact-parity 상태를 기록한다.

Packet은 GPU buffer가 아니다. Backend adapter가 instance data를 upload하고 topology/frame version으로
resource reuse 여부를 판단한다. 현재는 multiple-bond offset, bond cap/join style, periodic copies,
object transform, per-representation transparency ordering, mesh cap, nucleic-acid cartoon과
coordinate-only incremental update를 포함하지 않는다.

## CPU reference renderer

Reference renderer는 perspective/orthographic camera로 analytic sphere, flat-capped cylinder와
indexed triangle mesh를 ray-intersect하고 line을 pixel width로 rasterize한다. Mesh는 barycentric
normal/color interpolation과 양면 shading을 사용한다. 출력은 RGBA8, camera-space depth와 pick ID
buffer이며 binary PPM writer와 FNV-1a image checksum을 제공한다. 고정된 64×64 scene checksum과
depth/picking regression, 10,000 atom packet scaling fixture가 CTest에 포함된다.

이 renderer의 목적은 correctness oracle과 headless image regression이다. 각 pixel이 모든 solid
primitive를 검사하므로 interactive/high-performance renderer가 아니며 GPU backend의 대체물이
아니다. Alpha는 현재 입력 순서에 따른 단순 source-over와 depth write를 사용해 order-independent
transparency를 제공하지 않는다. Line endpoint가 near/far plane을 가로지를 때 clipping하지 않고
그 line을 생략한다. PPM은 debug artifact이고 publication output 계약이 아니다.

## GPU prototype boundary

Qt Quick/QRhi 6.8.3을 exact-minor optional dependency로 선택하고 첫 viewport를 구현했다.
`MolecularViewport`는 GUI thread의 immutable packet snapshot을 scene-graph synchronize phase에서
render thread storage로 복사한다. QRhi resource와 draw는 render thread에만 존재하고 ShaderTools가
portable `.qsb`를 생성한다. Apple M5 Metal에서 indexed cartoon mesh, depth, directional shading,
2× HiDPI와 screenshot/timed smoke를 검증했다. 정적 unit sphere/cylinder mesh와 per-instance
position/radius/color buffer, clip-space pixel-width line quad도 같은 packet에서 실제 draw한다. Smoke는
576 mesh triangle, 38 line, 38 cylinder와 20 sphere instance의 pipeline·buffer·draw submission 이후에만
성공 marker를 출력한다.

Click picking은 visual MSAA target과 분리된 single-sample RGBA8/depth pass를 사용한다. Packet의 임의
64-bit ID를 frame-local dense 32-bit handle로 remap하고 mesh triangle은 pick pass에서 flat ID를 유지하도록
vertex를 펼친다. Sphere/cylinder/line instance는 같은 geometry buffer에 pick color만 추가한다. 클릭한
pixel을 1×1 transfer texture로 copy한 뒤 async readback하며 request/packet revision이 바뀐 stale result는
폐기한다. GUI thread는 해석된 atom/bond/residue target을 canonical `select` action의 `picked` named
selection으로 저장한다. Apple M5 Metal의 centered atom fixture가 실제 ID readback과 `index 1` selection을
검증했다. 다음 acceptance fixture는 다음과 같다.

- macOS Metal regression, Windows D3D11/12와 Linux Vulkan/OpenGL에서 instance drawing 가능 여부
- off-center/occlusion atom·bond·residue pick의 reference buffer 일치 및 Windows/Linux backend 검증
- 10k, 100k, 1M atom의 frame time, upload time, peak CPU/GPU memory
- camera interaction 중 update 범위와 trajectory coordinate-only update cost
- Qt/QML render-thread integration, binary size, build/installer 및 license impact

Qt의 public `QQuickRhiItem`은 Vulkan, Metal, D3D11/12와 OpenGL을 지원하지만 QRhi 계열은 제한된
compatibility guarantee와 `Qt::GuiPrivate` link를 요구한다. 따라서 Qt를 6.8.3으로 고정하고
minor upgrade마다 compile/render regression을 수행한다. `--redirected-render-smoke=<backend>`는
`QQuickRenderControl`과 표시되지 않는 `QQuickWindow`를 사용해 제품 `MolecularViewportRenderer`를
native window 없이 QRhi texture로 구동한다. 두 연속 frame의 320×240 RGBA8 readback이 byte-exact하고
pixel variation이 있으며 centered pick이 0이 아닌 GPU ID를 반환해야 성공한다. CI fixture는 3-atom PDB를
canonical load path로 열고 spheres representation을 적용한 뒤 4-frame DCD를 background attach하고 frame 1로
seek한다. 따라서 성공 marker는 `atoms=3 frames=4 frame=1`과 atom GPU pick까지 포함한다. 이 경로는 Metal에서
product pipeline과 installed bundle resource를 검증했으며 Windows hosted runner에서는 D3D11 WARP로 실행하도록
연결돼 있다. Redirected smoke는 실제 on-screen window, `Main.qml`, file dialog, keyboard/mouse
event delivery나 physical display presentation을 검증하지 않으므로 interactive platform checkpoint를
대체하지 않는다. 남은 backend 항목은 Windows/Linux remote evidence, hover/highlight/multi-selection과
large-instance benchmark다.

Desktop adapter는 synthetic packet 전용이 아니다. Qt file dialog 또는 `--open` path를 registry의
canonical `load` action으로 전달하고, toolbar representation은 canonical `show all` action으로
Workspace packet을 재생성한다. Core `scene::Camera`가 frame-sphere, orbit/pan/dolly를 계산하고 renderer는
동기화된 view/projection snapshot을 사용한다. Static scene의 첫 synchronize 이후 dirty upload와
projection을 `render()`에서 갱신해 animation이 없어도 첫 frame이 완전하도록 lifecycle regression을
고쳤다.

공식 참고 문서:

- [Qt QQuickRhiItem](https://doc.qt.io/qt-6/qquickrhiitem.html)
- [Qt QQuickRenderControl](https://doc.qt.io/qt-6.8/qquickrendercontrol.html)
- [Qt Quick scene graph renderer](https://doc.qt.io/qt-6.8/qtquick-visualcanvas-scenegraph-renderer.html)
- [Qt RHI texture item example](https://doc.qt.io/qt-6/qtquick-scenegraph-rhitextureitem-example.html)

## Validation contract

Invalid pointer/count, non-binary selection, non-finite/out-of-range color, non-positive geometry와
invalid render settings는 `invalid_argument`로 실패한다. Image regression은 동일 compiler/build
환경에서 byte-exact하게 유지한다. 서로 다른 architecture/compiler의 부동소수점 raster 결과가
검증되기 전 golden checksum을 cross-platform portability 계약으로 간주하지 않는다. GPU backend는
동일 픽셀을 강제하지 않고 silhouette, pick-ID와 정해진 color/depth tolerance를 별도로 정의한다.
