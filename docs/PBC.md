# Periodic boundary condition foundation

Public PBC API는 `include/molshredder/trajectory/pbc.hpp`에 있다. 이 foundation은
orthorhombic 및 triclinic unit cell의 좌표 변환, exact minimum image, 원자 단위 wrap과
trajectory continuity unwrap을 제공한다. 입력과 출력의 coordinate unit은 바꾸지 않는다.

## Cell과 minimum-image 의미

`UnitCell::a`, `b`, `c`는 Cartesian lattice basis이고 fractional coordinate `f`는
`r = a*f.x + b*f.y + c*f.z`로 해석한다. 변환과 PBC operation은 finite한 right-handed
non-degenerate cell만 받는다.

`minimum_image(cell, displacement)`는 단순히 fractional component를 각각 반올림하지 않는다.
그 방식은 심하게 기울어진 triclinic cell에서 가장 짧은 이미지를 놓칠 수 있다. 구현은 cell
basis의 QR decomposition과 bounded sphere search로 3차원 closest lattice vector를 찾고,
minimum displacement와 적용한 integer lattice shift를 함께 반환한다. 정확히 같은 거리의
half-cell image가 여러 개면 lattice shift의 lexicographic order로 결정해 orthorhombic 축의
boundary를 `[-L/2, L/2)`로 고정한다.

수치적으로 지나치게 ill-conditioned인 cell이나 백만 search node를 초과하는 pathological
입력은 임의의 근사 결과를 반환하지 않고 stable error로 거부한다. Fractional integer를 double로
정확히 표현할 수 없는 매우 먼 좌표도 같은 이유로 거부한다.

## Wrap

`wrap_position()`은 각 fractional component를 `[0, 1)`로 옮긴다. `wrap_frame()`은 present atom에
같은 연산을 적용한 새 immutable frame lease를 만든다.

- float32/float64 position precision, velocity, presence와 기존 metadata를 보존한다.
- Missing atom의 finite placeholder coordinate는 변경하지 않는다.
- 결과 metadata에는 `pbc.operation=wrap-atoms`를 추가한다.
- Unit cell이 없는 frame은 periodic이라고 추정하지 않고 실패한다.

이는 원자별 wrap이다. 결합 graph를 따라 분자를 하나로 잇는 molecule `join`/`whole`, residue 또는
fragment 단위 wrap 및 periodic representation copies는 아직 구현되지 않았다.

## Trajectory continuity unwrap

`TrajectoryUnwrapper`는 frame을 순서대로 받아 atom별 image continuity를 유지한다. 첫 frame의
present coordinate는 그대로 anchor가 되며, 이후 frame은 현재 frame cell에서 이전 wrapped
coordinate와의 exact minimum-image delta를 구해 이전 unwrapped coordinate에 더한다.

- 모든 frame은 생성 시 정한 atom count와 일치하고 unit cell을 가져야 한다.
- Missing atom은 해당 atom의 continuity를 끊으며, 다시 나타난 coordinate는 새 raw anchor가 된다.
- 변하는 cell에서는 각 transition의 도착 frame cell을 사용한다.
- 변환 또는 frame 생성 실패 시 내부 continuity와 processed-frame count는 바뀌지 않는다.
- `reset()`은 frame count와 모든 atom continuity를 지운다.
- 결과 metadata에는 `pbc.operation=unwrap-atom-continuity`를 추가한다.

이 알고리즘은 연속 frame 사이의 실제 atom 이동이 minimum-image ambiguity보다 작다는 전제를 가진다.
Bond-aware whole-molecule reconstruction이나 큰 frame stride에서의 이동 추론을 대신하지 않는다.

## Object frame 처리 모드 (`pbc mode`)

`pbc mode --mode wrap|unwrap|none|status`는 object의 frame을 읽는 층에 `trajectory::PbcProcessedSource`를 끼워 넣어(non-destructive) 렌더링,
선택, 분석, trajectory 재생, 저장이 같은 처리된 frame을 보게 한다. 원래의 좌표 source와 trajectory 상태는 object에 보관되어 `none`이
그대로 복원하고, mode 전환은 coordinate revision을 올려 진행 중이던 분석 계획을 stale로 만든다.

- `wrap`: frame마다 모든 atom을 단위 세포의 분수 좌표 [0,1)로 접는다(`wrap_frame`). 이미 세포 안에 있는 atom은 변하지 않는다.
- `unwrap`: `TrajectoryUnwrapper`로 atom별 연속성을 이어 주기적 경계를 넘은 atom을 이전 위치에서 최소 이미지 변위만큼 이어 붙인다.
  frame을 순서대로 읽으면 O(1), 건너뛰거나 거꾸로 가면 처음부터 다시 계산하며 재계산이 5만 frame을 넘으면 `resource_exhausted`다.
- 모든 frame에 unit cell이 있어야 하며 없으면 모드 전환이 거부된다. 분자 단위(`bymol`) unwrap, 중심 지정(`center`)은 제공하지 않는다.
- 편집(`object remove-atoms`, 원자 위치 편집), `load append`, `edit undo|redo`는 모드가 켜져 있으면 거부한다(처리된 source에 원본 기준 편집이
  섞이는 것을 막기 위함). 복사·추출은 처리된 좌표를 새 object에 그대로 담고 새 object의 모드는 `none`이다.

## Analysis 연결 상태

`analysis::atom_distance()`는 `DistanceBoundary::raw`와
`DistanceBoundary::minimum_image`를 지원하며 후자는 위 exact triclinic kernel을 공유한다.
Minimum-image mode는 frame unit cell이 없으면 실패한다. 현재 Workspace/GUI/CLI/Python operation은
`--pbc raw|minimum-image`를 같은 Workspace kernel로 실행한다. 성공 response와 persistent
measurement record는 선택한 boundary mode를 보존한다. Selection reduction, trajectory frame range와
measurement object의 session serialization은 후속 vertical slice다.
