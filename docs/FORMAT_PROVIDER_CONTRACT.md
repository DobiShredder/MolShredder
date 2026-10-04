# Format provider contract

상태: schema v3 native foundation + ABI 18 loader/structure channel local implementation

MolShredder의 file I/O registry는 format 지원을 확장자 boolean으로 표현하지 않는다. Schema v3의 각 row는
canonical format ID와 extension에 더해 provider 및 read/write 방향별 capability를 함께 반환한다.

## Provider identity와 provenance

Provider에는 다음 field가 항상 존재한다.

- `id`, `version`: 실행에 사용한 구현의 stable identity와 version
- `origin`: `native_builtin`, `dynamic_plugin`, `external_converter`
- `trust`: `trusted_builtin`, `trusted_configured`, `untrusted`
- `license_status`: `approved`, `pending`, `rejected`
- `license_expression`: provider code에 적용되는 license 식별자
- `available`, `unavailable_reason`: 현재 process에서 실제 선택 가능한지와 그 이유

현재 등록된 24개 format은 모두 MolShredder `native` provider로 migration됐다. Native provider version은
application version과 같고 자체 코드는 `GPL-3.0-or-later`, origin/trust/license status는 각각
`native_builtin`/`trusted_builtin`/`approved`다. HDF5, netCDF-C 같은 linked dependency의 별도 license와
installer closure는 format limitation 및 third-party notice에서 계속 관리한다.

`format list`와 structure/trajectory/volume load·save result는 같은 provider object를 반환한다. 따라서 GUI,
CLI와 Python은 화면 label이나 parser-local 상태가 아니라 실제 shared action result에서 provenance를 얻는다.

## 방향별 capability와 typed loss

각 format row의 `directions.read`와 `directions.write`에는 다음 값이 있다.

- `available` 및 unavailable일 때 non-empty `unavailable_reason`
- 해당 방향의 `channels`와 `limitations`
- export가 loss table을 반환하는지 나타내는 `typed_loss_reporting`

Schema v2의 top-level `read`, `write`, `channels`, `limitations` field는 기존 client의 단계적 migration을 위해
schema v3에도 유지한다. `migrate_format_capability_v2`는 이 필드를 방향별 row로 승격하고 알 수 없는 vendor
field를 `extension_fields`에 보존한다. Unknown field를 버리거나 의미를 추측하지 않는다.

## Provider 선택과 collision 정책

모든 I/O command는 `--provider auto|<provider-id>`를 사용한다. 정적 registry의 기본 explicit ID는 `native`이고,
동적 structure read는 `molfile:<plugin>` 중 아래 "Structure channel profile"의 12개 이름을 지원한다.

- `auto`는 available, license-approved provider 중 `trusted_builtin/native_builtin`을 먼저 선택한다.
- Explicit provider가 없거나 요청 방향을 지원하지 않으면 실패하며 다른 provider로 fallback하지 않는다.
- Extension은 ASCII case-insensitive하게 정규화한다.
- 같은 extension이 서로 다른 canonical format과 충돌하면 provider 우선순위로 의미를 추정하지 않고 실패한다.
  이 경우 `--file-format`을 명시해야 한다.
- 같은 format의 여러 provider가 생기면 trust/origin 우선순위와 stable provider ID 순서로 결정한다. Explicit
  override는 이 순서를 건너뛰되 availability와 license gate를 우회하지 않는다.
- Sequential-only plugin은 native indexed trajectory provider보다 높은 automatic priority를 받을 수 없다.

`molfile:pqr`는 자동 선택되지 않는다. 첫 호출은 다음처럼 provider ID와 shared library를 둘 다 명시한다.

```text
load model.pqr --file-format pqr --provider molfile:pqr --plugin-path /absolute/path/to/plugin
```

CLI와 Python은 같은 `load` action을 호출하며, 등록은 해당 action registry session 동안 유지된다. 등록 후에는
같은 session에서 `--plugin-path`를 생략할 수 있다. `--file-format`은 `auto`, plugin 이름 또는 canonical format
(`pdbx`→`mmcif`, `parm7`→`prmtop`, `vsf`→`vtf`)만 허용하며 다른 값은 Workspace 변경 없이 거부한다. Writer와 GUI provider picker는 아직 연결하지 않았다.
Explicit binary는 실행 코드이므로 `trust=untrusted`, file별 재배포 license가 검토되지 않은 상태는 `license_status=pending`, `license_expression=NOASSERTION`으로
보고한다. 이는 사용자의 명시적 local 실행 동의를 나타낼 뿐 installer 포함이나 license 승인을 뜻하지 않는다.

## Structure channel profile (MOL-03 S1)

