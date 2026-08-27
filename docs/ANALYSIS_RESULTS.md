# Persistent analysis results

MolShredder의 자주 쓰는 분석은 transient console text로 끝나지 않고 Workspace가 소유하는
`PersistentAnalysisResult`를 만든다. 현재 자동 저장 대상은 다음과 같다.

- centroid와 center of mass
- atom-to-atom distance
- atom angle과 signed dihedral
- solvent-accessible surface area(SASA)
- radial pair histogram과 radial distribution function(RDF)
- fitted trajectory RMSD matrix
- static contacts
- trajectory RMSD와 trajectory contacts

각 결과는 schema v2의 monotonic 64-bit result ID, unique name, original typed response/table, export table과
versioned `ScientificResultContract` provenance를 보존한다.

- source object ID/name snapshot, topology version, coordinate-source/displayed-coordinate revision과
  `current_frame`/`trajectory_range` scope
- normalized canonical command/arguments
- algorithm과 MolShredder algorithm version
- input coordinate/output unit, float calculation precision과 presentation precision
- PBC policy/cell requirement와 missing-data policy
- absolute/relative numerical tolerance와 tolerance unit
- time-series first/last/stride
- millisecond precision UTC creation time

Schema v1 typed snapshot은 schema v2로 failure-atomic migration한다. v1에 없던 coordinate
revision과 numerical tolerance는 임의로 추론하지 않고
`coordinate_revision_known=false`, `tolerance_known=false`로 보존한다. 이런
legacy result는 원본 data를 조회할 수 있지만 현재 coordinate와 동일하다고
가정할 수 없으므로 `coordinate_changed`로 표시한다. Malformed legacy snapshot은
기존 result store를 변경하지 않는다.

Source object가 삭제되거나 topology/coordinate/method version이 바뀌어도 결과는 삭제하지 않는다.
`result get/list`는 `object_deleted`, `topology_changed`, `coordinate_changed` 또는 `method_changed`를 반환하며
계산 당시 data와 overlay anchor는 immutable snapshot으로 유지한다. `current_frame` 결과는 frame commit 뒤 stale이고,
`trajectory_range` 결과는 viewport seek만으로 stale이 되지 않으며 coordinate source 교체 시에만 stale이다.
새 결과를 같은 이름으로 만들거나 invalid name을 사용하면 분석 실행 전에 거부한다. PBC/cell, missing policy,
precision, tolerance 또는 frame-range contract가 모순인 draft와 long analysis cancellation/error는 partial result를
publish하지 않는다.

## Canonical operations

