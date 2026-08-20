# Command grammar v1

첫 molecular vertical slice의 GUI·Console·Python parity와 session replay 검증을 기준으로 foundation
command grammar를 version 1로 pin한다. Canonical history/session에는 생략된 기본값과 alias가 모두
정규화된 invocation을 저장한다.

## Native CLI

```text
molshredder load --path PATH [--name NAME] [--file-format FORMAT]
molshredder format list [--family all|structure|trajectory|volume]
                           [--direction all|read|write]
molshredder volume load --path PATH [--name NAME]
                           [--file-format auto|dx|opendx|mrc|map|ccp4|mrcs]
                           [--coordinate-unit angstrom|nanometer]
molshredder volume list
molshredder volume save --path PATH [--file-format auto|dx|opendx|mrc|map|ccp4|mrcs]
                           [--overwrite false|true]
molshredder volume isosurface --level NUMBER
                           [--color blue|cyan|green|magenta|orange|red|white|yellow]
                           [--opacity 0..1] [--replace false|true]
molshredder save --path PATH [--file-format auto|pdb|mmcif|cif|mol|mol2|psf|pqr|sdf|gro|g96|xyz]
                 [--frames current|all] [--precision 0..15]
                 [--comment TEXT] [--overwrite false|true]
molshredder select --name NAME --expression EXPR [--update false|true]
molshredder show --representation lines|sticks|spheres|ribbon|cartoon [--selection EXPR]
                   [--replace false|true]
molshredder analyze center [--selection EXPR] [--mode centroid|com]
                               [--precision 0..15] [--unit angstrom|nanometer]
molshredder measure distance --from EXPR --to EXPR
                               [--mode atom|centroid|com|minimum|maximum|mean|closest]
                               [--pbc raw|minimum-image]
                               [--precision 0..15] [--unit angstrom|nanometer]
molshredder script run --path SCRIPT.py --trust true
                         [--arguments-json '["arg1","arg2"]']
                         [--working-directory DIRECTORY]
molshredder system version
molshredder system info
molshredder view get
molshredder view center [--selection EXPR] [--move-origin false|true]
                           [--state current|all|STATE]
                           [--duration SECONDS] [--hand -1|0|1]
molshredder view zoom [--selection EXPR] [--buffer ANGSTROM]
                         [--complete false|true]
                         [--state current|all|STATE]
                         [--duration SECONDS] [--hand -1|0|1]
molshredder view orient [--selection EXPR] [--state current|all|STATE]
                         [--duration SECONDS] [--hand -1|0|1]
molshredder view origin [--selection EXPR] [--state current|all|STATE]
molshredder view reset [--duration SECONDS] [--hand -1|0|1]
molshredder view clip --mode near|far|move|slab|atoms|near-set|far-set
                        --distance ANGSTROM [--selection EXPR]
                        [--state current|all|STATE]
molshredder view get-clip
molshredder view move --axis x|y|z --distance ANGSTROM
molshredder view turn --axis x|y|z --angle DEGREES
molshredder view set [--target-x X] [--target-y Y] [--target-z Z]
                       [--model-origin-x X] [--model-origin-y Y]
                       [--model-origin-z Z]
                       [--orientation-w W] [--orientation-x X]
                       [--orientation-y Y] [--orientation-z Z]
                       [--distance D]
                       [--projection perspective|orthographic]
                       [--field-of-view RADIANS]
                       [--orthographic-height H] [--aspect-ratio R]
                       [--near-clip N] [--far-clip F]
molshredder view export-pymol
molshredder view import-pymol --values "18 comma/space-separated numbers" [--duration SECONDS] [--hand -1|0|1]
molshredder view list
molshredder view store --name NAME
molshredder view recall --name NAME [--duration SECONDS] [--hand -1|0|1]
molshredder view delete --name NAME
molshredder view clear
molshredder stereo get
molshredder stereo modes
molshredder stereo set [--enabled false|true]
                         [--mode side_by_side|crosseye|walleye|anaglyph|quad_buffer|row_interleaved|column_interleaved|checkerboard|openvr]
                         [--swap-eyes false|true]
                         [--shift-percent 0..100] [--angle-scale 0..20]
                         [--anaglyph-mode true|gray|color|half_color|optimized]
```

각 leaf command는 `--format text|json|csv`를 받는다. CSV 성공 출력은 typed table command에 한정한다.
`molshredder COMMAND --help`가 registry의
required/default/choice metadata에서 생성되는 authoritative user help다.