Plugin 이름마다 하나의 channel profile이 있고 profile이 없는 이름(`dcd`, `trr` 등)은 입력을 열기 전에
`unsupported`로 실패한다. `vcf`는 좌표만 있는 channel이라 trajectory adapter(S2)에서 다룬다.

| plugin | canonical format | 필수 atom option | 좌표 | 비고 |
|---|---|---|---|---|
| `pdb` | pdb | 없음 | timestep 1개 | |
| `mmcif`, `pdbx` | mmcif | 없음 | timestep 1개 | |
| `pqr` | pqr | charge, radius(>0) | timestep 1개 | radius는 `pqr.radius` |
| `mol2` | mol2 | 없음 | timestep 1개 | |
| `psf`, `parm7`, `vsf` | psf, prmtop, vtf | 없음 | 없음(topology-only, zero frame) | `read_next_timestep`을 호출하지 않는다 |
| `gro`, `g96`, `vtf` | gro, g96, vtf | 없음 | timestep 1개 | |
| `xyz` | xyz | 없음 | timestep 1개 | 빈 residue 이름은 synthetic `MOL` residue |

공통 규칙은 다음과 같다.

- Plugin이 option flag로 보고한 field만 property가 된다. charge→`partial_charge`(elementary charge), mass→`mass`(dalton),
  radius→`vdw_radius`(PQR은 `pqr.radius`, Å), occupancy→frame `occupancy`와 `occupancy_present`, B-factor→frame
  `b_iso_or_equiv`와 `b_iso_or_equiv_present`. 보고하지 않은 field를 0으로 채우지 않는다.
- Atomic number flag가 없으면 이름에서 보수적으로 추론하고 `atomic_number_inferred_from_name` loss를 기록한다. 추론할 수
  없으면 실패한다.
- `read_bonds`는 1-based endpoint로 해석하고(`0`, 범위 밖, self-bond는 실패) 대칭 중복은 한 번만 남기며
  `duplicate_bonds_dropped:<n>`을 기록한다. Bond order 1/1.5/2/3은 single/aromatic/double/triple이고 order 배열이 없거나
  다른 값이면 `unknown`이며 `bond_order_not_provided` 또는 `bond_order_unmapped:<n>`을 기록한다. Bond type 이름은
  보존하지 않고 `bond_type_names` loss를 기록한다. Callback이 없으면 `connectivity_not_provided`다. Bond 수는
  `MolfileReadLimits::max_bonds`(기본 50,000,000)와 byte budget을 모두 만족해야 한다.
- Typed loss는 topology source metadata `molfile.loss`에 쉼표 목록으로 남는다. 항상 `source_atom_serial`(ABI atom 구조에
  없음), `velocity`, `force`(이 channel은 요청하지 않음)를 포함하고 좌표 channel은 `physical_time`을 포함한다.
- Bond endpoint가 1-based라는 해석은 공개 ABI 문서 발췌에서 확인되지 않았고 합성 plugin으로만 검증했다. 실제 plugin
  binary는 번들·다운로드하지 않으므로(Q-016) 실물 대조는 사용자가 신뢰하는 binary를 명시적으로 지정할 때만 가능하다.

## Trajectory channel (MOL-03 S2)

`traj load --provider molfile:<plugin>`은 `dcd`, `trr`, `xtc`, `crd`(mdcrd), `crdbox`, `lammpstrj`, `netcdf`, `binpos`, `rst7`,
`vcf` plugin만 trajectory로 받는다. 다른 이름은 입력을 열기 전에 `unsupported`다. Structure callback이 없는 trajectory 전용
plugin(`read_structure` 없음)도 등록된다. 등록은 open/close와 structure 또는 timestep callback 중 하나를 요구하고, 어느 channel을
쓰는지는 사용 시점에 확인한다.

- 접근: `MolfileTrajectorySource`는 `FrameAccess::sequential`이다. Attach 때 한 번 끝까지 읽어 frame을 세며(`max_frames`
  기본 10,000,000, 취소와 progress 확인) 그동안 frame buffer 하나만 쓴다. 이후 read는 열린 handle을 앞으로만 진행하고, 뒤로 가는
  접근은 handle을 닫고 다시 열어 건너뛴다. Thread-unsafe plugin은 callback마다 provider mutex를 잡고 handle은 source mutex가
  직렬화한다. ABI의 EOF와 error는 둘 다 -1이라 scan에서는 첫 실패를 EOF로 보며, frame 0에서 실패하면 "no readable frames"다.
- Atom 수: host가 topology의 atom 수를 `open_file_read`에 미리 넣어 주며(file이 수를 기록하지 않는 `crd`, `binpos`용), 열린 뒤 plugin이
  보고한 수가 다르면 attach 전에 거부한다. 수가 0이거나 `max_atoms`·frame byte budget을 넘으면 입력을 열기 전에 거부한다.
