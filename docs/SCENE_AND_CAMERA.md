# Scene object tree and camera

이 문서는 renderer와 UI toolkit에 독립적인 foundation scene contract를 설명한다. Public API는
`include/molshredder/scene/`에 있으며 Qt/QML object tree와 renderer backend는 이 API의 consumer가
된다.

## Object tree

```text
Scene (immutable versioned snapshot)
└── root, NodeId 0
    ├── group
    │   └── molecular_system ── shared_ptr<const MolecularSystem>
    ├── volume ── shared_ptr<const VolumeGrid>
    └── group
```

`SceneBuilder`가 hierarchy mutation을 수행하고 `build()`가 immutable snapshot을 만든다. 첫
snapshot version은 1이고 기존 snapshot에서 `SceneBuilder::from()`으로 만든 다음 snapshot은
version이 증가한다. `NodeId`는 lineage 전체에서 단조 증가하며 삭제된 ID를 재사용하지 않는다.
따라서 GUI selection, undo record와 future session reference는 node name 대신 ID를 사용한다.

Node는 다음 상태를 가진다.

- `root`, `group`, `molecular_system`, `volume` kind
- parent와 순서가 보존되는 child list
- 사용자-facing name과 local visibility
- translation, normalized quaternion rotation, positive nonzero scale와 local-space pivot의 local transform
- system node의 immutable `MolecularSystem` 또는 volume node의 immutable `VolumeGrid` ownership

Sibling name은 중복될 수 있다. Identity는 name이 아니라 `NodeId`다. Reparent는 명시적 child
position을 지원하고 cycle/root mutation/out-of-range insertion을 거부한다. Subtree 삭제는 해당
node를 selection에서도 제거한다. Multi-selection은 정렬된 ID set으로 snapshot에 저장한다.
`effectively_visible()`은 모든 ancestor visibility를 반영하고 `world_transform()`은 root부터 local
matrix를 합성한다.

현재 snapshot에는 representation, measurement와 per-frame transform node가 없다. Volume payload node의
isosurface packet은 Workspace가 소유하며 slice/direct-volume representation component는 아직 없다.
Analysis presenter의 point/atom-anchor marker는 view-model artifact이며 아직 Scene node가 아니다.
해당 payload는 각 vertical slice에서 새 `NodeKind` 또는 별도 component store로 추가하며 stable
ID와 snapshot lifetime을 유지한다.

## Coordinate and camera convention

Scene math는 right-handed world coordinate를 사용한다. Matrix storage는 column-major이고 column
vector에 작용한다. 기본 camera local axis는 right `+X`, up `+Y`, forward `-Z`다. Camera
orientation은 camera-local vector를 world vector로 회전시키는 unit quaternion이다.

Camera는 mutable UI object가 아니라 validated value snapshot이다. 주요 parameter는 target, 별도의
model-space rotation origin, orientation, target distance, perspective/orthographic mode, vertical field of view, orthographic
height, aspect ratio와 near/far clip이다. `view_matrix()`는 world point를 camera space로 옮기지만
projection matrix는 만들지 않는다. Vulkan/OpenGL 등의 clip-space depth와 Y convention은 후속
renderer adapter가 명시적으로 결정한다.

Interaction method는 새 validated Camera 값을 반환한다.

- `with_viewport`: positive pixel extent로 aspect ratio 갱신
- `orbit_pixels`: world-up yaw와 yaw 후 camera-right pitch; target/distance 보존
- `pan_pixels`: target depth의 vertical world span을 viewport height로 나눈 pixel scale 사용
- `dolly`: exponential scale과 configurable positive min/max clamp
- `frame_sphere`: aspect/projection을 고려해 bounding sphere center와 padded extent framing
- `frame_box`: 현재 camera-local 축으로 표현한 half extent를 perspective/orthographic viewport와 clip에 맞춤

`pan_pixels(dx, dy)`에서 양의 `dx`는 target을 camera-left로, 양의 `dy`는 camera-up으로 이동시켜
dragged content가 pointer 방향을 따르게 한다. Perspective dolly는 distance를, orthographic
dolly는 view height를 변경한다. Interaction sensitivity는 physical mouse event에 결합하지 않고
`CameraInteractionConfig`로 전달하므로 mouse, trackpad와 touch adapter가 각 입력 단위를
정규화할 수 있다. Pan과 frame은 target과 model origin을 함께 옮기며 orbit은 target 주위에서 camera
orientation을 바꾼다.

## Application view state와 named views

Validated `Camera` snapshot의 application source of truth는 `Workspace`다. `view get/set`은 snapshot 전체를 typed
result로 반환하며 `set`은 전달된 field만 현재 값 위에 적용한 다음 전체 invariant를 검증한다. Desktop orbit/pan/dolly는
`view set`, selection framing/orient/pivot/reset은 `view center/zoom/orient/origin/reset`으로 commit하므로 CLI, GUI와 Python이 서로 다른 camera model을
갖지 않는다. Renderer는 GUI-thread mirror의 revisioned immutable value만 읽으며 background script가 view operation을
실행한 경우 GUI completion에서 mirror를 동기화한다.

