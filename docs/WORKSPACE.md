# Workspace molecular workflow

`application::Workspace`는 첫 stateful molecular vertical slice다. 동일 Workspace를 capture한 command
registry를 GUI action, native interactive console과 Python module이 공유할 수 있다. `load`, `select`,
`show`, `analyze center`, `measure distance` command는 더 이상 validation-only stub이 아니며 같은
application state와 core kernel을 호출한다.

## State model

Workspace는 immutable scene snapshot, ordered molecular objects, ordered scalar volume objects, 각각의
active index와 공유되는 다음 object ID를 소유한다. Molecular object는 `MolecularSystem`, scene node,
named selections와 representation record를 묶고 volume object는 `VolumeGrid`, source path, scene node와
isosurface packet list를 묶는다. 두 종류는 하나의 object-name/ID namespace를 사용한다.
Structure reader가 둘 이상의 frame을 반환하면 Workspace는 그 coordinate source를 공통 bounded cache,
playback timeline과 prefetch controller에 자동 등록한다. 따라서 multi-model PDB/mmCIF, GRO/G96/VTF/XYZ는
별도 `traj load` 없이 GUI timeline과 `traj frame`/trajectory analysis에서 같은 state를 사용한다.
Load가 성공하면 각 topology/coordinate source를 system으로 만들고 scene root 아래 node를 추가한 뒤
마지막 object를 active로 설정한다. Duplicate name과 실패한 read/build는 기존 state를 바꾸지 않는다.
Active object와 scene selection은 `object activate`에서 함께 commit되고 `object visibility`는 immutable
scene snapshot을 교체한다. `object list`가 GUI/CLI/Python에 동일한 typed state table을 제공한다.

현재 molecular load는 PDB/mmCIF/BCIF/PQR/MOL/SDF/MOL2/PSF/PRMTOP/GRO/G96/VTF/XYZ/XMol document를 받는다. Multi-model PDB/mmCIF/BCIF,
concatenated GRO, ordered G96 frame block과 multi-block XYZ는
한 object의 coordinate frames로 유지하고, multi-data-block mmCIF 및 multi-record SDF는 ordered object
batch로 펼친다. MOL2의 각 `@<TRIPOS>MOLECULE` record도 같은 ordered batch path를 사용한다. 모든
system/node를 temporary batch에서 완성한 뒤 scene/object/next-ID를 commit하므로
중간 실패가 일부 object를 남기지 않는다. Result의 `structure_count`, `object_ids`, `objects[]`는 모든
생성 object와 source record index를 노출하며 기존 singular field는 active/last object를 가리킨다.
PSF와 PRMTOP은 topology와 atom count가 있지만 frame count가 0인 정상 system이다. `traj load`는 같은 atom
count/order의 DCD/XTC/TRR/MDCRD/Amber NetCDF/H5MD/LAMMPS/BINPOS indexed coordinate source 또는 single-frame Amber RST7을 이 system에 붙인다.
Attach 전 `show`나 coordinate
analysis는 좌표를 발명하지 않고 actionable `not_found`를 반환한다.

`volume load`는 OpenDX 또는 MRC2014/CCP4 document 전체를 먼저 parse/validate하고 하나의 typed volume scene node를 만든 뒤
Workspace에 commit한다. Duplicate molecular/volume name이나 read/build 실패는 기존 scene, object vector,
active index와 next ID를 바꾸지 않는다. Molecular active object와 active volume은 독립적이므로 potential
map을 load해도 현재 molecular representation과 trajectory state를 폐기하지 않는다. `volume list`는
dimensions, precision, scalar range, coordinate unit, representation count와 scene visibility를 동일
GUI/CLI/Python result로 노출한다. `volume isosurface`는 mesh를 먼저 완성한 뒤 append/replace를 commit한다.
Volume session persistence는 아직 없다.

## Command sequence