- 가져오는 값: 좌표(Å, float32). Unit cell은 plugin이 여섯 값을 모두 쓰고(host가 NaN으로 미리 채운다) 전부 0이 아닐 때만 가져온다.
  좌표가 non-finite이면 그 frame만 실패한다.
- 가져오지 않는 값(loss): `velocity`, `force`, `step_number`, `physical_time_not_imported_unit_unverified`. 근거는 MOL-00 audit이다.
  예: rst7 velocity는 20.455 정규화 전 raw 값이고 parsed time은 timestep에 넣지 않으며, trr writer는 time을 step의 0.1배로 만든다. Plugin이
  쓴 time 원값은 frame field `molfile.physical_time_raw`에 단위 미검증으로만 보존하고 `PhysicalTime`을 만들지 않는다.
- Library lifetime: source는 registry 내부 상태를 shared로 잡고 있어 registry가 먼저 사라져도 마지막 source가 해제될 때까지 plugin이
  unload되지 않는다(그 뒤 fini→unload).
- Native 우선: `auto`는 native indexed reader를 고르며 sequential plugin의 priority는 올라가지 않는다. `ReadTimestepMetadata`
  같은 ABI 후반 callback은 struct 배치를 공개 문서로 완전히 확인하지 못해 사용하지 않는다(velocity 지원은 별도 근거 필요).

## Volume channel (MOL-03 S3)

`volume load --provider molfile:dx|molfile:ccp4`는 scalar volume 하나를 읽는다(`ccp4`는 canonical `mrc`, `dx`는 `opendx`). 다른 이름은
입력을 열기 전에 `unsupported`다.

- 격자: ABI의 origin(Å)과 세 축 extent(첫 sample에서 마지막 sample까지의 벡터)로 delta = extent / (count − 1)을 만든다. 축은 skewed여도
  그대로 보존한다. Sample이 하나뿐인 축은 ABI에 간격이 없어 `unsupported`이며 native reader를 쓰라고 안내한다.
- 순서: plugin block은 x가 가장 빠른 순서이고 MolShredder grid는 z가 가장 빠르므로 전치한다. Scalar는 float32이고 non-finite 값은 실패한다.
  Scalar unit은 만들지 않는다. Coordinate unit은 ABI에 없으므로 native OpenDX처럼 호출자가 선언한 값(`--coordinate-unit`, 기본 Å)을 기록한다.
- 다중 dataset: ABI dataset 배열의 stride(`molfile_volumetric_t` 전체 크기)를 공개 문서로 확인하지 못해 dataset이 1개일 때만 읽고 그 외는
  `unsupported`다. 구조체의 `dataname`~`zsize` 앞부분만 선언하고 `has_scalar`/`has_gradient`/`has_variance`/`has_color`는 읽지 않으며 color block은 요청하지 않는다.
- 한도: `MolfileVolumeLimits`(기본 268,435,456 value, staging 1 GiB = plugin block + 전치본)를 plugin data callback 전에 확인한다.
- ABI 배치 검증 수준: `read_volumetric_metadata`/`read_volumetric_data`의 위치는 공개 header 문서(`molfile_plugin_t` 멤버 순서)와 대조했지만 실제 plugin
  binary로는 확인하지 못했다(Q-016, 번들·다운로드 금지). 합성 test plugin은 같은 struct 정의를 쓰므로 배치 오류를 잡지 못한다.

## Writer disposition (MOL-04)

molfile plugin writer는 adapter로 만들지 않는다(결정 2026-10-04). P0에서 plugin이 쓰기를 등록한 15개 format 중 14개는 native writer가 같은
channel을 대체하고, `lammpstrj`만 native writer가 없어 deferred다. `save`, `traj save`, `volume save`에 `--provider molfile:<plugin>`을 주면
fallback이나 출력 파일 없이 `unsupported`로 실패한다(`io.molfile_channels`가 세 action을 확인한다).

