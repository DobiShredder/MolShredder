# Persistent identity and numeric contract

MolShredder는 dense ordinal/index와 persistent identity를 분리한다. Object, atom과 bond ID는
non-zero `uint64`이고 topology snapshot은 monotonic `uint64` version을 가진다. Ordinal은 현재
snapshot의 배열 위치일 뿐 저장 가능한 atom/bond identity가 아니다.

`TopologyBuilder::from`으로 만든 새 immutable snapshot은 version을 하나 올린다. Insert는 새 ID를
발급하고, delete/reorder는 surviving ID를 그대로 유지한다. `TopologyRemap`은 source→target과
target→source ordinal을 atom/bond별 `optional` mapping으로 제공한다. Null mapping은 삭제되었거나
새로 삽입되어 source coordinate가 없다는 뜻이다.

## Reference와 transaction

Persistent atom/bond reference는 schema version, object ID, 관찰한 topology version과 stable ID를
저장한다. 같은 object의 surviving ID는 newer topology에서 새 ordinal로 resolve되며, 삭제된 ID는
`not_found`다. Cross-object, future/incompatible schema reference와 exact snapshot version이 필요한
mutation의 stale version은 `invalid_argument`다. 실패한 mutation은 system, scene, setting,
selection, visibility와 measurement state를 바꾸지 않는다.

`object topology-retain --atom-ids IDS --expected-version VERSION`은 이 계약을 검증하는 low-level
canonical operation이다. Coordinate의 모든 frame/channel/property를 같은 mapping으로 감싸며 삽입된
source-missing atom은 finite zero placeholder와 `presence=0`을 사용한다. Static selection과
representation visibility는 stable ID로 remap되고 surviving render-setting override도 stable ID를
따른다. 삭제된 override는 제거하고, 아직 persistent result schema가 없는 measurement는 잘못된
ordinal 재사용을 막기 위해 명시적으로 invalidate한다.

## Serialization과 수치 정책

- Object/topology/atom/bond ID는 decimal unsigned 64-bit 범위 전체를 검증한다. `2^32`는 valid
  parse이고 `2^64-1`도 parse할 수 있지만 존재하지 않는 target이면 `not_found`다. `2^64` 이상은
  `invalid_argument`다.
- In-memory count/index는 `size_t`이며 외부 count를 변환할 때 platform `size_t` 범위를 검사한다.
- Host-side pick identity는 uint64이고 indexed GPU mesh는 uint32다. `checked_gpu_mesh_index`는
  `2^32-1`을 허용하고 그 이상을 geometry packet 분할이 필요한 typed error로 거부한다.
- Render-setting snapshot schema 2의 atom/bond target은 stable ID다. 이전 ordinal 의미의 schema 1을
  묵시적으로 읽지 않는다.
- Representation visibility session schema 2는 object ID, topology version과 ordered stable atom ID
  table을 mask와 함께 저장한다. Revision/order/schema가 다르면 restore하지 않는다.
- Coordinate와 velocity는 finite 값만 허용한다. Missing atom은 NaN이 아니라 `presence=0`과 finite
  placeholder로 표현한다. Frame metadata는 coordinate unit, time unit과 remap provenance를 보존한다.
- Scientific tolerance와 output unit은 각 analysis result/provenance가 소유한다. Identity/count 비교는
  exact이며 이 계약이 임의의 floating-point tolerance를 발명하지 않는다.