MD vertical slice의 additive trajectory playback 및 time-series 문법은
[Trajectory commands](TRAJECTORY_COMMANDS.md)에 둔다. 이는 foundation 다섯 command의 grammar v1을
변경하지 않는다.
다중 object의 additive `object list/activate/visibility` 문법과 failure-atomic scene semantics는
[Molecular objects and visibility](OBJECTS.md)에 둔다.
Additive `format list`와 `save` 문법, atomic output 및 semantic loss table은
[Structure writing](STRUCTURE_WRITING.md)에 둔다.
외부 Python script 실행의 trust, provenance와 failure semantics는 [Automation](AUTOMATION.md)에 둔다.
`system info`의 build/runtime 진단 계약은 [Build support configuration](SUPPORT_CONFIGURATION.md)에 둔다.

## 의미

- `load`: molecular structure를 새 object로 읽는다. Multi-record SDF, multi-molecule MOL2와 multi-block mmCIF는 record/block마다
  object를 만들고 전체 batch를 atomic commit한다. Explicit name은 여러 structure에서 숫자 suffix로 확장된다.
  Concatenated GRO는 stable identity의 frame들로 한 object에 유지된다.
  G96 ordered POSITION block도 stable identity의 frame들로 한 object에 유지된다. PSF와 Amber PRMTOP은
  정상적인 zero-frame topology object로 load되며 `traj load` 전에는 coordinate operation이 명시적으로
  실패한다. PRMTOP에는 `--file-format prmtop`을 사용하며 `.prmtop`, `.parm7`, `.top`은 자동 판별된다.
  VTF는 `--file-format vtf` 또는 `.vtf` suffix로 topology, bond, atom property, unit cell과 ordered/indexed
  sparse frame을 한 object에 load한다. Multi-frame structure는 즉시 `traj frame/play`에 연결된다.
  BinaryCIF 0.3.x는 `.bcif` 또는 `--file-format bcif`로 읽으며 multi-block/model semantics는 mmCIF와 같다.
- `volume load`: ASCII OpenDX regular scalar grid 하나를 molecular object와 독립된 typed volume scene
  object로 읽는다. `coordinate-unit` 기본값은 APBS convention인 `angstrom`이며 source format 자체가
  단위를 기록하지 않으므로 override와 provenance를 결과에 보존한다. 성공 결과는 dimensions, origin,
  세 delta vector, z-fastest value count, precision과 scalar range를 반환한다. MRC/CCP4는 header geometry가
  Angstrom이므로 `nanometer` 선택 시 geometry를 변환한다. MRC axis permutation과 origin/start policy는
  [Volumetric data](VOLUMETRIC_DATA.md)에 고정한다.
- `volume list`: 현재 Workspace의 volume object ID/name, scene node, dimensions, value count, precision,
  scalar range, coordinate unit, representation count와 active/visibility를 typed table로 반환한다.
- `volume save`: active volume을 OpenDX 또는 canonical MRC2014 mode 2로 failure-atomic 저장한다. Format은
  명시하거나 suffix로 선택하며 GUI action, CLI와 Python이 같은 typed operation 및 loss report를 사용한다.
- `volume isosurface`: active volume의 contour mesh를 생성한다. 기본 cyan/불투명 style을 사용하며
  `--replace true`가 기본이다. GUI, CLI와 Python은 같은 failure-atomic Workspace operation을 호출한다.
- `select`: atom selection expression을 named selection으로 생성 또는 교체한다. `--update true`는
  frame/state 변화에 따라 다시 평가되는 selection을 뜻한다.
- `show`: selection에 representation을 보이게 한다. 현재 choice는 lines/sticks/spheres와
  protein backbone ribbon/cartoon이다. 기본은 기존 representation에 append하며 `--replace true`는
  새 packet 생성이 성공한 뒤 기존 representation을 atomic하게 교체한다. Cartoon은 독립
  STRIDE-method v0 assignment를 사용한다.
- `analyze center`: 재사용 가능한 scalar/vector analysis result를 계산한다. `centroid`와 `com`을
  구분하며 기본 selection은 `all`이다. COM은 topology의 explicit `mass` property를 우선하고 없을
  때 versioned estimated element-mass table을 사용하며 provenance를 결과에 포함한다.
- `measure distance`: scene/session에 남는 measurement를 만든다. 두 endpoint는 atom 또는 selection
  expression이고 mode가 reduction semantics를 지정한다. 현재 실행되는 첫 slice는 endpoint마다
  정확히 한 atom을 선택하는 `mode=atom`이며 `pbc=raw|minimum-image`를 지원한다. Minimum-image는
  active frame의 orthorhombic/triclinic unit cell을 요구한다.
- `view get/set`: target, normalized quaternion orientation, distance, projection, FOV/orthographic height,
  별도 model-space rotation origin, aspect ratio와 near/far clip으로 구성된 validated camera snapshot을 조회·부분 갱신한다. Validation 실패는
  이전 snapshot을 보존한다. Quaternion component를 직접 바꿀 때에는 결과 네 component가 unit quaternion이어야 한다.