```text
invoke "load" --file-format "pdb" --name "protein" --path "input.pdb"
invoke "select" --expression "chain A" --name "chain_a" --update "false"
invoke "show" --replace "true" --representation "spheres" --selection "@chain_a"
invoke "analyze center" --mode "com" --selection "@chain_a" --unit "angstrom"
invoke "measure distance" --from "index 1" --to "index 2" --mode "atom" --pbc "raw"
invoke "volume load" --file-format "opendx" --name "potential" --path "potential.dx" \
  --coordinate-unit "angstrom"
invoke "volume list"
invoke "volume isosurface" --level "0.5" --color "cyan" --replace "true"
```

MRC/CCP4도 같은 sequence에서 `--file-format mrc|map|ccp4|mrcs`로 load할 수 있으며 OpenDX와
동시에 별도 object/scene node로 유지된다.

Select는 active object의 named-selection registry를 atomic하게 갱신한다. Show는 active object의 현재
frame에서 expression을 평가하고 default atom visuals와 representation packet을 생성해 object에
append한다. Result에는 object/representation ID, selected atom 수와 primitive 수가 포함된다.
`--replace true`는 새 selection/packet을 먼저 완성한 뒤 기존 representation list를 교체하므로
validation 또는 geometry 생성 실패 시 이전 packet을 보존한다. Desktop preset toolbar는 누적되지 않는
single-preset 의미를 위해 이 option을 사용하고 일반 `show`의 additive 기본은 유지한다.
`ribbon`과 `cartoon`은 residue backbone mesh를 생성하며 triangle 수를 primitive 수로 보고한다.
Trajectory frame 변경 시 secondary structure와 mesh도 transactionally 다시 계산된다.

Default visuals는 현재 H/C/N/O/S의 임시 element color/VDW radius와 fallback style이다. 이는 color
system이나 publication style 계약이 아니며 후속 representation/color module에서 versioned preset으로
교체한다.

Analyze center는 active object의 현재 frame에서 selection을 평가하고 centroid 또는 COM을 계산한다.
분석 결과는 Workspace를 바꾸지 않는 pure result다. Measure distance는 각 endpoint expression이
정확히 한 atom을 선택해야 하며 성공 시 monotonic measurement ID를 가진 record를 Workspace에
append한다. Record는 raw/minimum-image boundary를 보존하며 실패한 측정은 기존 record를 바꾸지
않는다. Minimum-image는 active frame의 exact triclinic PBC kernel을 사용하고 unit cell이 없으면
실패한다. Selection reduction과 measurement object의 session serialization은 후속 범위다.
Toolkit-independent GUI presentation은 center point marker와 distance atom-anchor marker를 만들지만
실제 scene geometry/QML overlay는 아직 연결하지 않는다.

## Frontend와 lifetime

GUI `ActionAdapter`와 `Console`은 같은 registry가 capture한 Workspace를 사용하면 연속 action의
state를 공유한다. Native one-shot command는 process 종료와 함께 state가 사라지므로 현재 연속
terminal workflow는 `molshredder console` 안에서 수행한다. Session save/reload와 daemon IPC는 아직
없다. Python module의 default registry는 module lifetime 동안 state를 유지한다.

Workspace mutation은 현재 single-thread 전제다. UI/render worker 동시 접근, task cancellation 중
transaction과 undo snapshot은 후속 application/session 작업에서 추가한다. Active-object/visibility
operation은 현재 failure-atomic이지만 rename/delete/group/reorder는 아직 없다.
Representation packet은 생성 당시 frame의 snapshot이다. `traj frame/play`는 기존 representation을
도착 frame에서 전부 transactional rebuild한다. Command 밖의 임의 source 변화에는 자동 반응하지
않으며 coordinate-dependent selection, incremental rebuild와 render-thread GPU resource update는 아직
없다. Attach/seek/play 상세 계약은
[Trajectory commands](TRAJECTORY_COMMANDS.md)에 둔다.