```text
analyze center --selection all --mode com --result-name protein-com
measure distance --from "index 1" --to "index 2" --result-name active-site-gap
measure angle --first "index 1" --vertex "index 2" --third "index 3" --pbc minimum-image
measure dihedral --first "index 1" --second "index 2" --third "index 3" --fourth "index 4"
analyze sasa --selection all --probe-radius 1.4 --samples 960 --evaluation-budget 100000000
analyze rdf --first protein --second solvent --maximum-radius 10 --bin-width 0.1 --normalization g-r --pbc minimum-image
analyze trajectory rmsd-matrix --selection "name CA" --first 0 --last 100 --stride 5 --fit rigid --frame-pair-budget 1000000
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

Persistent RDF, trajectory RMSD/RMSF와 RMSD matrix table은 `plot.schema_version=1` projection도 가진다.
RDF/RMSD/RMSF는 `[x,value]` line samples, matrix는 `[first_frame,second_frame,rmsd]` heatmap cell이다.
즉시 CLI/Python/GUI response, `result get`과 JSON export가 같은 projection을 보존하고 Desktop Canvas는 이 data만
표현한다. CSV는 table column/value는 보존하지만 scientific provenance와 plot metadata를 담지 못하므로 export
response의 `loss_item_count`/`losses`에 손실을 명시한다. JSON export의 loss count는 0이다.

## Overlay와 frontend

Center 결과는 point marker와 screen-facing text label을, distance 결과는 stable Atom ID endpoint
snapshot, endpoint marker, dashed line과 midpoint label을 가진다. Angle/dihedral 결과는 계산 당시의
stable Atom ID와 minimum-image unwrapped position snapshot, 순서가 보이는 line chain, endpoint marker와
degree label을 가진다. Overlay visibility는 molecular
representation/object visibility와 독립적이다. Deleted/stale source도 snapshot overlay를 유지하므로
사용자는 source status를 함께 확인해야 한다.

Desktop의 **Analyze** panel은 compute/store, result list/detail, show/hide, JSON/CSV export와 delete를
같은 canonical operation으로 호출한다. Python은 `molshredder.invoke()`로 동일 command를 사용한다.
SASA, RDF와 trajectory RMSD matrix는 bounded background task로 실행되며 진행률/취소를 제공한다. Worker는
immutable input snapshot만 계산하고 owner-thread commit이 generation과 source revision을 확인한 뒤 동일 canonical
formatter로 저장하므로 stale/cancel/error에서 partial result가 생기지 않는다.
Creation time은 실제 실행 시각이므로 cross-process frontend parity에서는 canonical UTC 형식을 먼저
검증한 뒤 timestamp 값만 정규화하고 나머지 envelope/provenance를 exact 비교한다.

현재 distance reduction은 endpoint마다 정확히 한 atom인 `mode=atom`만 실행된다. Selection
centroid/COM, minimum/maximum/mean/closest distance mode와 full PyMOL label font/drag/pick semantics는
구현된 것으로 표시하지 않는다.

Angle/dihedral도 현재 active object의 current frame과 single-atom selection만 지원한다. PyMOL의
multi-state query, measurement arc/dash geometry, measurement 전용 color/size/label-position 설정과
drag/pick semantics는 별도 capability이며 이 slice가 구현한 것으로 표시하지 않는다.

SASA는 `molshredder-shrake-rupley-fibonacci-v1` deterministic sampling을 사용한다. 선택 원자의
expanded radius는 atom VDW radius + probe radius이고, 선택 밖 present atom도 occluder로 포함한다.
결과에는 total/per-atom area, accessible sample count, radius source, probe/sample/budget, 실제 evaluation 수,
missing occluder 수와 single-sample area quantum을 기록한다. Isolated sphere는 analytic `4πR²`와 exact하게
일치하고 overlap sphere는 sample quantum/`1/N` tolerance로 검증한다. 현재 active frame의 raw coordinate만
지원하며 periodic image occlusion, trajectory aggregation과 exact PyMOL `get_area` dot/state/setting semantics는
구현 또는 compatibility로 주장하지 않는다. Kernel은 cancellation과 work budget을 받지만 현재 Desktop의 static
SASA 호출은 동기식이므로 대규모 선택의 background progress/cancel UI는 후속 공통 analysis task slice에서 닫는다.

RDF는 `molshredder-rdf-histogram-v1`을 사용한다. Export table은 bin index/lower/upper/center, exact pair count,
normalized value, distance unit과 normalization을 가진다. Same-selection은 unordered pair, cross-selection은 ordered
pair이며 `g(r)`은 unit-cell volume과 minimum-image가 필요하다. Pair budget, skipped missing count,
eligible/evaluated pair와 bin/range/PBC/normalization을 provenance에 남기고 analytic count·shell-volume fixture로
검증한다. 현재 active-frame 정적 계산이며 bonded exclusion, trajectory 평균/uncertainty와 finite-size correction은
제공하지 않는다. Desktop background lifecycle은 공통 long-analysis task slice에 남아 있다.

RMSD matrix는 `molshredder-rmsd-matrix-v1`이며 diagonal 포함 upper triangle을 explicit frame-pair table로
저장한다. 기존 Horn quaternion fit/RMSD kernel의 unit conversion, fit/score selection, uniform/mass weight와
missing policy를 재사용한다. Frame-pair budget을 실행 전에 검증하고 각 cell마다 cancellation/progress를 확인하며
오류·취소 시 partial matrix를 commit하지 않는다. Matrix 계산은 coordinate source와 viewport를 mutate하지 않는다.
현재 Desktop 호출은 동기식이므로 대규모 matrix의 background progress/cancel/stale-completion UI는 다음 공통
analysis task lifecycle에서 연결한다.