- `view center/zoom/orient/origin`: active object에서 selection을 평가하고 missing coordinate는 제외한다.
  `state=current`는 현재 trajectory frame(기본값), `all`은 알려진 모든 coordinate frame,
  양의 정수는 1-based explicit state를 사용한다. PyMOL script 이식을 위해 `-1=current`, `0=all`도
  받는다. All-state extent는 trajectory를 메모리에 전체 복사하지 않고 frame source/cache에서 streaming하며,
  exact complete-fit radius를 위해 bounds/radius 두 pass를 수행한다. Result의 `evaluated_frame_count`로 실제
  평가 frame 수를 확인할 수 있다.
  Center는 selection AABB midpoint로 target을 옮기며 `move-origin=true`가 기본이다. Zoom은 같은 midpoint와 extent를
  사용하고 `complete=true`이면 모든 선택 atom center까지의 최대 거리를 사용한다. `buffer`는 Angstrom 단위의
  절대 여백이다. Orient는 uniform-coordinate covariance의 principal axes를 variance 내림차순으로 camera
  right/up/backward에 맞춘 후 회전된 AABB를 frame한다. 축 부호는 현재 camera에서 회전 변화가 가장 작은 조합을
  선택하고, eigenvalue가 퇴화한 subspace는 현재 camera 축을 투영해 갑작스러운 회전을 피한다. Origin은 기본적으로
  selection AABB midpoint를 camera의 model-space rotation pivot으로 사용한다. `--position x,y,z`는 selection 없이
  명시 좌표를 사용한다. `--object current|NAME|ID`를 함께 주면 camera 대신 해당 object의 local transform pivot을
  설정하며, position이 없으면 그 object 안에서 selection/state를 평가한다. Pivot만 바꿀 때 현재 affine placement는
  유지한다. Empty selection은 기존 camera/scene을 보존한 채 `invalid_selection`으로 실패한다.
  Atom-radius-aware exact PyMOL `MAX_VDW` framing은 후속 범위다.
- `view reset`: object option이 없으면 camera 기본 projection/FOV/clip/orientation을 복원하고 모든 effectively-visible molecular object의
  current frame과 volume grid bounds를 함께 frame한다. Live viewport aspect ratio는 보존하며 molecule이나 volume이
  없으면 canonical default camera로 돌아간다. Center, zoom과 reset의 `duration/hand`는 named-view recall과 같은
  committed-endpoint animation 계약을 사용한다. `--object current|all|NAME|ID`는 camera를 바꾸지 않고 대상 molecular
  object의 translation/rotation/scale/pivot을 identity transform으로 초기화한다.
- `view clip/get-clip`: `near`는 near plane에서 distance를 빼고 `far`는 far plane에서 distance를 빼며,
  `move`는 두 plane을 같은 signed distance만큼 옮긴다. `slab`은 양의 thickness로 현재 slab midpoint를
  유지하거나 optional selection의 projected AABB midpoint에 맞춘다. `atoms`는 selection atom을 current camera
  forward 방향으로 투영한 min/max depth에 signed buffer를 더한다. `near-set/far-set`은 absolute camera distance를
  설정한다. 모든 mode는 `0 < near < far`를 만족해야 하며 위반하면 이전 camera가 유지된다. `get-clip`은 near/far와
  thickness를 typed result로 반환한다. Selection을 사용하는 slab/atoms mode는 같은
  current/all/explicit state scope를 사용한다.
- `view move/turn`: `x|y|z`는 world 축이 아니라 현재 camera-local 축이다. Move의 양의
  x/y는 model을 화면 반대 방향으로 평행 이동하도록 target을 옮기며 model origin은 고정한다.
  양의 z는 camera distance와 near/far clip을 같은 Angstrom 만큼 줄이고, orthographic view의 height도
  같은 비율로 조정한다. Turn은 model origin을 pivot으로 camera-local 축 주위를 도(degree)로 회전하며
  off-axis target과 eye도 같이 회전한다. Invalid/non-finite 요청이나 z 축이 camera/clip invariant를
  넘는 요청은 기존 camera를 보존하고 실패한다.
- `view projection`: `--mode perspective|orthographic`와 degree 단위
  `--field-of-view-degrees`를 제공한다. 기본 `--preserve-scale true`는 전환 직전 target-plane vertical span을
  유지한다. Perspective→orthographic은 그 span을 orthographic height로 사용하고, orthographic→perspective 또는
  perspective FOV 변경은 distance를 다시 계산하며 near/far clip도 distance와 같은 비율로 조정한다. Target,
  orientation과 model origin은 유지한다. `--preserve-scale false`는 projection/FOV field만 바꾸는 raw switch다.
