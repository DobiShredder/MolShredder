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
- translation, normalized quaternion rotation, positive nonzero scale의 local transform
- system node의 immutable `MolecularSystem` 또는 volume node의 immutable `VolumeGrid` ownership

Sibling name은 중복될 수 있다. Identity는 name이 아니라 `NodeId`다. Reparent는 명시적 child
position을 지원하고 cycle/root mutation/out-of-range insertion을 거부한다. Subtree 삭제는 해당
node를 selection에서도 제거한다. Multi-selection은 정렬된 ID set으로 snapshot에 저장한다.
`effectively_visible()`은 모든 ancestor visibility를 반영하고 `world_transform()`은 root부터 local
matrix를 합성한다.

현재 snapshot에는 representation, measurement와 per-frame transform node가 없다. Volume payload node는
있지만 isosurface/slice/direct-volume representation component는 아직 없다.
Analysis presenter의 point/atom-anchor marker는 view-model artifact이며 아직 Scene node가 아니다.
해당 payload는 각 vertical slice에서 새 `NodeKind` 또는 별도 component store로 추가하며 stable
ID와 snapshot lifetime을 유지한다.

## Coordinate and camera convention

Scene math는 right-handed world coordinate를 사용한다. Matrix storage는 column-major이고 column
vector에 작용한다. 기본 camera local axis는 right `+X`, up `+Y`, forward `-Z`다. Camera
orientation은 camera-local vector를 world vector로 회전시키는 unit quaternion이다.

Camera는 mutable UI object가 아니라 validated value snapshot이다. 주요 parameter는 target,
orientation, target distance, perspective/orthographic mode, vertical field of view, orthographic
height, aspect ratio와 near/far clip이다. `view_matrix()`는 world point를 camera space로 옮기지만
projection matrix는 만들지 않는다. Vulkan/OpenGL 등의 clip-space depth와 Y convention은 후속
renderer adapter가 명시적으로 결정한다.

Interaction method는 새 validated Camera 값을 반환한다.

- `with_viewport`: positive pixel extent로 aspect ratio 갱신
- `orbit_pixels`: world-up yaw와 yaw 후 camera-right pitch; target/distance 보존
- `pan_pixels`: target depth의 vertical world span을 viewport height로 나눈 pixel scale 사용
- `dolly`: exponential scale과 configurable positive min/max clamp
- `frame_sphere`: aspect/projection을 고려해 bounding sphere center와 padded extent framing

`pan_pixels(dx, dy)`에서 양의 `dx`는 target을 camera-left로, 양의 `dy`는 camera-up으로 이동시켜
dragged content가 pointer 방향을 따르게 한다. Perspective dolly는 distance를, orthographic
dolly는 view height를 변경한다. Interaction sensitivity는 physical mouse event에 결합하지 않고
`CameraInteractionConfig`로 전달하므로 mouse, trackpad와 touch adapter가 각 입력 단위를
정규화할 수 있다.

## Validation and lifetime

Scene transform은 finite translation, normalized quaternion와 positive finite scale만 받는다.
Camera는 finite target/delta, normalized orientation, positive distance/span/aspect, `0 < fov < pi`
및 `0 < near < far`를 요구한다. Invalid mutation은 stable `operation::Error`를 반환하며 이전
snapshot은 변하지 않는다.

Scene snapshot과 molecular system은 shared immutable ownership을 사용한다. Render/analysis
worker는 snapshot lease를 잡고 있는 동안 hierarchy, system 및 transform이 바뀌지 않는다고
가정할 수 있다. GUI edit는 새 snapshot을 만들고 application undo service가 이전 snapshot을
보관한다.