Center, zoom과 origin은 active object의 state-scoped selection extent를 공유한다. `current`, `all`과
1-based explicit state를 지원하고 `-1`/`0`은 PyMOL-compatible current/all alias다. 기본 `current`는 기존
MolShredder workflow를 보존한다. All-state는 known frame count를 요구하고 `CoordinateSource`/`FrameCache`에서
out-of-core two-pass bounds/radius scan을 수행하며 `TaskContext` cancellation/progress를 지원한다. Missing atom
coordinate는 frame별 count와 함께 제외되고 present coordinate가 하나도 없으면 failure-atomic
`invalid_selection`이다. Center는 AABB midpoint를
target으로 사용하고 pivot 이동 여부를 선택할 수 있다. Zoom은 기본적으로 최대 AABB 축의 절반, `complete=true`이면
midpoint에서 가장 먼 atom center를 radius로 사용하며 absolute buffer와 molecular-scale floor를 적용한다. 현재 floor는
atom radius property를 반영하지 않는 독립 근사이므로 exact PyMOL `MAX_VDW` parity로 주장하지 않는다. Reset은 default
camera state와 identity orientation을 만들고 effectively-visible molecule current frame 및 volume grid corner의 합집합을
frame한다. Live aspect ratio는 유지한다.

`view origin --position x,y,z`는 selection scan 없이 camera model origin을 설정한다. `--object current|NAME|ID`는
대상 molecular node의 local pivot을 설정하며, position이 없으면 그 object에 selection과 state scope를 평가해 AABB
midpoint를 사용한다. 기존 transform이 identity가 아니어도 pivot 변경 전후의 affine origin이 같도록 translation을
보정한다. `view reset --object current|all|NAME|ID`는 대상 node transform만 identity로 되돌리고 camera는 보존한다.
현재 renderer packet은 object-local geometry와 camera를 직접 사용하므로 이 slice는 object transform state와 shared
operation 계약까지 구현한 것이다. Scene world transform을 GPU draw와 object animation에 적용하는 완전한 PyMOL TTT
parity는 아직 완료로 주장하지 않는다.

`view orient`는 선택된 present coordinate를 전부 동일 가중치로 취급하고 Welford online covariance를 계산한다.
Eigenvector는 variance 내림차순으로 camera right/up/backward에 배치하고 right-handed basis를 보장한다. 부호 조합은
현재 camera basis와 dot 합이 가장 큰 방향을 고른다. 두 eigenvalue가 수치적으로 같으면 해당 degenerate subspace에
현재 camera 축을 투영하며, 세 값이 모두 같으면 orientation을 유지한다. 두 번째 streaming pass에서 principal-axis
AABB와 world AABB radius를 계산해 `frame_box`로 맞춘다. 따라서 all-state trajectory도 좌표 전체를 materialize하지
않고 bounded frame cache를 사용하며 cancellation 전에는 camera를 commit하지 않는다.

`view clip`은 PyMOL user-facing seven-mode surface를 hyphenated canonical mode로 제공한다: relative `near/far/move`,
thickness `slab`, projected-depth `atoms`, absolute `near-set/far-set`. Atom fit은 world-axis extent를 재사용하지 않고
각 present coordinate에 `dot(point-camera_position, camera_forward)`를 적용한다. Selection slab만 PyMOL처럼 world AABB
midpoint의 projected depth를 사용한다. MolShredder는 renderer invariant인 `0 < near < far`를 유지하므로 PyMOL이 내부
minimum slab으로 보정할 수 있는 zero/negative/crossing 요청을 명시적 error로 반환한다. `view get-clip`은 같은
Workspace snapshot의 near/far/thickness를 조회한다.
Selection-based slab/atoms clipping도 같은 state scope kernel을 사용한다.

`view move`와 `view turn`의 x/y/z는 현재 camera-local axis다. Move x/y는 model origin을 변경하지
않고 target을 local right/up의 반대 방향으로 이동한다. Move z는 distance와 clip plane을 같은
거리만큼 줄이며 orthographic height의 encoded field-of-view span을 비율로 보존한다. Turn은 local
quaternion을 현재 orientation의 오른쪽에 합성하고 target/eye를 model origin 주위로 회전한다.
따라서 off-axis PyMOL view에서도 pivot이 보존된다. 두 operation은 `Workspace::set_camera`로 최종
validated snapshot만 commit하며 invalid distance, clip crossing과 non-finite input은 failure-atomic이다.

`view projection`은 PyMOL의 global `orthoscopic` boolean과 degree `field_of_view`를 한 곳에서 찾을 수 있게 한
convenience operation이다. 기본 scale-preserving policy는 현재 target plane의 vertical world span을 먼저 고정한다.
Orthographic target은 그 값을 `orthographic_height`로 사용하고 perspective target은 requested/latent FOV로 필요한
distance를 역산한다. Distance가 바뀌면 near/far를 같은 비율로 조정해 normalized clipping을 보존한다. Raw mode는
inactive camera parameter를 자동 조정하지 않는다. 모든 계산은 새 Camera validation 성공 후 한 번에 commit된다.

