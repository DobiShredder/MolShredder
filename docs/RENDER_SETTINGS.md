# Render setting service

MolShredder의 P0 render-setting service는 lines, sticks와 spheres에 관련된 43개 setting의
definition, override와 resolution을 C++ core에서 관리한다. 같은 canonical
operation을 CLI, Desktop GUI와 Python에서 호출하며 geometry rebuild와 session replay에
연결된다.

## Catalog contract

Catalog schema version은 `1`, revision은 `pymol-oss-3.1.0-p0-v1`이다. 각 definition은 다음
정보를 가진다.

- stable numeric ID와 이름
- `boolean`, `integer`, `number`, `color` 중 하나인 value type
- default value, 허용 범위와 unit
- override가 허용되는 가장 구체적인 scope

ID, 이름, type과 default는 pinned PyMOL Open Source 3.1.0 `SettingInfo`를 조사한 결과에
연결된다. MolShredder의 range는 non-finite 값과 안전하지 않은 geometry parameter를 거부하기
위한 자체 validation contract다. PyMOL의 모든 clamp 동작과 호환된다는 의미는 아니다.

## Scope와 resolution

Override scope는 `global`, `object`, `object_state`, `atom`, `bond`다. Resolution은 다음 순서로
처음 발견한 값을 사용한다.

```text
bond > atom > object_state > object > global > definition default
```

`unset`은 값을 다른 곳으로 복사하지 않고 해당 override만 제거하므로 다음 상위 scope의 값이
다시 보인다. Scope reset은 정확히 일치하는 scope의 override를 한 transaction으로 제거한다.
잘못된 key, type, range, scope 또는 object target은 기존 snapshot을 변경하지 않는다.

내부 state index는 0-based이지만 사용자가 CLI/Python으로 입력하는 명시적 state는 1-based다.
Atom/bond target은 ordinal이 아니라 현재 topology가 발급한 non-zero 64-bit stable ID다.
`current`는 현재 object/state를 지정한다. Atom reorder 뒤에도 override는 같은 identity를
따라가며 삭제된 atom/bond override는 topology transaction에서 제거된다.

## Public operation

```text
setting list
setting get   --name NAME --scope global|object|state|atom|bond [--target N]
setting set   --name NAME --value VALUE --scope SCOPE [--target N]
setting unset --name NAME --scope SCOPE [--target N]
setting reset --scope SCOPE
```

Object/state scope는 현재 object/state를 사용한다. Atom/bond scope에서는 `--target`이
필수다. Python은 같은 command와 parameter map을 `molshredder.invoke`에 넘긴다.
Desktop의 Settings editor도 동일 operation을 호출한다. 잘못된 type, range, color,
scope 또는 target은 setting state와 scene packet 모두를 변경하지 않는다.

