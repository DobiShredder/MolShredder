# Command grammar v1

## Render settings

Render style는 GUI, CLI와 Python이 공유하는 canonical operation으로 변경한다.

```text
setting list
setting get --name sphere_scale --scope global
setting set --name line_width --value 2.0 --scope object
setting set --name sphere_color --value '#33aaff' --scope atom --target 12
setting unset --name sphere_color --scope atom --target 12
setting set --name sphere_scale --value 0.5 --scope atom --selection 'resi 1-5'
setting set --name stick_color --value blue --scope bond --selection 'resi 2'
setting reset --scope state
```

Scope는 `global`, `object`, `state`, `atom`, `bond`이다. 명시적 state는 1-based이고 atom/bond
`--target`은 topology가 발급한 non-zero 64-bit stable ID다. Atom/bond scope에서는 `--target` 또는
`--selection`이 필요하다. `--selection`은 PyMOL `set NAME, VALUE, SELECTION`처럼 atom scope에서는 선택된 모든 원자에,
bond scope에서는 두 원자가 모두 선택된 결합에 한 번에 적용한다(`stick_color`, `stick_radius`, `line_color`, `line_width`는
bond setting). 오류는 기존 setting과 scene을
변경하지 않는다. 전체 catalog와 value contract은 [Render setting service](RENDER_SETTINGS.md)를
참고한다.

## Color

Atom color와 color table은 GUI, CLI, Python이 공유하는 canonical operation으로 바꾼다.

```text
color --color red --selection 'chain A'
color --color atomic
color --color auto --selection 'index 3'
color define --name mycolor --red 255 --green 128 --blue 0
color get --color 0xff8000
color list --scope custom
recolor
```

- `color`는 active object의 selection에 색을 지정한다. 지정하지 않은 원자는 MolShredder 기본 palette를 쓴다.
- `color`, `show`, `hide`, `as`, `toggle`의 `--scope workspace`는 PyMOL처럼 모든 molecular object에 같은 selection을 적용한다
  (기본 `active`). Object는 `model NAME`/`model PATTERN`(wildcard)으로 좁힌다. Toggle은 모든 대상 원자 기준으로 끄거나 켠다.
- `setting set --name auto_color --value true`면 이후 load에서 PyMOL처럼 carbon을 object auto color(26, 5, 154, …)로, 다른 원소를
  원소 색으로 칠한다. MolShredder 기본값은 off다(PyMOL 기본 on).
- Color token은 대소문자를 무시한 이름(앞부분이 같은 이름 중 가장 낮은 index), 10진 index, `0xRRGGBB`,
  `#RRGGBB`, `atomic`(원소별 기준 색), `object`(object 색), `auto`(object 생성과 공유하는 40색 순환),
  `default`(기본 palette로 되돌림)를 받는다.
- 내장 table은 Open-Source PyMOL 3.1.0과 같은 5,388개 index/이름/RGB를 쓴다. `color define`은 새 이름에 5388부터
  index를 붙이고, 기존 이름은 index를 유지한 채 RGB만 바꾼다. 성분 중 하나라도 1보다 크면 세 성분을 0-255 범위로 본다.
- `color`와 `color define`은 session journal에 기록되어 replay 때 table, atom color와 auto 순환 위치가 복원된다.
- 실패한 지정(알 수 없는 색, 잘못된 selection, atom color로 쓴 `front`/`back`)은 상태를 바꾸지 않는다.
- `color`와 `background set`은 접두사가 여러 named color에 맞으면(`bl` → black/blue/bluewhite) `ambiguous color` 오류를
  낸다. named color가 하나만 맞으면 그 색, 하나도 없으면 생성 ramp 중 가장 낮은 index(`s0` → `s000`)를 쓴다. Render setting
  값과 `color get`은 가장 낮은 index를 쓴다.

```text
spectrum --expression b --palette blue_white_red --selection 'chain A'
spectrum --expression count --palette rainbow --byres true
spectrum --expression 'b*q' --palette 'red green blue' --interpolation hls
spectrum --expression b --palette rainbow --minimum 10 --maximum 60
background set --color white
background get
bg_color --color 0x336699
```

- `spectrum`의 내장 palette(60개, 고유 접두사 허용)와 단순 속성(`count`, `b`, `q`, `pc`/`partial_charge`, `formal_charge`,
  `resi`, `resv`, `index`, `rank`, `ID`, `vdw`, `elec_radius`, `numeric_type`, `cartoon`, `chain`, `resn`, `name`, `elem`, `segi`, `alt`,
  `ss`, `type`, `text_type`)은 생성 ramp 색 index를 지정한다. `fc`, `stereo`, `state`는 PyMOL처럼 오류다. `ss`는 PDB HELIX/SHEET
  code이며 기록이 없는 file은 빈 값이다(PyMOL은 load 때 dss로 정한다). 문자열 속성은 처음 등장한
  순서로 번호를 붙이고, `byres`는 각 잔기의 첫 선택 원자 값을 쓴다. 범위는 최소·최대를 모두 주고 최대≥최소일 때만 쓰며,
  범위를 벗어난 값은 양 끝 색으로 맞춘다.
- 그 밖의 식(`b*q` 등, selection 숫자 문법)이나 공백으로 나눈 색 목록은 RGB/HLS/HSV 보간 literal 색(`0xRRGGBB`)을
  지정한다. 이때 문자열 속성은 정렬 순서로 번호를 붙이고, 값이 없는 원자는 색을 유지한다. 반환값은 사용한 최소·최대다.
- `background set --color default`는 MolShredder 기본 배경으로 되돌린다. 알 수 없는 색은 오류다. Render setting color의
  `front`는 배경의 반전색, `back`은 배경색이다.

## Labels와 measurement 표시

```text
label --selection 'name CA' --expression '"%s-%s" % (resn, resi)'
label --expression b
label --selection all --expression "''"
label literal --selection 'index 1' --text 'active site'
show --representation labels --selection all
setting set --name label_digits --value 2 --scope object
setting set --name label_position --value '[0, 0, 3]'
setting set --name dash_gap --value 0.2
```

- `label`은 선택 원자마다 식을 평가해 label을 붙이고 labels 표시를 켠다. 빈 식은 text만 지운다. 식은 Python 부분집합이다:
  원자 field(`name`, `resn`, `resi`, `resv`, `chain`, `segi`, `alt`, `elem`, `type`, `model`, `ss`, `text_type`, `b`, `q`,
  `partial_charge`, `formal_charge`, `ID`, `index`, `rank`, `vdw`), 문자열·숫자 literal, `+ - * / // % **`, `%` 서식, 괄호와 tuple,
  `str()`/`int()`/`float()`/`len()`/`round()`/`abs()`. 실수 결과는 `label_digits` 자릿수로 쓴다. 알 수 없는 이름은 오류이며 상태를
  바꾸지 않는다. `ss`는 PDB HELIX/SHEET code다.
