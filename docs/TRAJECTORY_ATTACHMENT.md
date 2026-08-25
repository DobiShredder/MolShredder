# Trajectory attachment contract

`traj load`는 topology와 trajectory를 atom 수만으로 조용히 결합하지 않는다. 사용자는 다음 mapping 정책을
반드시 선택하고, 성공 result의 `atom_mapping`에서 실제 검증 강도와 비교 축을 확인할 수 있다.

- `exact`: trajectory가 제공한 identity 축을 active topology와 ordinal별로 exact 비교한다. H5MD와 LAMMPS는
  모든 frame의 source atom ID를 topology `source_serial`에 대조하므로 현재
  `available-identity-exact`/`source_serial`이다. Atom/residue/chain/element 전체 tuple을 제공하는 source는
  `full-identity-exact`가 된다. Identity가 없는 DCD/XTC/TRR/Amber coordinate format은 exact를 지원한다고
  가장하지 않고 실패한다.
- `index`: atom count와 decoded ordinal order를 사용한다. File과 topology의 atom order가 같음을 사용자가
  확인한 경우에만 명시적으로 선택한다. Result는 `ordinal-only-explicit-opt-in`이다.
- `explicit`: `--atom-map`의 i번째 stable atom ID가 trajectory source ordinal i의 target이다. 현재 topology의
  모든 atom을 중복 없이 한 번씩 포함해야 하며 `--expected-topology-version`이 일치해야 한다. Missing,
  duplicate, zero, unknown ID와 stale version은 Workspace를 바꾸지 않는다.

```bash
molshredder traj load --path run.h5md --mapping exact
molshredder traj load --path run.dcd --mapping index
molshredder traj load --path reordered.dcd --mapping explicit \
  --atom-map 3,2,1 --expected-topology-version 1
```

Mapping은 position뿐 아니라 velocity, presence mask와 frame-dependent atom property/force column 전체에 같은
target order를 적용한다. Canonical session은 policy, stable IDs와 expected topology version을 보존한다.

## Channel과 unit validation

Attached source는 `trajectory-semantic-v1` decorator를 거친다. Frame zero는 attach 전에 검증하고 이후 frame은
읽을 때 같은 contract를 적용한다.

- Position과 triclinic cell은 canonical Å로 변환한다.
- Physical time과 velocity time basis는 canonical ps로 변환한다. Velocity가 있는데 time unit이 없으면 실패한다.
- Force x/y/z는 complete vector여야 한다. `kJ mol^-1 nm^-1`, `kJ mol^-1 angstrom^-1` 및 Amber
  `kilocalorie/mole/angstrom`을 `kJ mol^-1 angstrom^-1`로 정규화한다. Unknown/mixed/non-finite unit은 실패한다.
- Step, physical time, cell, velocity와 force의 availability/unit이 frame 사이에서 바뀌면 해당 frame read가
  typed failure가 된다. 인접 source step과 physical time은 strictly increasing이어야 한다.
- Missing atom은 reader의 explicit presence mask를 보존하며 임의로 present라고 표시하지 않는다.

Result의 `semantics` object는 source/canonical unit, channel availability, 적용된 conversion,
`missing_data_policy`와 validation scope를 machine-readable하게 반환한다. 현재 validation은 requested frame과
직전 frame metadata를 확인하므로 arbitrary seek에서 predecessor를 한 번 더 decode할 수 있다. Background
coalescing과 rapid-seek 최적화는 후속 runtime 단계 범위다.

## GUI

Desktop 상단의 `Map exact`/`Map index`/`Map IDs` control이 같은 canonical parameter를 선택한다. 기본은
`exact`다. `Map IDs`에서는 trajectory source order의 comma-separated stable atom ID를 입력하며 GUI가 dispatch
직전 current topology version을 함께 보낸다. Identity가 없는 format을 열 때는 exact 오류를 확인한 뒤 source
order를 검증하고 index 또는 explicit mapping을 선택한다.
