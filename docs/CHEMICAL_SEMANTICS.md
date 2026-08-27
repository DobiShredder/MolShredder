# Chemical semantics contract

MolShredder topology는 format-specific integer를 unknown/single bond로 낮추지 않고 표현 가능한
화학 의미를 typed field로 보존한다. 현재 core atom contract는 atomic number, formal charge/value
presence, isotope mass number, radical state, atom stereo parity와 annotation origin을 표현한다.
Partial charge는 formal charge와 다르며 unit과 source가 있는 `partial_charge` atom property로 유지한다.

Bond contract는 single/double/triple/aromatic/amide/zero/unknown/query order, query constraint,
bond stereo와 order annotation origin을 갖는다. Query order는 반드시 `single_or_double`,
`single_or_aromatic`, `double_or_aromatic`, `any` 중 하나와 함께 있어야 하며 다른
order에 query constraint를 붙이는 draft는 builder가 거부한다.
Bond endpoint는 stable atom index 순서로 canonicalize한다. 이때 방향성 wedge의 물리적
의미를 유지하기 위해 endpoint가 뒤집히면 `up`과 `down`을 함께 반전한다. Stable-ID
retain/reorder도 같은 invariant를 적용한다.

## MOL/SDF V2000 vertical slice

- Atom parity 0–3, `M  CHG`, positive `M  ISO`와 radical 1–3을 core field에 저장한다.
- Bond type 1–8과 stereo code 0/1/3/4/6을 exact typed enum으로 읽고 다시 쓴다.
- Core field와 이전 `sdf.*` compatibility property가 둘 다 있는데 값이 다르면 writer는
  임의로 하나를 선택하지 않고 오류를 반환한다.
- MOL V3000, query atom, enhanced stereo collection, Sgroup과 reaction record는 이 slice의 지원
  범위가 아니다.

Conservative perception kernel v1은 load와 분리된 명시적 단계다. Å로 변환한 좌표와 main-group
covalent radius로 없는 connectivity만 inferred bond proposal로 만들고, ring edge,
valence와 implicit-hydrogen assessment를 반환한다. 5/6원 C/N cycle이 1.25–1.50 Å bond와 0.15 Å
planarity 조건을 만족하면 새/inferred bond 또는 non-explicit unknown order만 aromatic으로 제안한다.
Explicit single/double/user order는 조건을 만족해도 바꾸지 않는다. Rule set/version,
assumption, warning, evaluated-pair count와 pair budget을 보존하며 apply는 같은 topology version의
proposal만 받아 explicit/user annotation을 변경하지 않는다. 이 kernel의 canonical frontend operation과
더 넓은 aromatic/organometallic rule은 아직 완료 범위가 아니다. Read-only
`object perceive-chemistry`는 connectivity toggle, radius scale과 positive pair budget을 typed parameter로 받고
schema v1 provenance, assumption/warning, proposal table과 assessment count를 반환한다. TaskContext cancellation과
progress를 kernel scan에 전달하며 GUI·CLI·Python이 같은 command를 호출한다. Mutation apply는 아직 public
frontend에서 `--apply true`로 명시해야 한다. Apply transaction은 bond, bond-order 또는 residue proposal이 있을 때 topology version을
증가시키고 MolecularSystem, selection/visibility remap, trajectory topology reference, representations와 scene candidate가
모두 준비된 뒤 한 번에 commit한다. Desktop은 proposal 결과를 먼저 보여준 뒤에만 Apply control을 노출한다.

Residue core contract는 component `ResidueKind`와 chain-level `PolymerType`, annotation origin을 분리한다.
Rule v1은 표준 amino/nucleic/solvent/ion/common carbohydrate 이름을 component kind로 분류하고 나머지는
ligand로 제안한다. Polymer membership은 같은 chain에 같은 family residue가 둘 이상 있을 때만 protein,
DNA, RNA 또는 carbohydrate로 제안한다. Existing explicit/user classification은 그대로 보존하고 selection
chemical class는 typed classification을 우선하며 unspecified residue만 기존 name fallback을 사용한다.
현재 native structure writer는 이 internal classification을 직접 표현하지 못하므로 `residue_semantics`
typed loss를 반환한다.

## 현재 normalization/loss matrix

| Format | Import의 core normalization | Export chemical contract |
|---|---|---|
| MOL/SDF V2000 | charge presence, isotope, radical, atom parity, concrete/query bond, bond stereo와 explicit origin | 지원 field round-trip; core/legacy 충돌은 hard error |
| PDB | explicit formal-charge presence와 CONECT explicit origin | formal charge/endpoints 외 isotope, radical, atom stereo, bond order/stereo는 typed loss |
| mmCIF | nullable formal-charge presence와 `struct_conn` order/origin | implemented concrete order와 charge를 쓰고 isotope, radical, atom stereo, query/stereo bond는 typed loss |
| MOL2 | atom/bond explicit origin, aromatic/amide order, unit-tagged partial charge/property presence | explicit SYBYL type/partial charge를 요구; query/unknown/stereo bond는 hard error, formal charge/isotope/radical/atom stereo는 typed loss |
| XYZ/PQR/G96/GRO/PSF | format별 identity/property/connectivity만 normalization | explicit-zero formal charge, isotope, radical, atom stereo, query/stereo bond처럼 표현할 수 없는 core 의미를 typed loss로 반환 |

`object chemistry`는 active topology의 formal/partial charge presence, isotope, radical, atom stereo,
구체/query bond order, bond stereo와 explicit/inferred/user origin count를 schema version 1 JSON/table로
반환한다. Object menu/panel/command palette의 `object.chemistry` action, CLI와 Python은 같은 typed
operation을 호출한다. Partial-charge property가 있으면 별도 presence mask를 우선하며, mask가 없으면
property row 자체를 present로 해석한다.

Organometallic bonding, tautomer/protonation, full valence model, fused aromaticity와 stereochemistry
perception은 conservative v1의 지원 범위가 아니며 지원한 것으로 표시하지 않는다.