- `label literal`은 text를 평가하지 않고 그대로 붙인다.
- `labels`는 representation 종류이며 `show`/`hide`/`as`/`toggle`과 `everything`에 포함된다.
- Measurement overlay label은 `label_distance_digits`/`label_angle_digits`/`label_dihedral_digits`(음수면 `label_digits`) 자릿수의
  숫자다(Å가 아닌 단위만 붙임). Dash는 중점에서 대칭으로 `dash_length`+`dash_gap` 간격으로 놓이고 `dash_width`, `dash_color`를 쓴다.

## Color ramp

```text
ramp new --name potential --source esp_map --range '-10 0 10' --color 'red white blue'
ramp_new --name heat --source dens --range '0 1' --color hot
ramp new --name near --source ligand --range '0 2 4' --color 'red green blue'
color --color potential --selection all
ramp update --name potential --range '-5 0 5'
```

- `ramp new`(alias `ramp_new`)는 volume map 값 또는 molecular object까지의 근접 거리(마지막 level이 cutoff)로 색을 정하는 ramp를
  만든다. 색은 이름 목록이나 내장 spectrum(`traditional`, `sludge`, `ocean`, `hot`, `grayable`, `rainbow`, `afmhot`, `grayscale`)이다.
- Ramp 이름은 `color` token으로 쓸 수 있다(color code −10, −11, …). 원자 색은 rebuild 때 원자 위치에서 계산하며 map 밖은 경계 값을
  쓴다. 색 수가 level 수와 다르면 첫~끝 level 사이에 균등 재배치한다. Proximity ramp은 PyMOL처럼 현재 frame과 무관하게 source의 첫
  frame(state 1)을 쓰고, 그 좌표를 편집하면 다시 칠한다.
- `ramp update`(alias `ramp_update`)는 range나 color를 바꾸고 그 ramp를 쓰는 원자를 다시 칠한다.

## Map contour과 cartoon type

```text
isomesh --name mesh1 --map dens --level 1.0
isosurface --name surf1 --map dens --level 1.5 --color red
isodot --name dots --map dens --level 1.0
volume contour --name pocket --map dens --level 1.0 --mode mesh --selection 'resi 45' --carve 2.0
isolevel --name mesh1 --level 0.8
volume isolevel get --name mesh1
cartoon --type tube --selection 'chain A'
cartoon --type automatic
```

- `volume contour`(alias `isosurface`/`isomesh`/`isodot`)는 map의 이름 있는 contour를 만든다. 같은 이름·같은 mode는 교체하고 다른
  mode면 오류다. `--carve`는 선택 원자로부터 그 거리 안의 geometry만, `--buffer`는 선택 범위를 그 거리만큼 넓힌 상자 안만 남긴다(PyMOL처럼 triangle·mesh 선의
  모든 정점이 영역 안일 때만 남긴다).
  결과 extent는 carve/buffer 영역 또는 map 범위다. Level은 float32로 저장되며 `volume isolevel`(alias `isolevel`)로 바꾸고
  `volume isolevel get`으로 읽는다. Contour는 PyMOL에서 관찰한 256-case table의 marching cubes로 만들어 isosurface triangle, isomesh(grid 평면 위의 triangle 변)와 isodot(surface 정점마다 점)이
  PyMOL export와 같다. `isosurface_algorithm`이 0이 아니면 PyMOL의 다른 algorithm을 확인하지 못해 결과 `warnings`에 fallback을 보고한다.
- `cartoon`은 PyMOL type(`skip`, `automatic`, `loop`, `rectangle`, `oval`, `tube`, `arrow`, `dumbbell`, `putty`, `dash`, `cylinder`,
  고유 접두사 허용)을 지정한다. `dumbbell`/`putty`/`dash`/`cylinder`는 가장 가까운 profile로 그리고 packet provenance에 남긴다.
- CCP4 reader는 identity skew matrix의 translation을 map origin으로 받는다(PyMOL 저장 형식). 다른 skew는 지원하지 않는다.

## Image export

```text
image save --path figure.png --width 1200
png --path figure.png --width 800 --height 600 --dpi 300
image save --path draw.png --ray false
image save --path figure --width 3.5 --units inches --dpi 600
image save --path frame.ppm --height 480
movie export --prefix frames/f_ --first 1 --last 120 --width 1280
setting set --name opaque_background --value true
view refresh
```

- `image save`(alias `png`)는 보이는 object의 표현과 measurement overlay를 deterministic reference ray caster로 그려 8-bit RGBA
  PNG로 저장한다. Width 0은 640, height 0은 camera 비율을 따른다. dpi는 PNG pHYs에 기록한다. 저장은 임시 파일을 거쳐
  원자적으로 바꾸며, 없는 directory는 오류다.
- 형식과 확장자: `--image-format guess|png|ppm`(기본 guess)에서 확장자가 encoder를 정한다(`.png`, `.ppm`, 확장자 없음은
  `.png`를 붙임). 알 수 없는 확장자(`.jpg` 등)는 오류이며 파일을 만들지 않는다. 명시 형식은 주어진 파일 이름을 그대로 쓴다.
- 크기: `--width`/`--height`는 `--units pixels`(기본, 정수)이거나 `inches|centimeters|millimeters`(소수 허용, `--dpi` 없으면
  300)이다. 한쪽만 주면 다른 쪽은 camera 비율로 정해지고(width만: height = width / aspect, height만: width = height * aspect)
  둘 다 없으면 width 640이다. 최대 변은 8192 pixel이다. PNG에는 `--dpi`(또는 물리 단위 기본 300)가 기록된다.
- `movie export --prefix P [--first F] [--last L]`은 설정된 movie 타임라인의 1-based frame 범위(기본 전체)를 차례로 이동하며
  `P0001.png`처럼 4자리 번호 파일로 저장한다(`--image-format png|ppm`, 크기·`--ray`·`--dpi`는 `image save`와 같음). 재생 중에는 거부하고,
  대상 파일이 하나라도 있으면 `--overwrite true` 없이는 아무 것도 쓰지 않으며, 중간에 실패하면 이번 호출이 만든 파일을 지우고 타임라인
  위치를 되돌린다. Video 인코딩(FFmpeg 등)은 외부 encoder 연동으로 아직 제공하지 않는다.
- 배경 alpha: `--ray true`는 `ray_opaque_background`가 0 이상이면 그 값, −1이면 `opaque_background`(기본 off: 투명)를 따른다.
  `--ray false`는 `opaque_background`만 따른다. 투명 pixel도 배경 RGB를 유지한다.
- PyMOL ray tracing, OpenGL draw, `ray_default_renderer`의 외부 renderer는 이 reference ray caster로 대체하는 명시적 fallback이며
  결과 `warnings`에 기록한다. Text label은 image에 그리지 않는다. `ambient`과 `light`만 image 음영에 반영한다.

## Object lifecycle

