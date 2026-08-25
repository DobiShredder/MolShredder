# Persistent analysis results

MolShredder의 자주 쓰는 분석은 transient console text로 끝나지 않고 Workspace가 소유하는
`PersistentAnalysisResult`를 만든다. 현재 자동 저장 대상은 다음과 같다.

- centroid와 center of mass
- atom-to-atom distance
- static contacts
- trajectory RMSD와 trajectory contacts

각 결과는 monotonic 64-bit result ID, unique name, original typed response/table, export table과
다음 provenance를 보존한다.

- source object ID/name snapshot과 topology version
- normalized canonical command/arguments
- algorithm과 MolShredder algorithm version
- coordinate unit, PBC policy와 missing-data policy
- time-series first/last/stride
- millisecond precision UTC creation time

Source object가 삭제되거나 topology version이 바뀌어도 결과는 삭제하지 않는다. `result get/list`는
각각 `object_deleted` 또는 `topology_changed`를 반환하며 계산 당시 data와 overlay anchor는 immutable
snapshot으로 유지한다. 새 결과를 같은 이름으로 만들거나 invalid name을 사용하면 분석 실행 전에
거부한다. Long analysis의 cancellation/error는 partial result를 publish하지 않는다.

## Canonical operations

```text
analyze center --selection all --mode com --result-name protein-com
measure distance --from "index 1" --to "index 2" --result-name active-site-gap
analyze contacts --first protein --second ligand --cutoff 4.0
analyze trajectory rmsd --selection "name CA" --reference 0

result list
result get --id 2
result hide --id 2
result show --id 2
result export --id 2 --path result.json --output-format json
result export --id 2 --path result.csv --output-format csv
result delete --id 2
```

CLI의 global `--format text|json|csv`와 충돌하지 않도록 export 파일 형식 argument는
`--output-format`이다. Export는 temporary file을 flush한 뒤 target을 atomic publish한다.
기존 파일은 기본적으로 덮어쓰지 않으며 `--overwrite true`로만 교체한다. JSON은 전체 provenance와
original data를, CSV는 분석별 explicit table을 저장한다.

## Overlay와 frontend

Center 결과는 point marker와 screen-facing text label을, distance 결과는 stable Atom ID endpoint
snapshot, endpoint marker, dashed line과 midpoint label을 가진다. Overlay visibility는 molecular
representation/object visibility와 독립적이다. Deleted/stale source도 snapshot overlay를 유지하므로
사용자는 source status를 함께 확인해야 한다.

Desktop의 **Analyze** panel은 compute/store, result list/detail, show/hide, JSON/CSV export와 delete를
같은 canonical operation으로 호출한다. Python은 `molshredder.invoke()`로 동일 command를 사용한다.
Creation time은 실제 실행 시각이므로 cross-process frontend parity에서는 canonical UTC 형식을 먼저
검증한 뒤 timestamp 값만 정규화하고 나머지 envelope/provenance를 exact 비교한다.

현재 distance reduction은 endpoint마다 정확히 한 atom인 `mode=atom`만 실행된다. Selection
centroid/COM, minimum/maximum/mean/closest distance mode와 full PyMOL label font/drag/pick semantics는
구현된 것으로 표시하지 않는다.