- `stereo set/get/modes`: `stereo_shift`(objective distance의 %)와 `stereo_angle` scale을 validated state로
  관리한다. 현재 QRhi renderer는 `side_by_side`, `crosseye`, `walleye`, `anaglyph`를 실제 렌더링한다.
  Anaglyph는 `true`, `gray`, `color`, `half_color`, `optimized` 색상 조합을 선택한다. 다른 알려진 mode는
  monoscopic fallback으로 위장하지 않고 `unsupported`를 반환하며, `stereo modes`가 구현 여부와 runtime availability를 구분한다.
  FOV는 `0 < degrees < 180`이어야 하며 실패 시 camera 전체를 보존한다. `perspective`, `orthographic`,
  PyMOL 용어 `orthoscopic` shorthand도 이 operation으로 정규화된다.
- `view store/recall/list/delete/clear`: 현재 camera-only snapshot을 이름으로 저장하고 deterministic name order로
  관리한다. PyMOL scene처럼 object visibility나 representation을 함께 저장하지 않으며 named scene은 별도 capability다.
  Store 후 같은 Workspace에서 recall해야 하므로 one-shot process 두 개가 아니라 Desktop, Python 또는
  `molshredder console`에서 연속 실행한다.
- `view export-pymol/import-pymol`: PyMOL public `get_view/set_view`의 18-value camera layout을 typed array와
  재사용 가능한 text로 내보내거나 붙여넣어 가져온다. Import는 정확히 18개 finite number, right-handed
  orthonormal rotation과 valid clip range를 요구하며 실패 시 현재 camera를 보존한다. Import와 recall의
  `duration`은 0–3600초이고 기본 0은 즉시 적용이다. `hand=-1|0|1`은 180도 부근 회전 방향을 고르며 0은
  PyMOL 동작과 같이 +1로 해석된다. Operation은 최종 camera를 즉시 commit하고 `animation`에 start/end,
  duration과 handedness를 반환한다. 따라서 Desktop 같은 interactive client는 결정적 endpoint를 유지하면서
  화면만 시간에 따라 보간할 수 있다.

## Shorthand

| shorthand | canonical command | 고정 argument |
|---|---|---|
| `open` | `load` | 없음 |
| `perspective` | `view projection` | `mode=perspective` |
| `orthographic` | `view projection` | `mode=orthographic` |
| `orthoscopic` | `view projection` | `mode=orthographic` |
| `center` | `analyze center` | 없음 |
| `centroid` | `analyze center` | `mode=centroid` |
| `com` | `analyze center` | `mode=com` |
| `dist`, `distance` | `measure distance` | 없음 |

명시한 option은 shorthand의 고정 argument도 override한다. 예를 들어 `com --mode centroid`는
canonical `analyze center --mode centroid`가 된다. Alias 사용 여부는 history에 남기지 않는다.

## 현재 실행 상태

다섯 foundation command는 stateful Workspace와 실제 reader/selection/representation/analysis
kernel에 연결됐다. 연속 native terminal workflow는 한 process의 `molshredder console`에서
실행한다. One-shot command를 각각 새 process로 실행하면 state가 유지되지 않는다.
`analyze center`는 pure result이고 `measure distance`는 Workspace에 boundary mode를 포함한
measurement record를 남긴다. Distance의 selection reduction mode는 schema v1 choice로 예약돼 있지만
kernel이 구현될 때까지 명시적 `unsupported` error를 반환한다. `minimum-image`는 shared exact
triclinic PBC kernel을 실행하며 결과 data의 `pbc` field에 provenance를 보존한다.
Camera gesture는 Desktop에서 `view set`으로, selection framing/orient/pivot/reset은 각각
`view center/zoom/orient/origin/reset`
operation으로 정규화된다. Named-view operation을 session journal에 포함하면
replay가 현재 camera와 inventory를 복원한다. PyMOL public 18-value layout은 import invocation 자체를 journal에
넣어 재현할 수 있다. Replay는 wall-clock animation을 기다리지 않고 committed endpoint를 복원한다. Full scene 저장은
아직 구현되지 않았다.

## 호환성 정책

- Grammar v1 canonical leaf name은 `load`, `select`, `show`, `analyze center`, `measure distance`다.
- `open`, `center`, `centroid`, `com`, `dist`, `distance`는 convenience alias이며 session/history에는
  저장하지 않는다.
- 기존 invocation의 의미를 바꾸는 command/parameter rename, default 변경 또는 choice 제거는
  grammar version과 session migration이 필요하다.
- 새 optional parameter나 representation/format choice를 추가하는 것은 기존 normalized invocation을
  바꾸지 않는 범위에서 additive change로 처리할 수 있다.
- `--format`은 presentation option이므로 domain invocation과 session provenance에 포함하지 않는다.