```text
object list
object activate --id 2
object visibility --id 2 --visible false
object rename --object 2 --name "ligand A"
object reorder --object 2 --position 1
object delete --object current
object topology-retain --atom-ids 3,1 --expected-version 1
group add --name complexes --members "ubq* 1abc"
group remove --name complexes --members ubq2
group empty --name complexes
group ungroup --members ubq1
group excise --name complexes
group purge --name complexes
group open --name complexes
group list
object enable --pattern "ubq*"
object disable --selection "resn HOH"
object covering --selection "name N"
object type --name ubq
object count --object current --selection "name CA"
object create --name cas --selection "name CA" --source ubq
object copy --object ubq --name ubq-copy
object extract --name waters --selection "resn HOH" --source ubq
object remove-atoms --selection "resn HOH"
```

`--object`는 `current`, stable object ID 또는 exact name을 받는다. `--position`은 object
panel의 1-based position이다. Rename과 reorder는 object ID와 scene-node ID를 바꾸지 않는다.
Delete는 object에 속한 measurement와 render-setting override를 같이 제거하고, active object를
지우면 같은 panel position의 다음 object 또는 직전 object를 선택한다. Missing target,
duplicate name이나 범위 밖 position은 Workspace와 scene을 변경하지 않는다.

`object enable|disable`은 `--pattern`(이름 glob: `*`, `?`, 대소문자 구분) 또는 `--selection`(그 selection에 atom이 있는 molecular
object) 중 정확히 하나로 대상을 정하고 visibility를 한꺼번에 바꾼다. 대상이 없거나 인자가 둘이거나 없으면 아무 것도 바꾸지 않는 오류이고,
중간에 실패하면 이미 바꾼 object를 되돌린다. `object covering`은 selection이 걸리는 object의 목록을, `object type`은 이름이
`object:molecule`, `object:volume`, `selection` 중 무엇인지 알려 준다(selection 이름은 active object 기준). Group은 아직 없다(Q-033).

Group은 이름과 member(object 또는 다른 group)를 가진 workspace 메타데이터다(Q-033 a). Object는 계속 scene root의 직계 child라
렌더링·순서·transform에는 영향이 없고 session journal로 그대로 복원된다. `group add`는 필요하면 group을 만들고 member를 옮기며
(한 object는 한 group에만 속하므로 이미 다른 group에 있으면 이동), member 인자는 공백/쉼표로 구분한 이름과 glob이고 아무것도
일치하지 않으면 오류다. 자기 자신이나 조상 group을 넣는 순환, object와 같은 group 이름, 쉼표·wildcard가 든 이름은 거부한다.
`group remove|ungroup|empty`는 member를 최상위로 돌려보내고, `group excise`는 group을 지우고 member를 group이 있던 자리(상위 group
안이면 그 안)에 둔다. `group purge`는 group 아래의 모든 object와 하위 group을 지우고 빈 group만 남긴다. `object enable|disable`에
group 이름을 주면 그 아래 모든 object에 적용되며 group의 `enabled`는 아래 object 중 하나라도 보이면 true로 계산한다(그래서 member를
켜면 상위 group도 켜진 상태가 된다). `group open|close|toggle`은 object panel 펼침 상태만 바꾸고 `group list`는 member·parent·상태를
돌려준다. Object를 삭제하면 속한 group에서도 빠진다. Group 이름과 object 이름은 같은 이름 공간을 쓴다.

`object count`는 상태를 바꾸지 않고 selection의 atom 수와 object의 state 수를 반환한다. `object create`와
`object copy`는 선택한 atom의 stable ID·bond·모든 state를 복사한 새 object를 만들고 source를 바꾸지 않는다
(copy는 `all` selection). `object extract`는 같은 object를 만든 뒤 source에서 그 atom을 제거하며 source가 빈
object가 되는 경우는 거부한다. `object remove-atoms`는 active object에서 선택 atom과 그 bond를 제거하고
(measurement와 해당 atom·bond의 setting override도 함께 정리) 모든 atom을 지우는 요청은 거부한다. 새 object 이름이
이미 있거나 selection이 비어 있으면 아무 것도 바꾸지 않는다. 이 operation들은 session journal에 기록되어 session 복원 때 같은 순서로 다시 실행되지만, 아직 `edit undo` history에는 들어가지 않는다.

`object topology-retain`은 editing UI가 도입되기 전의 low-level snapshot operation이다. Active
object의 stable atom ID를 주어진 순서로 유지하고 나머지를 삭제한다. 반드시 읽은 topology
version을 `--expected-version`으로 보내며 stale version, duplicate/deleted ID와 uint64 overflow는
transaction 전체를 거부한다. 자세한 계약은 [Persistent identity and numeric contract](IDENTITY_AND_NUMERIC_CONTRACT.md)를
참고한다.

## Molecular editing

```text
edit atom-position --atom-id 1 --x 9 --y 8 --z 7 \
  --expected-topology-version 1 \
  --expected-coordinate-source-revision 1 --unit angstrom
edit atom-properties --atom-id 1 --name C1 --formal-charge 1 \
  --expected-topology-version 1 \
  --expected-coordinate-source-revision 1
edit residue-properties --atom-id 1 --name LIG --chain A --residue-number 7 \
  --expected-topology-version 2 \
  --expected-coordinate-source-revision 2
edit bond-order --bond-id 1 --order single \
  --expected-topology-version 3 \
  --expected-coordinate-source-revision 3
# order: single, double, triple, aromatic, amide, zero(PyMOL zero-order/coordination bond)
build molecule --name carbonyl \
  --atoms "C,6,0,0,0,0;O,8,1.2,0,0,0" \
  --bonds "1,2,double" --residue-name LIG --chain A \
  --residue-number 1 --unit angstrom --memory-budget-bytes 1048576
edit undo
edit redo
edit history [--memory-budget-bytes 268435456]
```

좌표 편집은 static coordinate source의 모든 state에 적용된다. Atom identity/element/formal charge,
residue name/chain/number와 bond order 편집은 stable ID를 보존하면서 topology version을 증가시킨다.
각 응답은 diff schema 1의 transaction kind, before/after 값과 revision을 반환한다. Undo history는 byte
budget을 넘지 않으며 새 edit는 redo branch를 무효화한다. GUI의 Edit menu/property editor/builder,
CLI와 Python `invoke`가 모두 같은 operation을 공유한다. Attached trajectory, missing ID, invalid chemistry,
stale revision, cancellation 및 undo snapshot budget 부족은 commit 전에 거부한다. 상세 계약과 의도적으로
지원하지 않는 template builder 범위는 [Editing and builders](EDITING_AND_BUILDERS.md)를 참고한다.

첫 molecular vertical slice의 GUI·Console·Python parity와 session replay 검증을 기준으로 foundation
command grammar를 version 1로 pin한다. Canonical history/session에는 생략된 기본값과 alias가 모두
정규화된 invocation을 저장한다.

## Native CLI