Stereo도 `Workspace::stereo()`의 validated value가 source of truth다. `shift_percent`는 objective distance에 대한
각 eye camera의 수평 이동 백분율이고 `angle_scale`은 natural convergence angle의 배율이다. Core는 physical
left/right camera와 adjacent viewport presentation order를 분리해 crosseye와 explicit eye swap을 결정적으로 합성한다.
QRhi renderer는 side-by-side/crosseye/walleye에서 두 uniform buffer와 두 viewport를 실제 draw하고 picking은
full-size monoscopic camera로 분리한다. Anaglyph는 두 full-size offscreen eye target을 독립 depth clear한 뒤
`true`, `gray`, `color`, `half_color`, `optimized` matrix 중 하나로 fullscreen 합성한다. Quad-buffer,
interleaved와 OpenVR는 구현 전 명시적 unsupported다.

Named view는 name→`CameraParameters`의 정렬된 Workspace map이다. Store는 같은 name을 atomic하게 교체하며 recall은
stored snapshot 전체를 검증·적용한다. Delete/clear 실패나 invalid snapshot은 현재 camera와 inventory를 바꾸지 않는다.
Named view에는 object visibility, representation, frame 또는 message가 포함되지 않는다. 그것들은 multi-channel scene
capability에서 별도로 결합한다. Canonical `view set/store/recall` invocation을 session journal에 넣으면 replay로 동일
camera와 inventory를 재구성할 수 있다.

Recall과 18-value import는 optional `duration`과 `hand`를 받는다. Workspace source of truth에는 target endpoint를
즉시 commit하고 typed result에 start/end snapshot을 함께 반환한다. Core의 `interpolate_pymol_camera`는 PyMOL의
symmetric power-2 easing, axis-angle rotation, camera/model origin과 clip 선형 보간, perspective/orthographic
FOV span 보간 및 180도 부근 handedness 선택을 독립적으로 재현한다. Desktop은 16 ms precise timer로 mirror만
보간하며 종료 시 committed Workspace camera에 정확히 동기화한다. 새 mouse camera action이나 background state sync는
진행 중 animation을 취소한다. Session replay는 duration을 수면이나 timer로 재생하지 않고 endpoint를 결정적으로 적용한다.
Session producer overload는 저장 직전 current Workspace camera와 stereo를 full `view set`/`stereo set` pair로 append하거나
기존 trailing pair를 교체한다. 이 snapshot은 object visibility, representation, measurement, frame과 named-view inventory를
포함하지 않으며 full scene 저장과 별도다.

## PyMOL public 18-value view compatibility

`view export-pymol/import-pymol`은 PyMOL `cmd.get_view()`/`cmd.set_view()`의 공개 18-value layout과 변환한다.
0–8은 column-major model→camera rotation, 9–11은 camera-space rotation origin, 12–14는 model-space
origin, 15–16은 clip distance, 17은 projection sign과 degree FOV다. MolShredder의 별도 `model_origin`은
off-axis translation을 잃지 않고 왕복하기 위해 camera snapshot에 포함된다.

Perspective에서는 degree FOV를 radian으로 변환한다. Orthographic에서는 PyMOL 렌더링과 같이
`height = 2 × max(1e-4, -camera_z) × tan(FOV/2)`를 사용한다. 반대로 MolShredder native orthographic
height를 export할 때는 화면 span을 유지하는 effective FOV를 계산한다. 9-decimal single-precision 출력도
받을 수 있도록 회전은 tolerance 안에서 right-handed orthonormal인지 확인한 뒤 재직교화한다. Invalid matrix,
non-finite value, 잘못된 clip은 Workspace를 바꾸지 않는다.

행동 기준은 Open-Source PyMOL 3.1.0 commit
`f51e58f6b08308c41c85b9d12a23231f49ca325a`의 `modules/pymol/viewing.py`, `layer1/Scene.cpp`와
`layer1/SceneRender.cpp`, animation 기준은 `layer1/Scene.cpp`와 `layer1/View.cpp`다. 구현은 공개 동작 계약을
독립적으로 재구현했으며 PyMOL source를 복사하지 않았다.

## Validation and lifetime

Scene transform은 finite translation, normalized quaternion와 positive finite scale만 받는다.
Camera는 finite target/delta, normalized orientation, positive distance/span/aspect, `0 < fov < pi`
및 `0 < near < far`를 요구한다. Invalid mutation은 stable `operation::Error`를 반환하며 이전
snapshot은 변하지 않는다.

Scene snapshot과 molecular system은 shared immutable ownership을 사용한다. Render/analysis
worker는 snapshot lease를 잡고 있는 동안 hierarchy, system 및 transform이 바뀌지 않는다고
가정할 수 있다. GUI edit는 새 snapshot을 만들고 application undo service가 이전 snapshot을
보관한다.
