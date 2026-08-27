# Surface and volume rendering

MolShredder의 surface/volume 기능은 scalar grid, geometry generation과 frontend action을 분리한다. 현재 구현은
OpenDX 및 MRC/CCP4의 immutable `VolumeGrid`, marching-tetrahedra isosurface, orthogonal scalar slice와
선택 기반 VDW/SAS molecular surface, post-classified QRhi direct volume을 제공한다.
PyMOL 호환 전체가 완료됐다는 의미는 아니며, 아래 표가 현재 구현 경계다.

| 기능군 | 현재 상태 | Canonical operation | 검증 |
|---|---|---|---|
| Scalar volume load/save | 구현 | `volume load/list/save` | OpenDX/MRC round-trip 및 CLI/Python parity |
| Map isosurface | 부분 구현 | `volume isosurface` | skewed grid geometry, cancellation, GUI/CLI/Python parity |
| Orthogonal scalar slice | 구현된 첫 slice | `volume slice` | X/Y/Z physical basis, deterministic image, volume pick, budget/cancel/parity |
| Molecular VDW/SAS surface | 첫 bounded 구현 | `surface show/hide` | analytic sphere, deterministic image/pick, budget/cancel 및 GUI/CLI/Python parity |
| Solvent-excluded surface(SES) | 미구현 | 없음 | probe-center SAS와 구분해 후속 범위로 유지 |
| Direct volume rendering | post-classified 첫 GPU 구현 | `volume render/hide` | QRhi R32F 3D texture, mono/stereo ray integration, GPU/CPU pick, texture budget/cancel/stale/parity |
| Versioned transfer function | builtin GUI와 custom CLI/Python 구현 | `volume ramp set/define/get` | builtin 5종, custom RGBA, LUT budget/cancel/parity |

## Orthogonal slice contract

`volume slice`는 active volume의 grid sample을 X, Y 또는 Z logical plane에서 읽고 실제 origin/delta basis로 위치를
계산한다. `index`는 0부터 시작한다. 전체 scalar range에서 minimum/maximum color 사이를 선형 보간하며 constant grid는
중간 색을 사용한다. 결과 packet provenance는 `orthogonal-grid-slice` algorithm version 1, axis/index,
`grid-sample-linear-color`, 요청 및 필요 memory byte를 기록한다.

```text
volume slice --axis z --index 1 \
  --minimum-color blue --maximum-color orange \
  --opacity 0.8 --memory-budget-bytes 67108864 --replace true
```

CLI와 Python은 동일한 operation envelope를 반환한다. Desktop의 Represent > Volume Slice, command palette와 하단
volume panel도 stable action ID `represent.volume-slice`를 통해 같은 operation을 호출한다. 패널은 Surface와 X/Y/Z
slice를 전환하고 현재 plane을 이동한다.

kernel은 primitive allocation 전에 vertex/triangle byte 수를 overflow-safe하게 계산한다. budget을 넘으면
`resource_exhausted`, 잘못된 axis index나 2×2 미만 plane은 `invalid_argument`, cancellation은 `cancelled`를 반환하며
기존 Workspace representation을 바꾸지 않는다. Slice triangle은 volume scene identity를 가진 pick target을 공유한다.

## Molecular surface contract

`surface show`는 현재 frame과 selection의 원자별 VDW radius union에 대해 signed-distance scalar field를 만들고
marching-tetrahedra level 0 mesh를 생성한다. `kind=vdw`는 probe radius 0, `kind=sas`는 원자 radius에 명시한
probe radius를 더한다. 따라서 현재 SAS는 solvent-accessible surface이며 re-entrant solvent-excluded surface가 아니다.
Å로 받은 radius/spacing은 frame의 Å/nm coordinate unit에 맞게 변환된다.

```text
surface show --kind sas --selection all --probe-radius 1.4 \
  --grid-spacing 0.7 --voxel-budget 8388608 \
  --memory-budget-bytes 536870912
```

Field/owner buffer와 marching-tetrahedra gradient, edge cache 및 mesh allocation은 하나의 explicit memory budget을
공유한다. 생성 실패·cancellation은 기존 packet을 보존한다. Surface triangle은 가장 가까운 선택 원자 pick으로
연결되며 현재 topology/coordinate revision이 바뀌면 stale surface를 제거한다. Desktop의 Represent > Molecular
Surface와 command palette는 selection, VDW/SAS, probe, spacing, voxel/memory budget을 받는 같은 parameter panel을
연다. CLI와 Python도 동일 operation/result provenance를 사용한다.

## Pinned capability gap

로컬의 `surface-map-volume-settings` acceptance inventory는 12개 group, 150개 capability다. 현재 직접 연결된 것은
map isosurface의 marching-tetrahedra triangle mode, orthogonal slice, bounded VDW/SAS와 post-classified direct-volume
subset이다. SES, carve/cavity, mesh/dots 45개, contour/gradient/isolevel 30개, slice 고급 동작 8개 및
pre-integrated/custom GUI editor를 포함한 direct-volume/ramp 나머지는 아직 support 완료로 표시하지 않는다.
Source/target multi-state와 camera-tracked/height slice도 후속 parity 범위다.

## Direct-volume preparation contract

`piecewise-linear-rgba` version 1 transfer function은 scalar가 strictly increasing하는 RGBA point 두 개 이상을
요구하며 endpoint 밖은 clamp한다. `density`, `fire`, `grayscale`, `ice`, `spectrum` builtin과 custom
`value,r,g,b,a;...` 정의를 지원한다. LUT 생성은 sample/memory budget과 cancellation을 검증한다.

`volume render`는 scalar grid를 normalized float texture payload와 transfer LUT로 준비한다. 현재 공개 mode는
`post-classified`뿐이며 요청한 `pre-integrated`는 silent fallback 없이 명시적으로 실패한다. Sampling step,
maximum step, LUT sample 및 texture byte budget은 result/provenance에 남는다. CPU reference renderer는 skewed grid의
world-to-logical inverse basis, trilinear sampling과 front-to-back alpha correction으로 fixed-camera image/pick oracle을
제공한다.

Desktop은 이 candidate를 GUI thread 밖의 bounded single-worker scheduler에서 만든다. Worker는 immutable grid/ramp만
읽고 owner thread의 `commit_ready`가 active volume ID, grid lease 및 presentation revision을 다시 검사한다. 새 요청은
이전 generation을 취소하며 stale completion, ramp/representation 변경, budget 실패와 shutdown cancellation은 기존
Workspace state를 보존한다. Volume panel은 진행률과 Cancel을 노출한다.

Commit된 payload는 QRhi R32F 3D texture와 RGBA32F LUT에 upload된다. Fragment shader는 mono camera뿐 아니라
side-by-side 및 composite stereo의 eye별 inverse view-projection/uniform binding으로 front-to-back ray march한다.
별도 GPU pick shader는 투명 threshold를 통과한 volume identity를 반환한다. Backend가 3D R32F/RGBA32F texture를
지원하지 않으면 monoscopic mesh로 조용히 대체하지 않고 renderer warning과 unavailable state를 유지한다.
Renderer status는 candidate lease와 함께 GUI thread에 전달되므로 이전 GPU upload completion이 새 volume 상태를
덮을 수 없고 panel에서 `ready`, `degraded`, `unavailable`, `failed`와 원인을 확인할 수 있다.
현재 geometry depth와 volume의 완전한 order-independent compositing, pre-integrated mode 및 point-editing GUI는
남은 제한이므로 전체 PyMOL/VMD direct-volume 호환으로 승격하지 않는다.