| plugin | native writer | 상태 | native에 남은 한계 |
|---|---|---|---|
| pqr, pdb, gro, psf | pqr, pdb, gro, psf | native 대체 | 없음 |
| dx | opendx | native 대체 | 없음 |
| rst7 | rst7 | native 대체 | 없음 |
| pdbx | mmcif | native 대체 + 한계 | BinaryCIF writer와 압축 입력 없음 |
| mol2 | mol2 | native 대체 + 한계 | 선택 SUBSTRUCTURE field와 multi-object 일괄 export 없음 |
| xyz | xyz | native 대체(한계 없음) | 없음 |
| trr | trr | native 대체 + 한계 | multi-frame append, virial·pressure·energy block 없음 |
| dcd | dcd | native 대체 + 한계 | multi-frame write·append, fixed-atom 압축 write 없음 |
| crd, crdbox | mdcrd, crdbox | native 대체 + 한계 | multi-frame write·append 없음 |
| binpos | binpos | native 대체 + 한계 | multi-frame write·append 없음 |
| lammpstrj | 없음 | deferred | atom-style writer와 multi-frame append. plugin writer는 timestep 번호와 numeric type을 만들어 내고 경계를 좌표에서 추정하므로 채택하지 않는다 |

Plugin writer가 만들어 내는 값(trr time = step의 0.1배, pdb/pqr의 P 1·Z=1 고정, dcd header의 step 0·간격 1 고정, gro box 9값 재구성 등)은 채택하지 않는다.
LAMMPS dump writer가 필요해지면 native writer를 별도 항목으로 추가한다.

## ABI 18 dynamic loader 경계

MOL-02의 loader는 VMD main source와 독립적이며 official public ABI 18 header의 structure/timestep read prefix만
선언한다. 현재 vertical slice는 다음을 제공한다.

- `vmdplugin_init`, `vmdplugin_register`, `vmdplugin_fini`의 세 OS dynamic loading
- explicit file, application-bundled approved directory와 user-approved directory의 non-recursive discovery
- ABI 18, `mol file reader` type, 필수 callback과 중복 provider identity의 transactional validation
- plugin-owned descriptor/callback table을 보호하는 library lifetime과 reverse-order fini/unload
- `VMDPLUGIN_THREADUNSAFE` open→structure→timestep→close sequence의 provider별 serialization
- atom/residue identity, optional atom field, bond, coordinate와 optional unit cell의 typed staging(channel profile 기준)
- callback 오류 때 close를 보장하고 Workspace를 바꾸지 않는 document-before-commit 경계

PQR staging은 `MolfileReadLimits`의 두 예산을 모두 만족해야 한다. 기본값은 atom 5,000,000개와 보수적으로
추정한 peak staging 1 GiB다. Byte estimate에는 ABI atom/coordinate buffer뿐 아니라 최악의 atom별 topology,
residue, property와 string copy 비용을 포함한다. Count×estimate overflow, atom budget 또는 byte budget 초과는
구조 callback과 allocation 전에 실패하고 이미 열린 plugin handle은 close한다. 신뢰한 대형 입력에서는 API로
예산을 명시적으로 높일 수 있지만 native indexed provider 우선순위는 바뀌지 않는다.

`TaskContext` overload는 `molfile-wait-provider`, open, structure, timestep, atom conversion, validation과 completion
stage의 단조 증가 progress를 보고한다. Thread-unsafe provider lock은 timed wait로 취소를 10 ms마다 관찰한다.
Plugin callback 자체는 ABI 18에 cancellation hook이 없으므로 실행 중인 callback을 강제 중단하지 않으며, callback이
반환된 직후 취소를 관찰해 handle을 close하고 staged result를 폐기한다. Atom conversion은 4,096개마다 취소와
progress를 갱신한다. 취소 결과는 `cancelled` error이며 Workspace commit은 발생하지 않는다.

Registry의 library/descriptor collection은 shared/exclusive synchronization을 사용한다. 따라서 immutable descriptor
snapshot 조회와 이미 등록된 provider read는 새 library discovery와 data race를 만들지 않는다. Library record는
등록 뒤 제거되지 않고 registry lifetime 동안 유지된다. Shared `load` action은 별도의 session-scoped timed lane에서
최초 discovery→read→Workspace commit을 직렬화한다. 동시에 들어온 두 첫 호출은 library를 한 번만 등록하고 두
Workspace commit을 순서대로 수행한다. Lane 대기는 10 ms마다 `TaskContext` cancellation을 관찰하며 취소된 호출은
plugin input을 열거나 Workspace를 변경하지 않는다.

Working directory, input file 인접 directory, `PATH` 및 임의 shared-library search path는 자동 scan하지 않는다.
In-process native plugin은 process 권한을 공유하므로 explicit path도 sandbox가 아니며, 사용자가 신뢰하지 않는 binary를
load해서는 안 된다.

이 loader의 structure read path는 shared `load` action에 연결되어 CLI와 Python에서 동일한 result envelope를 반환한다.
동적 provider는 아직 runtime `format list`에 영구 row로 합쳐지지 않으며 GUI provider picker도 없다. MOL-03의
P0 read conformance와 MOL-04의 writer/details UI가 끝나기 전에는 일반 molfile dispatch가 완료됐다고 해석해서는
안 된다.
