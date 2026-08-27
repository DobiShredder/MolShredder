# Editing and builders

MolShredder의 molecular edit는 GUI, CLI와 Python에서 동일한 canonical operation을 호출한다. Coordinate,
atom/residue property와 bond-order transaction은 candidate topology/coordinate source, named selection remap,
representation과 scene을 검증한 뒤 한 번에 교체한다. 검증·cancellation·memory reservation·representation
rebuild 중 오류가 나면 Workspace는 변하지 않는다.

```text
edit atom-position --atom-id 1 --x 9 --y 8 --z 7 \
  --expected-topology-version 1 \
  --expected-coordinate-source-revision 1 \
  --unit angstrom
edit atom-properties --atom-id 1 --name C1 --atomic-number 6 \
  --formal-charge 1 --expected-topology-version 1 \
  --expected-coordinate-source-revision 1
edit residue-properties --atom-id 1 --name CRB --chain B \
  --residue-number 7 --expected-topology-version 2 \
  --expected-coordinate-source-revision 2
edit bond-order --bond-id 1 --order single \
  --expected-topology-version 3 \
  --expected-coordinate-source-revision 3
edit undo
edit redo
edit history
edit history --memory-budget-bytes 268435456
```

`atom-id`는 ordinal index가 아닌 non-zero 64-bit stable ID다. 호출자는 읽은 topology와 coordinate
source revision을 함께 보내야 하며 둘 중 하나라도 바뀌면 `stale_result`로 거부된다. 입력 좌표는 유한해야
하고 `angstrom` 또는 `nanometer`를 받는다. 이 operation은 모든 정적 coordinate state에서 해당 원자의
absolute position을 변경하며 응답에 before/after revision, previous/new position, affected state count,
무효화된 measurement 수와 undo snapshot byte 수를 반환한다.

Property edit는 omitted field를 유지한다. Atom은 name, atomic number 0–118과 signed formal charge,
residue는 그 residue에 속한 stable atom ID를 기준으로 name/chain/sequence number, bond는 stable bond ID를
기준으로 `single`, `double`, `triple`, `aromatic`, `amide`를 받는다. Stable atom/bond identity와 cardinality는
변하지 않고 topology version은 정확히 1 증가한다. Static named selection은 stable-ID identity remap으로
같은 원자를 유지하며 dynamic selection은 새 topology에서 다시 평가된다. Existing measurement는 제거되고
persistent scientific result는 삭제하지 않되 revision contract에 따라 `topology_changed` 또는
`coordinate_changed`로 표시된다.

연결된 trajectory는 source frame과 cache의 소유권을 모호하게 만들지 않기 위해 직접 편집하지 않는다.
먼저 trajectory를 분리하거나 향후 제공할 static-copy workflow를 사용해야 한다. 한 state에서라도 원자가
missing이면 전체 transaction을 거부한다.

Undo history는 immutable molecular-system snapshot을 보유하며 기본 budget은 256 MiB다. 새 edit는 redo
branch를 폐기하고, budget 축소는 가장 오래된 transaction부터 결정적으로 제거한다. 기록할 snapshot 하나가
budget보다 크면 현재 구조를 바꾸기 전에 edit를 거부한다. Undo/redo가 적용될 때 coordinate revision은
되돌아가지 않고 계속 증가하므로 이전 analysis completion이 현재 결과로 오인되지 않는다.

모든 edit 응답은 `diff_schema_version=1`과 `transaction_kind`를 가진다. 현재 kind는
`atom_position`, `atom_properties`, `residue_properties`, `bond_order`, `molecule_build`다. Property diff는
previous/new field와 before/after topology/source revision을 포함하며 Undo/Redo도 이동한 transaction kind를
반환한다. Canonical invocation journal은 builder와 편집 및 undo/redo를 새 Workspace에서 결정적으로 replay한다.
Self-contained full-session 및 autosave/recovery schema는 별도 session milestone 범위다.

## Molecule builder

```text
build molecule --name carbonyl \
  --atoms "C,6,0,0,0,0;O,8,1.2,0,0,0" \
  --bonds "1,2,double" --residue-name LIG --chain A \
  --residue-number 1 --unit angstrom --memory-budget-bytes 1048576
```

현재 builder는 하나의 ligand residue와 하나 이상의 원자, optional bond 및 한 정적 coordinate state를
생성하는 generic kernel이다. Atom row는 `name,atomic-number,x,y,z,formal-charge`, bond row는 1-based
`first,second,order`다. Stable atom/bond ID는 request 순서로 발급된다. Duplicate/self/missing bond,
unknown residue, non-finite coordinate, invalid element/order, cancellation과 builder 또는 undo memory budget
초과는 object를 publish하기 전에 실패한다. 성공한 object-create transaction은 Undo로 object를 제거하고 Redo로
같은 stable object/system identity를 복원한다.

## 지원 범위

| 영역 | 상태 | 현재 contract |
|---|---|---|
| 좌표 이동 | 구현 | stable atom ID, all-static-state, unit/revision validation, GUI·CLI·Python parity |
| 좌표 undo/redo | 구현 | bounded memory, redo branch invalidation, deterministic eviction |
| 원자 삭제/reorder | low-level | `object topology-retain`; 일반 편집 UI와 undo integration은 아직 없음 |
| atom/residue/bond property edit | 구현 | stable ID, typed before/after diff, static selection remap, result staleness, GUI·CLI·Python parity |
| generic residue/atom/bond builder | 구현 | one ligand residue, one static state, bounded/cancellable validation과 object-create undo/redo |
| hydrogen, mutation, template fragment, peptide/nucleic-acid builder | 미구현 | template/protonation/chemistry policy와 independent fixtures가 필요함 |
| torsion/sculpt/minimize | 미구현 | force-field 또는 external minimizer integration을 확정하지 않음 |

표의 미구현 항목은 지원한다고 표시하지 않는다. Generic row builder는 PyMOL fragment catalog, VMD Molefacture,
valence completion, protonation, force-field minimization 또는 일반 PyMOL/VMD editing parity를 의미하지 않는다.
