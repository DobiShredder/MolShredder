# Trajectory time-series analysis

MolShredder는 외부 script 없이 trajectory centroid/COM, atom distance, RMSD/RMSF, contact와 H-bond를 계산한다. GUI action,
interactive console, Python `invoke()`와 session replay는 같은 Workspace operation과 C++ kernel을
호출하며 결과는 하나의 typed table에서 text, JSON과 CSV로 표현된다.

## Commands

```text
analyze trajectory center
  [--selection all] [--mode centroid|com]
  [--first 0] [--last LAST] [--stride 1]
  [--missing error|skip] [--precision 0..15]
  [--unit angstrom|nanometer]

analyze trajectory distance
  --from EXPR --to EXPR [--pbc raw|minimum-image]
  [--first 0] [--last LAST] [--stride 1]
  [--precision 0..15] [--unit angstrom|nanometer]

analyze trajectory rmsd|rmsf
  [--selection all] [--fit-selection EXPR]
  [--reference 0] [--first 0] [--last LAST] [--stride 1]
  [--fit rigid|none] [--weight uniform|mass]
  [--missing error|skip] [--precision 0..15]
  [--unit angstrom|nanometer]

analyze trajectory contacts
  [--selection1 all] [--selection2 EXPR] [--cutoff 4.0]
  [--pbc raw|minimum-image] [--exclude-bonded true|false]
  [--report frames|occupancy]
  [--first 0] [--last LAST] [--stride 1]
  [--precision 0..15] [--unit angstrom|nanometer]

analyze trajectory hbonds
  [--donors all] [--acceptors EXPR] [--cutoff 3.5] [--angle 30]
  [--pbc raw|minimum-image] [--report frames|occupancy]
  [--first 0] [--last LAST] [--stride 1]
  [--precision 0..15] [--unit angstrom|nanometer]
```

Frame index는 zero-based이고 first/last는 inclusive다. Last를 생략하면 attached trajectory의 마지막
frame을 사용한다. Stride는 positive integer다. 분석은 cache를 통해 requested frame lease를 읽지만
playback timeline, current viewport frame, representation과 persistent measurement list를 변경하지 않는다.

Center selection은 topology snapshot에서 한 번 평가한다. COM은 explicit mass property를 우선하고
없을 때 versioned estimated element-mass table을 사용한다. Missing=`error`는 어느 frame에서든 selected
atom이 없으면 전체 operation을 실패시키며, `skip`은 해당 frame의 missing atom을 제외하고 selected,
used, skipped count를 각 row에 기록한다.

Distance endpoint는 각각 정확히 한 atom을 선택해야 한다. Minimum-image는 모든 requested frame에
유효한 periodic cell이 있어야 한다. Frame read나 calculation 하나라도 실패하면 frame index를 붙인
stable error를 반환하며 partial table을 성공으로 반환하지 않는다.

RMSD/RMSF는 reference frame과 optional rigid fit을 사용한다. Fit selection과 score/output selection을
분리할 수 있으며 uniform/mass weighting, missing pair policy, pre-fit RMSD와 atom별 RMSF observation
count를 보존한다. 수치 정의와 degeneracy/PBC 한계는
[Rigid alignment, RMSD and RMSF](ALIGNMENT_AND_FLUCTUATION.md)에 둔다.

Contact/H-bond coordinator는 각 frame에서 spatial neighbor kernel을 실행한다. `report=frames`는 frame
metadata와 interaction count를, `report=occupancy`는 전체 analyzed frame 수를 분모로 한 pair 또는
donor/acceptor/hydrogen triple의 observation count와 occupancy를 반환한다. Mean distance와 H-bond
mean angle deviation은 interaction이 관찰된 frame만 online 평균한다. Cutoff는 requested unit으로
고정하고 frame coordinate unit에 맞춰 매 frame 변환한다. 자세한 geometry와 typing은
[Contact analysis](CONTACT_ANALYSIS.md)에 둔다.
`report=frames`는 unique-pair aggregate를 보관하지 않아 high-contact-density trajectory에서 occupancy
map memory를 소비하지 않는다.

## Table and metadata

모든 row는 frame index, optional source step/physical time와 unit을 포함한다. Center row는 x/y/z,
length unit, atom counts와 optional mass provenance를 포함한다. Distance row는 1-based endpoint index,
displacement, norm, unit과 PBC mode를 포함한다. JSON은 `data.table.columns/rows`, Python은 같은 shape의
native list, CSV는 같은 column order를 사용한다. Command-level fields에는 selection/endpoints,
inclusive range, stride, precision, requested unit과 PBC/missing policy를 보존한다.

Task cancellation은 각 frame/cell read 전에 확인하며 progress callback은 성공한 row/cell마다 0..1 fraction을
보고한다. RMSD/RMSF와 RMSD matrix는 persistent result와 versioned line/heatmap plot projection을 만들며 JSON/CSV
export를 공유한다. CSV에서 제외되는 provenance/plot metadata는 loss report에 기록한다. Desktop의 RMSD matrix는
bounded background worker에서 실행되고 generation과 object/system/topology/coordinate-source/cache identity를 owner-thread
commit 직전에 검사한다. Progress/cancel control을 제공하고 stale/cancel/error는 partial result를 publish하지 않는다.
Frame loop는 아직 single-threaded이며 한 frame 내부 spatial kernel만 pairwise quadratic scan을 피한다.
