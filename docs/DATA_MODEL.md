# Data model

## Background candidate boundary

Bounded worker는 mutable `Workspace`를 직접 소유하거나 변경하지 않는다. Background parse/analysis/mesh
task는 memory reservation과 generation을 가진 candidate를 만들고 `ready_to_commit`에서 멈춘다. Workspace
owner thread가 generation을 다시 확인한 뒤 commit closure를 실행한다. Structure multi-input load는 모든
MolecularSystem, stable object ID, name과 Scene node candidate가 성공한 경우에만 object vector와 Scene을
함께 publish한다. 자세한 lifecycle은 [Bounded task execution](TASK_EXECUTION.md)에 정의한다.

이 문서는 MolShredder foundation data model의 현재 public contract를 설명한다. API의 기준은
`include/molshredder/model/`이며 reader, trajectory cache와 renderer는 이 계약 위에 구현한다.

## 구조와 lifetime

```text
MolecularSystem
├── shared_ptr<const Topology>             immutable, versioned snapshot
│   ├── ResidueRecord[] / AtomRecord[]     stable dense indices in the snapshot
│   ├── bonds / angles / dihedrals / impropers
│   ├── AtomPropertyTable                  typed contiguous columns
│   └── source metadata
└── shared_ptr<const CoordinateSource>
    └── shared_ptr<const CoordinateFrame>  leased immutable frame
        ├── positions / optional velocities
        ├── explicit atom-presence mask
        └── time / unit cell / per-frame properties / source fields
```

`TopologyBuilder::build()`는 immutable snapshot을 만들며 첫 snapshot의 version은 1이다.
`TopologyBuilder::from()`으로 snapshot을 편집하면 원본은 바뀌지 않고 새 snapshot의 version이
증가한다. `AtomIndex`와 `ResidueIndex`는 snapshot 안에서만 dense하다. Atom/bond에는 별도의
non-zero 64-bit stable ID가 있고 insert/delete/reorder는 `TopologyRemap`으로 source/target ordinal을
연결한다. Input file의 atom serial은 MolShredder stable ID가 아니며
`AtomRecord::source_serial`에 provenance로만 보존한다. 상세 계약은
[Persistent identity and numeric contract](IDENTITY_AND_NUMERIC_CONTRACT.md)에 둔다.

`MolecularSystem`은 stable application ID와 이름을 topology/coordinate source에 묶는다. 두
component의 atom count가 다르면 생성할 수 없다. Shared immutable ownership은 worker가 topology
snapshot이나 decoded frame을 사용하는 동안 그 lifetime을 보장한다.

Workspace의 `PersistentAnalysisResult`는 molecular object와 별도 lifetime을 가진 immutable calculation
snapshot이다. Source object/topology snapshot reference, typed response/table, algorithm/unit/PBC/
missing-data provenance와 optional overlay를 stable result ID에 묶는다. Object 삭제나 topology mutation은
결과를 지우지 않고 source status만 stale로 바꾼다. 상세 계약은
[Persistent analysis results](ANALYSIS_RESULTS.md)에 둔다.

## Topology와 property

작고 접근 빈도가 낮은 atom/residue identity는 record로, 계산에 반복적으로 쓰는 property는
atom 수와 같은 길이의 column으로 저장한다. 현재 property type은 boolean byte, signed/unsigned
64-bit integer, float32, float64와 text다. Boolean column은 값 0 또는 1만 허용한다.

각 property에는 optional unit, source와 key/value annotation을 붙일 수 있다. Static property는
`Topology::properties()`에, frame-dependent property는 `FrameMetadata::atom_properties`에 둔다.
알 수 없는 source-level metadata는 정렬된 map에 보존해 후속 format reader가 정보 손실 없이
round-trip할 수 있게 한다.

Connectivity는 bond order를 가진 bond와 angle, dihedral, improper를 명시적으로 표현한다.
Builder는 존재하지 않는 atom, self/degenerate term과 canonical 방향까지 고려한 duplicate를
거부한다. Atom은 유효한 residue를 참조해야 하며 atomic number는 0(unknown)부터 118까지다.

## Coordinate frame과 source

Position과 optional velocity는 각각 float32 또는 float64 contiguous buffer다. 두 buffer의
precision은 독립적으로 보존되며 reader가 임의로 float64로 승격하거나 float32로 축소하지
않는다. 모든 numeric vector는 finite여야 한다. Missing atom은 좌표 buffer의 자리를 유지하고
별도의 0/1 presence mask로 표현한다. 따라서 placeholder 좌표도 finite여야 하며 소비자는
`atom_present()`를 먼저 확인해야 한다.

Frame metadata는 source step, physical time, coordinate length unit, optional velocity time unit,
unit cell, frame-dependent atom property와 unknown field를 보존한다. Unit cell은 세 개의 lattice
vector로 저장하므로 orthogonal 및 triclinic cell을 모두 표현하며 finite하고 non-coplanar인
right-handed basis만 유효하다. 현재 내부 buffer는 metadata가 선언한 coordinate unit 값을
보존한다. Analysis와 I/O adapter는 단위를 명시적으로 해석하거나 변환해야 하며 암묵적으로
angstrom이라고 가정하면 안 된다.

`CoordinateSource`는 다음을 분리한다.

- atom count
- known 또는 unknown인 optional frame count
- sequential 또는 random-access capability
- frame index로 immutable shared frame lease를 얻는 read operation

`InMemoryCoordinateSource`와 indexed DCD source가 random-access 구현을 제공한다.
`trajectory::FrameCache`는 임의 source를 memory-budgeted LRU로 감싸며 반환한 shared frame lease가
살아 있는 동안 eviction 후에도 storage lifetime을 보장한다. 상세 budget/concurrency 계약은
[Trajectory runtime](TRAJECTORY_RUNTIME.md)에 둔다.

External trajectory는 `MolecularSystem`에 연결되기 전에 exact/index/explicit stable-ID mapping을 거치고,
position/velocity/presence/frame atom property가 하나의 target order로 정규화된다. 이어지는 semantic source는
coordinate/cell을 Å, time/velocity basis를 ps, supported force를 kJ mol^-1 Å^-1로 변환하고 frame별 channel/unit
drift를 거부한다. 상세 계약은 [Trajectory attachment](TRAJECTORY_ATTACHMENT.md)에 둔다.

PBC transform은 source frame을 변경하지 않고 새 immutable frame을 만든다. Position precision,
velocity, presence와 metadata 보존 및 missing placeholder 정책은 [PBC contract](PBC.md)에 둔다.

## Validation contract

Public factory와 builder는 invalid input을 exception 대신 stable `operation::Result` 또는
`operation::Error`로 반환한다. Atom/property/frame count 일치, index 범위, duplicate
connectivity, finite coordinate/time, presence/boolean domain과 unit-cell handedness를 생성
경계에서 검증한다. 유효하게 생성된 immutable object를 consumer가 다시 방어적으로 검사할
필요는 없다.

이 foundation은 stable-ID retain/delete/reorder snapshot transaction을 제공하지만 일반 editing UI,
chemical mutation, format-specific alternate conformation semantics와 reactive topology는 아직
정의하지 않는다. Unit conversion policy와 GPU cache eviction은 각 전용 계약에서 다룬다.

현재 PDB/mmCIF field가 이 model에 매핑되는 방식은 [Structure format
support](FORMAT_SUPPORT.md)에 기록한다.