```text
molshredder load --path PATH [--name NAME] [--file-format FORMAT]
molshredder load batch --paths "PATH1;PATH2" [--names "NAME1;NAME2"]
                       [--file-format FORMAT]
molshredder format list [--family all|structure|trajectory|volume]
                           [--direction all|read|write]
molshredder volume load --path PATH [--name NAME]
                           [--file-format auto|dx|opendx|mrc|map|ccp4|mrcs]
                           [--provider auto|native|molfile:dx|molfile:ccp4]
                           [--plugin-path ABSOLUTE_PATH]
                           [--coordinate-unit angstrom|nanometer]
molshredder volume list
molshredder volume save --path PATH [--file-format auto|dx|opendx|mrc|map|ccp4|mrcs]
                           [--overwrite false|true]
molshredder volume isosurface --level NUMBER
                           [--color blue|cyan|green|magenta|orange|red|white|yellow]
                           [--opacity 0..1] [--replace false|true]
molshredder save --path PATH [--file-format auto|pdb|mmcif|cif|mol|mol2|psf|pqr|sdf|gro|g96|xyz]
                 [--frames current|all] [--precision 0..15]
                 [--comment TEXT] [--selection EXPR] [--overwrite false|true]
molshredder save text [--file-format pdb|mmcif|cif|mol|mol2|sdf|pqr|gro|g96|xyz]
                      [--selection EXPR] [--frames current|all]
                      [--precision 0..15] [--comment TEXT]
molshredder load text --content TEXT [--name NAME]
                      [--file-format auto|pdb|mmcif|cif|mol|mol2|sdf|pqr|gro|g96|xyz]
molshredder select [--name NAME] --expression EXPR [--update false|true]
                   [--merge false|true] [--state current|all|N]
molshredder select list [--name NAME] [--by index|id|rank] --values 1,5,9-12
                        [--merge false|true] [--state current|N]
molshredder show --representation REP [--selection EXPR]
                   [--replace false|true]
molshredder hide --representation REP [--selection EXPR]
molshredder as --representation REP [--selection EXPR]
molshredder toggle --representation REP [--selection EXPR]
molshredder analyze center [--selection EXPR] [--mode centroid|com]
                               [--precision 0..15] [--unit angstrom|nanometer]
                               [--result-name NAME]
molshredder measure fit --target-object OBJECT [--mobile-object OBJECT]
                        [--mobile-selection EXPR] [--target-selection EXPR]
                        [--match order|identifier] [--pairs 1:4,2:5,3:6]
                        [--weight uniform|mass] [--precision 0..15]
molshredder object align --target-object OBJECT [--mobile-object OBJECT] ...
molshredder object split [--object OBJECT] [--prefix PREFIX]
molshredder object split-states [--object OBJECT] [--first N] [--last N] [--prefix PREFIX]
molshredder object delete-states [--object OBJECT] --states "1-3 10"
molshredder object join-states --name NAME --objects a,b[,c] [--selection SELECTION] [--mode identical|intersection]
molshredder traj delete-frames [--object OBJECT] [--beg N] [--end N] [--skip N]
molshredder traj dup-frame [--object OBJECT] [--frame N]
molshredder edit move-atoms --selection SELECTION --values "..." [--mode add|set|transform] [--state current|all|N]
molshredder object bonds [--selection SELECTION] [--limit N]
molshredder select keywords
molshredder rep list|add|modify|delete|index|default|defaults|styles
molshredder object fix [--object OBJECT]
molshredder object free [--object OBJECT]
molshredder object sort
molshredder object matrix-copy --source OBJECT --target OBJECT
molshredder pbc mode [--mode status|none|wrap|unwrap] [--object OBJECT]
molshredder load append --path PATH [--object OBJECT] [--file-format FORMAT]
molshredder analyze charge [--kind formal|partial] [--selection EXPR] [--precision 0..15]
molshredder analyze mass [--selection EXPR] [--precision 0..15]
molshredder analyze phi-psi [--selection EXPR] [--precision 0..15]
molshredder object atoms [--selection EXPR] [--properties index,id,name,...] [--limit N]
molshredder object chains [--selection EXPR]
molshredder analyze gyration [--selection EXPR] [--mode centroid|com]
                               [--precision 0..15] [--unit angstrom|nanometer]
molshredder analyze inertia [--selection EXPR] [--mode com|centroid]
                               [--precision 0..15] [--unit angstrom|nanometer]
molshredder measure distance --from EXPR --to EXPR
                               [--mode atom|centroid|com|minimum|maximum|mean|closest]
                               [--pbc raw|minimum-image]
                               [--precision 0..15] [--unit angstrom|nanometer]
                               [--result-name NAME]
molshredder measure angle --first EXPR --vertex EXPR --third EXPR
                            [--pbc raw|minimum-image] [--precision 0..15]
                            [--result-name NAME]
molshredder measure dihedral --first EXPR --second EXPR --third EXPR --fourth EXPR
                               [--pbc raw|minimum-image] [--precision 0..15]
                               [--result-name NAME]
molshredder analyze sasa [--selection EXPR] [--probe-radius ANGSTROM]
                           [--samples N] [--evaluation-budget N]
                           [--unit square-angstrom|square-nanometer]
                           [--precision 0..15] [--result-name NAME]
molshredder analyze rdf [--first EXPR] [--second EXPR]
                          [--maximum-radius DISTANCE] [--bin-width DISTANCE]
                          [--normalization count|g-r] [--pbc raw|minimum-image]
                          [--evaluation-budget N] [--unit angstrom|nanometer]
                          [--precision 0..15] [--result-name NAME]
molshredder analyze trajectory rmsd-matrix [--selection EXPR]
                          [--fit-selection EXPR] [--first FRAME]
                          [--last FRAME] [--stride N] [--fit none|rigid]
                          [--weight uniform|mass] [--missing error|skip]
                          [--frame-pair-budget N] [--unit angstrom|nanometer]
                          [--precision 0..15] [--result-name NAME]
molshredder result list
molshredder result get --id ID
molshredder result show --id ID
molshredder result hide --id ID
molshredder result export --id ID --path PATH
                           [--output-format json|csv] [--overwrite false|true]
molshredder result delete --id ID
molshredder script run --path SCRIPT.py --trust true
                         [--arguments-json '["arg1","arg2"]']
                         [--working-directory DIRECTORY]
molshredder script run-isolated --path SCRIPT.py --trust true
                         [--arguments-json '["arg1","arg2"]']
                         [--working-directory DIRECTORY]
                         [--timeout-ms 30000]
                         [--max-output-bytes 8388608]
                         [--environment-policy minimal|inherit]
molshredder pml translate (--path SCRIPT.pml | --text "PML")
molshredder pml run (--path SCRIPT.pml | --text "PML") [--on-error stop|continue]
molshredder tcl translate (--path SCRIPT.tcl | --text "TCL")
molshredder tcl run (--path SCRIPT.tcl | --text "TCL") [--on-error stop|continue]
molshredder plugin list [--directory DIRECTORY] [--trust-store PATH]
molshredder plugin approve --manifest PATH [--allow-in-process false|true]
                           [--trust-store PATH]
molshredder plugin revoke --id ID [--trust-store PATH]
molshredder plugin status
molshredder plugin invoke --plugin ID --operation NAME
                          [--arguments-json '{}'] [--timeout-ms 60000]
                          [--trust-store PATH]
molshredder plugin NAMESPACE OPERATION [--PARAMETER VALUE ...]
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
Trajectory attach는 `--mapping exact|index|explicit`을 반드시 선택한다. Identity strength, stable-ID map,
topology version 및 channel/unit conversion result는
[Trajectory attachment contract](TRAJECTORY_ATTACHMENT.md)를 따른다.
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
- `load batch`: semicolon으로 구분한 여러 structure input을 모두 parse/build한 뒤 Workspace와 Scene에 한 번에
  commit한다. `--names`를 사용하면 path 수와 정확히 같아야 한다. 중간 parse 오류, duplicate name 또는
  cancellation에서는 어떤 object도 추가하지 않는다. Python과 Desktop multi-file Open도 같은 canonical
  operation을 사용한다. Resource와 stale-completion 계약은 [Bounded task execution](TASK_EXECUTION.md)을 참고한다.
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
- `show/hide/as/toggle`: object별 atom×representation visibility bitset을 같은 typed operation으로 바꾼다.
  `REP`은 `lines|sticks|spheres|ribbon|cartoon|everything|wire|licorice`다. Show는 선택 범위에 additive,
  hide는 subtractive, as는 선택 범위의 competing representation을 지운 뒤 요청 mask를 켠다. Toggle은 요청
  mask 중 선택 atom에 켜진 bit가 하나라도 있으면 선택 범위 전체를 끄고, 모두 꺼져 있으면 전부 켠다.
  `show --replace true`는 호환 문법으로 `as`와 같다. 모든 mutation은 packet rebuild 성공 후 state와 함께
  atomic commit되며 object visibility를 바꾸지 않는다. `everything`은 현재 구현된 다섯 primitive를 뜻한다.
  PyMOL의 `wire`/`licorice`에 포함되는 별도 nonbonded primitive는 아직 없으므로 각각 lines/sticks로
  축소되어 결과의 `resolved_representations`에 명시된다. Cartoon은 독립 STRIDE-method v0 assignment를 사용한다.
- `analyze center`: 재사용 가능한 scalar/vector analysis result를 계산한다. `centroid`와 `com`을
  구분하며 기본 selection은 `all`이다. COM은 topology의 explicit `mass` property를 우선하고 없을
  때 versioned estimated element-mass table을 사용하며 provenance를 결과에 포함한다.
- `measure fit`은 mobile object의 원자를 target object의 원자에 강체로 맞추는 4x4 행렬(행 우선, mobile → target)과 정렬 전후 RMSD를
  돌려주는 순수 결과다. 계산은 두 object의 scene 변환을 반영한 world 좌표와 현재 frame에서 하고(좌표 단위가 다르면 거부), 원자 쌍은
  `--match order`(선택 원자를 원자 순서대로 일대일, 개수 같아야 함), `--match identifier`(chain·잔기 번호·insertion code·원자 이름이
  같은 원자끼리) 또는 `--pairs m:t,...`(각 object의 1-based 원자 index 쌍; selection보다 우선)로 정한다. 쌍은 3개 이상이고 한 직선 위에
  있으면 안 된다. `--weight mass`는 mobile 원자의 질량을 가중치로 쓴다. `object align`은 같은 계산 뒤 결과를 mobile object의 scene
  `local_transform`으로 적용한다(Q-035 a). 좌표는 바뀌지 않으며 `view reset --object`로 되돌릴 수 있고 session journal로 재생된다. 같은 object끼리,
  없는 object, 쌍이 부족한 요청은 아무것도 바꾸지 않는 오류다. 서열·CE 기반 구조 정렬(`align`, `super`, `cealign`)은 제공하지 않는다.
- `object split-states`는 multi-state(다중 모델) object의 state마다 단일 state object `<prefix><state 4자리>`(prefix가 없으면 `<원본>_`)를
  만든다(`--last 0`은 마지막 state까지). 원본은 남고, 대상 이름 하나라도 있으면 아무 것도 만들지 않는다. `object delete-states`는 `1-3 10`
  또는 `1-3,10` 형식의 state(1부터, 범위는 양 끝 포함, 범위 밖 번호는 무시)를 지우되 모든 state를 지우는 요청은 거부한다(PyMOL은 state
  0개 object를 허용). `object join-states`는 여러 object의 state를 이어 붙여 새 multi-state object를 만든다. `--mode intersection`(기본,
  PyMOL mode 2)은 모든 object에 있는 원자만 첫 object의 순서로 chain·잔기·원자 이름으로 맞춰 남기고, `identical`(mode 1)은 원자가 모두
  같아야 한다. 이산(discrete) 합치기와 서열 정렬 합치기는 제공하지 않는다. 연결된 trajectory나 PBC 처리 중인 object는 거부한다.
- `traj delete-frames`는 정적·다중 모델 object의 frame(0부터)을 `[beg, end]`(end -1은 마지막) 범위에서 지우되 `--skip N`(N≥1)이면
  범위 시작부터 N번째마다 남긴다. 모든 frame을 지우는 요청은 거부한다. `traj dup-frame`은 frame(기본: 현재)의 복사본을 마지막에 추가한다.
  연결된 trajectory(`traj load`)와 PBC 처리 중인 object는 거부한다. `object fix`는 object의 scene 배치를 고정해 `object align`·`object matrix-copy`
  대상·`view reset --object`가 거부되게 하고 `object free`가 해제한다(`object list`의 `fixed` 열, `view reset --object all`은 고정된 object를 건너뜀).
- `edit move-atoms`는 활성 정적 object의 선택 원자를 옮긴다(VMD `moveto`·`moveby`·`lmoveto`·`lmoveby`·`move`, PyMOL `translate`·`transform_selection`).
  `--mode set`은 값을 위치로 대입하고(숫자 3개면 모든 원자가 같은 위치, 3×원자 수면 원자별 위치), `add`는 오프셋을 더하며, `transform`은 행 우선 4×4 affine 행렬
  16개를 적용한다(마지막 행은 `0 0 0 1`). 값은 공백·쉼표·세미콜론으로 구분한다. `--state`는 `current`(기본)·`all`·1부터의 state 번호다. 하나의 undo 항목이고(`edit undo`)
  object의 측정은 다른 좌표 편집처럼 지워진다. 연결된 trajectory·PBC 처리·고정(`object fix`) object는 거부한다. `object bonds`는 선택 원자에 닿는 결합을 원자 번호(1부터)·id·이름·차수로
  나열하고, `select keywords`는 selection 문법의 keyword와 별칭, 예시를 나열한다.
- `rep`는 VMD식 representation instance(Q-038 a)다. object마다 번호(0부터)와 이름(`rep0`…)을 가진 순서 있는 목록이며 PyMOL식 `show/hide`와 독립으로 함께 그려진다.
  `rep add`는 `rep default`(style=Lines, color=Name, selection=all, material=Opaque)로 새 instance를 만들고 `--style --params --selection --color --color-id --material --name`으로 덮어쓴다.
  `rep modify --rep 번호|이름`이 style·params·selection·color·visible·name·selection-update(끄면 설정 시점의 원자를 고정)·color-update·smooth·draw-frames·scale-min/max/auto를
  바꾼다. 그려지는 style은 Lines·Bonds·DynamicBonds·VDW·CPK·Licorice·Trace·Tube·Ribbons·NewRibbons·Cartoon·NewCartoon이고 나머지 VMD style 이름은 저장만 한다(`style_drawn`).
  색 방식은 Name·Type·Element·ResName·ResType·ResID·Chain·SegName·Conformation·Molecule·Structure·ColorID·Beta·Occupancy·Mass·Charge·Pos·PosX/Y/Z·Fragment·Index를 그린다(`color_drawn`).
  VMD color id(0~32)의 색은 MolShredder의 근사값이다. material·smooth·draw-frames·clip plane은 저장 또는 미지원이다. `rep styles`가 이름 목록을 보여 준다.
- 파일 형식(SEQ-319): `load`/`load batch`는 `.mmtf`(MMTF 0.x·1.x 읽기, 1.0 쓰기: `save --file-format mmtf`, `--frames all`이면 모델마다 한 state), `.pdbqt`(AutoDock),
  `.cor`/`.crd`(CHARMM 좌표, 제목 줄 `*`), `.cc1`(ChemDraw 3D), `.vdb`(VIPERdb = PDB)를 읽는다. MMTF는 결합 차수를 1~3만 저장하므로 aromatic 등은 single로 쓰고 `save`의 손실 표에 보고한다.
  `volume load --file-format`은 X-PLOR/CNS(`xplor`·`cns`; `.map`이 title block을 가진 텍스트면 X-PLOR), BRIX/DSN6(`brix`·`dsn6`·`omap`; PyMOL 기본처럼 평균 0·표준편차 1로 정규화하고 원래 값은 metadata에 기록),
  Delphi `phi`, ASCII `grd`, AVS `fld`, `acnt`를 더한다(모두 읽기 전용, 한 파일에 한 grid).
  구조 형식은 `.cml`(CML: `molecule`마다 object, `atomArray`/`bondArray`의 요소형·배열형, `atomRefs2`·`atomsRefs2`, `x2/y2` 평면 좌표는 z=0), `.pdbml`(PDBx/XML의 `atom_site`·`cell`·`struct_conn`;
  `<PDBx:datablock>` 파일은 `--file-format auto`가 내용으로 알아본다), `.mmd`/`.mmod`(MacroModel 고정 열; 같은 원자 구성을 가진 연속 블록은 state, 읽기·쓰기),
  `.mae`(Maestro `f_m_ct`/`m_atom`/`m_bond`; 구조마다 object, 읽기·쓰기), `.pse`(PyMOL 세션, 읽기 전용)를 더한다. XML은 자체 파서가 DOCTYPE 내부 subset·ENTITY 선언·사용자 정의 엔티티를 거부한다
  (XXE·엔티티 확장 불가, 깊이·요소 수 제한). MMOD 쓰기는 MacroModel 원자 type을 원소·결합·전하에서 고르며(어림값) 표현할 수 없는 원소는 type 64로 쓰고 `save` 손실 표에 알린다.
  `.dat`/`.out`은 확장자만으로 MMOD로 보지 않으므로 `--file-format mmod`를 지정한다. MAE는 비압축 파일만 읽는다(`.maegz` 미지원).
  PSE는 Python pickle이지만 **코드를 실행하지 않는다**: 데이터 opcode만 해석하는 제한 unpickler가 `_codecs.encode`(latin1)와 `pymol.Session_Storage`만 허용하고 그 밖의 global·호출은 오류로 거부한다.
  molecule object(모든 state, 결합, 원자 식별자, 전하, B-factor·occupancy, 이차구조, 단위 셀, 숨김 상태), 원자별 표현(sticks·spheres·lines·ribbon·cartoon)과 이름 있는 selection을 가져오고
  surface·mesh·dots·label·nonbonded 표현, map·CGO·measurement 같은 다른 object, 색·setting·scene·movie·카메라 view는 `import_notes`에 보고만 한다.
  원자 표가 binary dump(`pse_binary_dump`)로 저장된 세션은 오류로 거부하고 `set pse_binary_dump, 0`으로 다시 저장하라고 안내한다. `.pze`(gzip)는 지원하지 않는다.
- `object split`은 chain마다 새 object(`<prefix><chain>`, prefix가 없으면 `<원본>_<chain>`)를 만든다. chain이 하나이거나 chain 식별자가 비어
  있거나 대상 이름 하나라도 이미 있으면 아무 것도 만들지 않는 오류이고 중간에 실패하면 만든 object를 지운다. `object sort`는 object panel 순서를
  이름의 natural order(`s2` < `s10`)로 바꾸고 object ID는 그대로다. `object matrix-copy`는 한 object의 scene 배치(이동·회전·scale·pivot)를 다른
  object에 복사한다(좌표 불변, `view reset --object`로 되돌림). 확장자는 대소문자와 상관없이 `.pdb .ent .brk`를 PDB, `.cif .mmcif`를 mmCIF 등으로 읽는다.
- `pbc mode`는 object의 frame을 단위 세포로 접은(`wrap`) 상태, atom 연속성으로 펼친(`unwrap`) 상태, 읽은 그대로(`none`)로 보여 준다
  (Q-035 a). 파일과 저장된 좌표는 바뀌지 않고 렌더링·선택·분석·저장이 모두 처리된 frame을 읽는다. 모든 frame에 unit cell이 있어야 한다.
  `unwrap`은 frame을 순서대로 읽을 때 O(1), 건너뛰면 처음부터 다시 계산하며 5만 frame을 넘는 재계산은 거부한다. 모드가 켜진 동안에는
  원자 편집·`object remove-atoms|extract`·`load append`·`edit undo|redo`를 거부하고(`pbc mode --mode none` 후 가능), 복사본은 처리된 좌표를
  그대로 담는다. `--mode status`는 상태만 알려 준다. 분자 단위 unwrap과 중심 지정은 제공하지 않는다.
- `load append`는 구조 파일의 모든 state를 기존 object에 새 state로 추가한다. 원자 수·원자 이름·원소·잔기 이름·번호·chain·insertion
  code가 정확히 같아야 하며 하나라도 다르면 첫 불일치를 알려 주고 object를 바꾸지 않는다. 단일 state object와 다중 모델 파일에서 연
  object(embedded trajectory)만 대상이고, `traj load`로 붙인 trajectory가 있는 object는 거부한다. state 수는 10만 개를 넘을 수 없고 현재
  frame 위치는 유지된다.
- `analyze charge`는 selection의 formal(topology) 또는 partial(`partial_charge` property; 없으면 오류) 전하 합을, `analyze mass`는
  `analyze center --mode com`과 같은 질량 규칙(topology `mass`, 없으면 추정 원소 질량)과 provenance로 질량 합을 반환한다.
  `analyze phi-psi`는 N·CA·C를 가진 residue마다 phi(앞 residue의 C 필요)와 psi(다음 residue의 N 필요)를 도 단위로 돌려준다.
  앞뒤 residue는 C(i-1)–N(i) 거리가 2.0 Å 미만일 때만 연결된 것으로 보며 끝이나 사슬 끊김에서는 null이다.
  각도 부호는 IUPAC 규약(독립 공식으로 test)이다.
- `object atoms`는 PyMOL `iterate`/`iterate_state`/`identify`/`index`를 코드 평가 없이 대신하는 읽기 전용 표다. `--properties`는
  `index id source_id name element resn resi chain segment altloc b q partial_charge formal_charge x y z mass radius atomic_number bond_count`
  중에서 쉼표로 고르고(좌표는 현재 frame), 없는 optional property는 null이며 `--limit`(기본 1000)를 넘으면 `truncated`와 전체 선택 수를 알려 준다.
  `object chains`는 selection에 걸리는 chain 식별자를 처음 나타나는 순서로 돌려준다.
- `analyze gyration`, `analyze inertia`: active object의 selection을 현재 frame에서 읽어 회전 반경
  (`sqrt(sum(w|r-c|^2)/sum(w))`)과 중심 기준 관성 텐서의 principal moment(오름차순)·axis를 반환하는 pure result다.
  `centroid`는 가중치 1, `com`은 `analyze center`와 같은 mass 규칙과 provenance를 쓴다. Axis는 오른손 직교 기저이며
  각 axis의 절대값이 가장 큰 성분이 양수가 되도록 부호를 고정한다. 퇴화(`degenerate`)이면 그 사실을 보고한다. Moment는
  `질량 단위*길이^2`(centroid는 `unit^2`)이며 `--unit`을 따른다. 빈 selection, 누락 atom(기본 error)은 오류다.
- `measure distance`: scene/session에 남는 measurement를 만든다. 두 endpoint는 atom 또는 selection
  expression이고 mode가 reduction semantics를 지정한다. 현재 실행되는 첫 slice는 endpoint마다
  정확히 한 atom을 선택하는 `mode=atom`이며 `pbc=raw|minimum-image`를 지원한다. Minimum-image는
  active frame의 orthorhombic/triclinic unit cell을 요구한다.
- `measure angle`/`measure dihedral`: 각각 순서가 지정된 원자 선택 3개 또는 4개를 받아 degree 단위의
  current-frame geometry result를 만든다. 각 selection은 정확히 한 원자여야 하며 `raw`와
  orthorhombic/triclinic `minimum-image`를 지원한다. Angle은 안정적인 cross/dot `atan2`로
  `[0, 180]`, dihedral은 projected-vector signed `atan2`로 `[-180, 180]` 범위를 사용한다.
  중복 원자, missing coordinate, zero-length vector와 collinear torsion은 명시적 오류이고 result를
  남기지 않는다.
- `analyze sasa`: active frame에서 deterministic Fibonacci sphere를 사용하는 Shrake–Rupley SASA를
  계산한다. 기본 probe radius는 1.4 Å, 기본 sample 수는 atom당 960개이며 `evaluation-budget`으로
  pair/point occlusion 평가량을 제한한다. 선택 원자의 표면은 선택 밖의 present atom에도 가려진다.
  Radius는 PQR radius, explicit VDW radius, versioned element table 순으로 정하고 출처를 결과에 남긴다.
  출력은 Å² 또는 nm²이고 현재 PBC image를 만들지 않는 `raw` active-frame 계산만 지원한다.
  선택 원자의 missing coordinate는 오류이며 선택 밖 missing occluder는 count를 남기고 건너뛴다.
- `analyze rdf`: active frame의 두 selection 사이 거리를 half-open `[lower, upper)` bin으로 집계한다.
  `second`를 생략하면 같은 selection의 unordered distinct pair만 한 번 센다. 별도 selection은 self pair를
  제외한 ordered first→second pair다. `count`는 raw 또는 minimum-image에서 pair count를 반환한다.
  Dimensionless `g-r`은 finite cell volume이 있는 `minimum-image`에서만 허용하고
  `count/(eligible_pairs×shell_volume/cell_volume)`으로 정규화한다. Maximum radius는 triclinic cell의 최소
  높이 절반 이하여야 한다. Missing atom은 count와 함께 건너뛰며 budget/cancellation 실패는 result를 남기지 않는다.
- `analyze trajectory rmsd-matrix`: inclusive first/last/stride frame 집합의 all-pairs RMSD를 계산하고
  diagonal 포함 upper triangle만 저장한다. `fit=rigid`는 각 off-diagonal mobile frame을 reference frame에
  Horn quaternion rigid fit한 뒤 별도 score selection으로 RMSD를 계산하지만 좌표와 viewport를 변경하지 않는다.
  `N(N+1)/2` frame-pair 수가 budget을 넘으면 frame을 읽거나 partial table을 publish하기 전에 거부한다.
  Collinear/insufficient fit selection처럼 회전이 유일하지 않은 입력은 명시적 오류다.
- `result list/get/show/hide/export/delete`: analysis 결과와 provenance, stale source 상태 및 독립적인
  viewport overlay를 관리한다. 상세 schema와 atomic export 계약은
  [Persistent analysis results](ANALYSIS_RESULTS.md)에 둔다.
- `view get/set`: target, normalized quaternion orientation, distance, projection, FOV/orthographic height,
  별도 model-space rotation origin, aspect ratio와 near/far clip으로 구성된 validated camera snapshot을 조회·부분 갱신한다. Validation 실패는
  이전 snapshot을 보존한다. Quaternion component를 직접 바꿀 때에는 결과 네 component가 unit quaternion이어야 한다.
- `view center/zoom/orient/origin`: active object에서 selection을 평가하고 missing coordinate는 제외한다.
  `state=current`는 현재 trajectory frame(기본값), `all`은 알려진 모든 coordinate frame,
  양의 정수는 1-based explicit state를 사용한다. PyMOL script 이식을 위해 `-1=current`, `0=all`도
  받는다. PyMOL은 `state`를 생략하면 모든 state를 frame하지만 MolShredder 기본은 현재 state다(`--state all`로 PyMOL과 같게 쓸 수 있다).
  All-state extent는 trajectory를 메모리에 전체 복사하지 않고 frame source/cache에서 streaming하며,
  exact complete-fit radius를 위해 bounds/radius 두 pass를 수행한다. Result의 `evaluated_frame_count`로 실제
  평가 frame 수를 확인할 수 있다.
  Center는 PyMOL처럼 선택 atom 좌표 평균으로 target을 옮기며 `move-origin=true`가 기본이다. `move-origin=false`는
  회전 origin의 깊이를 유지한다. Zoom은 같은 평균을 중심으로 축별 `max(mean-min, max-mean)` 중 최댓값(weighted
  half extent)을 radius로 사용하고 `complete=true`이면 평균에서 가장 먼 atom center까지의 거리를 사용한다. `buffer`는
  Angstrom 단위의 절대 여백이며 radius 최소값은 2.5 Angstrom(PyMOL `MAX_VDW`)이다. Distance는
  `radius / tan(fov/2)`, clip은 `distance ± 1.2 radius`다. Orient는 uniform-coordinate covariance의 principal axes를
  variance 내림차순으로 camera right/up/backward에 맞춘 후 zoom과 같은 평균·weighted extent로 frame한다. 축 부호는 현재 camera에서 회전 변화가 가장 작은 조합을
  선택하고, eigenvalue가 퇴화한 subspace는 현재 camera 축을 투영해 갑작스러운 회전을 피한다. Origin은 기본적으로
  selection AABB midpoint를 camera의 model-space rotation pivot으로 사용한다. `--position x,y,z`는 selection 없이
  명시 좌표를 사용한다. `--object current|NAME|ID`를 함께 주면 camera 대신 해당 object의 local transform pivot을
  설정하며, position이 없으면 그 object 안에서 selection/state를 평가한다. Pivot만 바꿀 때 현재 affine placement는
  유지한다. Empty selection은 기존 camera/scene을 보존한 채 `invalid_selection`으로 실패한다.
  Orient, zoom, center, origin, clip, turn, move, projection과 reset의 18-value view는 PyMOL 3.1.0 capture와
  float32 오차(1e-4) 안에서 같다.
- `view reset`: object option이 없으면 identity orientation과 기본 clip/target을 복원하고 모든 effectively-visible molecular object의
  current frame 좌표 평균과 weighted extent(volume grid bounds 포함)로 frame한다. PyMOL에서 setting인 projection
  mode와 field of view, live viewport aspect ratio는 보존하며 molecule이나 volume이
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
  off-axis target과 eye도 같이 회전한다. 회전 방향은 PyMOL `turn`과 같아(view 회전 행렬에 `Rᵀ` 적용) 양의
  각도에서 camera quaternion은 음의 각도만큼 회전한다. Invalid/non-finite 요청이나 z 축이 camera/clip invariant를
  넘는 요청은 기존 camera를 보존하고 실패한다.
- `view projection`: `--mode perspective|orthographic`와 degree 단위
  `--field-of-view-degrees`를 제공한다. 기본 `--preserve-scale true`는 전환 직전 target-plane vertical span을
  유지한다. Perspective→orthographic은 그 span을 orthographic height로 사용하고, orthographic→perspective 또는
  perspective FOV 변경은 distance를 다시 계산하며 near/far clip도 distance와 같은 비율로 조정한다. Target,
  orientation과 model origin은 유지한다. `--preserve-scale false`는 projection/FOV field만 바꾸는 raw switch다.
- `stereo set/get/modes`: `stereo_shift`(objective distance의 %)와 `stereo_angle` scale을 validated state로
  관리한다. 현재 QRhi renderer는 `side_by_side`, `crosseye`, `walleye`, `anaglyph`, `row_interleaved`,
  `column_interleaved`, `checkerboard`를 실제 렌더링한다. Anaglyph는 `true`, `gray`, `color`, `half_color`,
  `optimized` 색상 조합을 선택한다. Interleaved mode는 global device-pixel parity를 사용하고 eye swap으로
  display phase를 반전할 수 있다. 다른 알려진 mode는
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
- `scene store/recall/list/delete/clear`: object/volume/trajectory/result/edit state, representation/settings/selection,
  camera/stereo와 stable identity를 포함한 full `Workspace` snapshot을 이름으로 관리한다. Recall은 detached candidate를
  publish하며 실패하면 현재 Workspace를 보존한다. Camera-only `view` inventory와는 독립적이다. Snapshot은 atom 색(`color usage`로
  확인), group, visibility를 포함하므로 recall이 이를 그대로 복원한다.
- Scene order: scene 목록은 이름순이 아니라 저장 순서(scene order)다. `scene store --insert end|before|after`는 현재 scene 앞/뒤에 넣고
  (이미 있는 이름을 다시 저장하면 위치 유지), `scene update`는 현재 scene을 지금 상태로 같은 자리에 다시 저장한다(현재 scene이 없으면 오류).
  `scene rename --name A --new-name B`는 위치·현재 표시·movie keyframe 참조를 함께 옮긴다. `scene first|last|next|previous`는 순서를
  따라 recall하며 next/previous는 `--wrap true`(기본)일 때 끝에서 반대편으로 돌고, 현재 scene이 없으면 next는 처음, previous는 끝에서
  시작한다. `scene order --names "a b" --location top|bottom|current [--sort true]`는 지정한 scene(없으면 `--sort`일 때 전체)을 이동하고,
  `current`는 현재 scene 앞에 둔다. `--sort`는 숫자 덩어리를 값으로 비교하는 natural sort(s2 < s10)다. 없는 scene·알 수 없는 위치·
  이름도 `--sort`도 없는 요청은 순서를 바꾸지 않는 오류다.
- `movie configure/keyframe/seek/play/pause/step/status/clear`: 1-based bounded timeline에 named scene과 optional 0-based
  trajectory frame만 연결한다. 임의 command/script payload는 허용하지 않는다. `seek`와 `step`은 scene recall 및
  trajectory seek를 하나의 failure-atomic transition으로 적용한다.
- `session save/load/autosave`: current canonical mutation journal, final camera/stereo, named scene/movie 및
  `ui.visible-panels` metadata를 schema 2 `.msess`로 저장한다. Load는 explicit recovery와 exact missing-path relink를
  지원한다. 자세한 migration/security/file transaction은 [Session format](SESSION_FORMAT.md)을 따른다.

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
| `angle` | `measure angle` | 없음 |
| `dihedral` | `measure dihedral` | 없음 |
| `sasa` | `analyze sasa` | 없음 |
| `rdf` | `analyze rdf` | 없음 |

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
넣어 재현할 수 있다. Replay는 wall-clock animation을 기다리지 않고 committed endpoint를 복원한다. Full-state
named scene, typed movie track와 bounded failure-atomic session file operation도 같은 Registry에서 제공한다.

## 호환성 정책

- Grammar v1 canonical leaf name은 `load`, `select`, `show`, `analyze center`, `measure distance`다.
- `open`, `center`, `centroid`, `com`, `dist`, `distance`는 convenience alias이며 session/history에는
  저장하지 않는다.
- 기존 invocation의 의미를 바꾸는 command/parameter rename, default 변경 또는 choice 제거는
  grammar version과 session migration이 필요하다.
- 새 optional parameter나 representation/format choice를 추가하는 것은 기존 normalized invocation을
  바꾸지 않는 범위에서 additive change로 처리할 수 있다.
- `--format`은 presentation option이므로 domain invocation과 session provenance에 포함하지 않는다.
