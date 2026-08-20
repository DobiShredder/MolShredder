# Format provider contract

상태: schema v3 native foundation + ABI 18 loader local implementation

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
현재 동적 structure read vertical slice는 `molfile:pqr`를 지원한다.

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
같은 session에서 `--plugin-path`를 생략할 수 있다. 현재 PQR 외의 molfile structure format, trajectory/volume,
writer와 GUI provider picker는 아직 연결하지 않았다. Explicit binary는 실행 코드이므로 `trust=untrusted`,
file별 재배포 license가 검토되지 않은 상태는 `license_status=pending`, `license_expression=NOASSERTION`으로
보고한다. 이는 사용자의 명시적 local 실행 동의를 나타낼 뿐 installer 포함이나 license 승인을 뜻하지 않는다.

## ABI 18 dynamic loader 경계

MOL-02의 loader는 VMD main source와 독립적이며 official public ABI 18 header의 structure/timestep read prefix만
선언한다. 현재 vertical slice는 다음을 제공한다.

- `vmdplugin_init`, `vmdplugin_register`, `vmdplugin_fini`의 세 OS dynamic loading
- explicit file, application-bundled approved directory와 user-approved directory의 non-recursive discovery
- ABI 18, `mol file reader` type, 필수 callback과 중복 provider identity의 transactional validation
- plugin-owned descriptor/callback table을 보호하는 library lifetime과 reverse-order fini/unload
- `VMDPLUGIN_THREADUNSAFE` open→structure→timestep→close sequence의 provider별 serialization
- PQR charge/radius, atom/residue identity, coordinate와 optional unit cell의 typed staging
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

이 loader의 PQR read path는 shared `load` action에 연결되어 CLI와 Python에서 동일한 result envelope를 반환한다.
동적 provider는 아직 runtime `format list`에 영구 row로 합쳐지지 않으며 GUI provider picker도 없다. MOL-03의
P0 read conformance와 MOL-04의 writer/details UI가 끝나기 전에는 일반 molfile dispatch가 완료됐다고 해석해서는
안 된다.