Color는 `-1`/`default`/`atom`/`atomic`(원자 색), `object`(object 색), color table 이름 또는 index,
`0xRRGGBB`, `#RRGGBB`를 받는다. 이름은 [Color](COMMANDS.md#color) 규칙과 같은 table로 해석하며 `color define`으로
만든 이름도 쓸 수 있다. `front`는 배경의 반전색, `back`은 배경색이며 배경을 바꾸면 다시 계산된다.

Catalog revision `pymol-oss-3.1.0-p0-v2`는 label·measurement setting 13개를 더한다: `label_color`(기본 front), `label_size`(14 px,
음수는 world 단위), `label_font_id`(5), `label_position`·`label_placement_offset`(vector3, Å, camera 공간), `label_digits`(1),
`label_distance_digits`·`label_angle_digits`·`label_dihedral_digits`(−1), `dash_color`, `dash_length`(0.15 Å), `dash_gap`(0.45 Å),
`dash_width`(2.5 px). PyMOL처럼 범위 제한은 없고 유한한 값만 받는다. Vector 값은 `x,y,z`, `x y z` 또는 `[x, y, z]`로 쓴다.

Catalog revision `pymol-oss-3.1.0-p0-v3`은 lighting·material·effect·image setting 12개를 더한다: `ambient`(0.14), `direct`(0.45),
`reflect`(0.45), `light`(vector3, −0.4 −0.4 −1), `light_count`(2, 1~10), `two_sided_lighting`(−1), `antialias`(1), `depth_cue`(on),
`transparency_mode`(2), `opaque_background`(off), `ray_opaque_background`(−1), `ray_default_renderer`(0). 현재 Desktop 음영에는
반영하지 않고, `image save`가 `ambient`/`light`만 쓴다.

Catalog revision `pymol-oss-3.1.0-p0-v4`는 `transparency`(0, atom), `surface_color`(−1, atom), `isosurface_algorithm`(0, 0~2)를
더한다. `transparency`와 `surface_color`는 `surface show` 때 적용한다.

Catalog revision `pymol-oss-3.1.0-p0-v5`는 `auto_color`(238, global boolean, MolShredder 기본 off)를 더한다. 켜면 새로 load한
object의 carbon은 object auto color, 다른 원소는 원소 색을 받는다(PyMOL 기본 on).

Catalog revision `pymol-oss-3.1.0-p0-v6`은 cartoon/ribbon geometry setting 6개를 더한다: `cartoon_sampling`(91, −1=atom 수에 따라
7/5/3/2), `cartoon_rect_length`(96, 1.4), `cartoon_rect_width`(97, 0.4), `cartoon_gap_cutoff`(750, 10), `ribbon_sampling`(19, 1),
`ribbon_width`(106, 3 pixel).

같은 revision은 측정 arc 크기 `angle_size`(405, 0.6666)와 `dihedral_size`(407, 0.6666)도 더한다. 측정 distance/angle/dihedral은 PyMOL처럼
기본 노랑 dash로 그리며(끝점 구 없음), angle arc는 짧은 arm×`angle_size` 반지름, dihedral은 중심 결합 중점의 반지름 선과 arc로 그린다.
결합되지 않은 arm(2.1 Å 초과)에는 실선이 더해진다. PyMOL VRML export와 5e-3 Å 안에서 같다(`tests/render/measure_parity_test.py`).

## Geometry contract

- Lines는 half-bond color, width, radius, long-bond hide, valence multi-stroke와 zero-order
  `skip`/`dashed`/`solid`을 적용한다. `valence_mode 1`의 비말단 다중 결합은 전체 선과 plane 원자 쪽 안쪽 선(양 끝
  `valence_size` 비율만큼 짧고 2·valence_size, 삼중은 4·valence_size 떨어짐)이고, 말단이거나 mode 0이면 ±valence_size
  대칭 선이다. Aromatic은 14~40%, 60~86% 안쪽 dash다. Dashed zero-order line은 0.15 Å dash 짝수 개다.
- Sticks는 radius, hydrogen radius scale, transparency, color, ball-and-stick enable/ratio/color를
  적용한다. 한쪽이 hydrogen이면 결합 전체에 `stick_h_scale`을 곱한다. 이중 결합은 반지름 R·scale/2.5 tube 두 개를
  ±1.5r에, 삼중은 R·scale/3.5 tube 세 개를 2.5r 간격에 둔다. Dashed zero-order stick은 길이 3·간격 6 zero 반지름
  dash 짝수 개다(양 끝 간격 포함).
- 위 primitive 규칙은 PyMOL 3.1.0 VRML export와 5e-3 안에서 비교한다(`tests/render/geometry_parity_test.py`, Q-019 a).
- Spheres는 scale, transparency, color와 requested/effective mode/fallback provenance를 적용한다.
- Object/state/atom/bond override는 각 half-bond에 독립적으로 resolve된다.
- 좌표와 radius의 내부 단위는 nm이며 setting의 Angstrom 값은 geometry 생성 시
  nm로 변환된다.

## Persistence와 현재 한계

Snapshot schema 2는 stable atom/bond ID 의미, catalog revision과 정렬된 override 목록을 보존한다. Parser와
Workspace restore는 schema/catalog 불일치, duplicate override, malformed value 및 존재하지 않는
object/state/atom/bond target을 거부하고 기존 state를 유지한다.

Analytic impostor sphere backend는 구현되었지만 point/cube/tetrahedron/triangle mode는
backend capability가 없으면 명시적 fallback으로 기록된다. Sphere/stick/line primitive(위치·반지름·색·연결)와 P0
geometry setting은 PyMOL export와 비교해 지원표에서 verified다. Shader 품질·음영 같은 화면 외관은 primitive 비교
범위 밖이다.
