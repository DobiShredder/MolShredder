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

Color는 `-1`, `atom`, `atomic`, `carbon`, `hydrogen`, `nitrogen`, `oxygen`,
`sulfur`, 기본 named color 또는 `#RRGGBB`를 받는다.

## Geometry contract

- Lines는 half-bond color, width, radius, long-bond hide, valence multi-stroke와 zero-order
  `skip`/`dashed`/`solid`을 적용한다.
- Sticks는 radius, hydrogen radius scale, transparency, color, ball-and-stick enable/ratio/color를
  적용한다.
- Spheres는 scale, transparency, color와 requested/effective mode/fallback provenance를 적용한다.
- Object/state/atom/bond override는 각 half-bond에 독립적으로 resolve된다.
- 좌표와 radius의 내부 단위는 nm이며 setting의 Angstrom 값은 geometry 생성 시
  nm로 변환된다.

## Persistence와 현재 한계

Snapshot schema 2는 stable atom/bond ID 의미, catalog revision과 정렬된 override 목록을 보존한다. Parser와
Workspace restore는 schema/catalog 불일치, duplicate override, malformed value 및 존재하지 않는
object/state/atom/bond target을 거부하고 기존 state를 유지한다.

Analytic impostor sphere backend는 구현되었지만 point/cube/tetrahedron/triangle mode는
backend capability가 없으면 명시적 fallback으로 기록된다. Shader/quality/endpoint
tuning과 일부 legacy mode의 PyMOL-exact 외관은 아직 reference capture로 인증되지 않았다.
따라서 공개 capability 지원표에서 관련 항목은 `In progress`로 유지하며
PyMOL compatibility 또는 완전 지원을 주장하지 않는다.
