# Structure writing and semantic loss

`save`는 active object를 native PDB/mmCIF/MOL/SDF/MOL2/PSF/PQR/GRO/G96/XYZ writer로 내보낸다. PDB, mmCIF, GRO, G96와 XYZ는
current/all frame, MOL/SDF/MOL2/PQR은 exactly one current frame을 지원한다. PSF는 topology-only라 frame을
요구하거나 출력하지 않는다.

```text
molshredder format list [--family all|structure|trajectory]
                           [--direction all|read|write]
molshredder save --path OUTPUT.pdb|OUTPUT.cif|OUTPUT.mol|OUTPUT.sdf|OUTPUT.mol2|OUTPUT.psf|OUTPUT.pqr|OUTPUT.gro|OUTPUT.g96|OUTPUT.xyz|OUTPUT.xmol
                 [--file-format auto|pdb|mmcif|cif|mol|sdf|mol2|psf|pqr|gro|g96|xyz]
                 [--provider auto|native]
                 [--frames current|all] [--precision 0..15]
                 [--comment TEXT] [--overwrite false|true]
```

`--provider`는 shared schema v3 registry의 provider ID를 선택한다. `auto`는 qualified native provider를 우선하며,
명시한 provider가 없거나 해당 write 방향을 지원하지 않으면 다른 구현으로 fallback하지 않는다. Save result에는
선택된 provider의 version/origin/trust/license provenance가 포함된다.

One-shot CLI process는 Workspace를 공유하지 않으므로 `load` 뒤 `save`하는 workflow는 console이나 Python에서
실행한다.

```text
molshredder console
> invoke "load" --path "input.pdb"
> invoke "save" --path "models-copy.pdb" --file-format "pdb" --frames "all"
> invoke "load" --path "models.cif" --name "cif_models"
> invoke "save" --path "models-copy.cif" --file-format "mmcif" --frames "all"
> invoke "save" --path "output.xyz" --frames "current"
> invoke "load" --path "charged.pqr" --name "charged"
> invoke "save" --path "charged-copy.pqr" --precision "4"
> invoke "load" --path "library.sdf" --name "ligand"
> invoke "object activate" --id "3"
> invoke "save" --path "ligand.sdf" --file-format "sdf"
> invoke "load" --path "typed-library.mol2" --name "typed"
> invoke "save" --path "typed-copy.mol2" --file-format "mol2"
> invoke "load" --path "trajectory.gro" --name "water"
> invoke "save" --path "water-copy.gro" --file-format "gro" --frames "all"
> invoke "load" --path "trajectory.g96" --name "gromos"
> invoke "save" --path "trajectory-copy.g96" --file-format "g96" --frames "all"
> invoke "load" --path "system.psf" --name "topology"
> invoke "save" --path "system-copy.psf" --file-format "psf"
```

`save` response는 format, object/atom/frame/byte count와 loss channel count를 fields로, 각
`channel,count,message`를 table로 반환한다. JSON의 `format list` 결과는 scalar summary table과 함께
`data.formats[]`에 extensions/channels/limitations array를 보존한다.

File writer는 target과 같은 directory에 temporary output을 만들고 전체 frame을 순차적으로 write/flush한
뒤 publish한다. Cancel, frame decode, disk write 또는 publish 실패는 target을 만들거나 교체하지 않는다.
기본 `overwrite=false`는 기존 파일을 보존한다. `serialize_structure()`는 test/small-memory workflow용이며
전체 결과 string을 memory에 보유하므로 대형 trajectory에는 file writer를 사용한다.

Plain XYZ는 residue/bond/order, charge, typed property, cell, velocity, time 및 arbitrary metadata를 저장하지
못한다. Writer는 이 정보를 조용히 버리지 않고 loss report에 누적한다. Missing atom, invalid atomic number,
unknown frame count 또는 topology/coordinate atom-count mismatch는 valid output을 만들 수 없으므로 hard error다.
`.xmol` suffix는 같은 plain XYZ writer를 선택하는 XMol 호환 alias이며 explicit format 이름은 `xyz`다.

PDB writer는 wwPDB 3.3 fixed-column `ATOM/HETATM`, optional `MODEL/ENDMDL`, `CRYST1`, formal charge와
`CONECT`를 출력한다. 첫 selected model은 topology의 모든 atom을 포함해야 하며 later model의 missing atom은
record omission으로 보존한다. Atom/residue/chain/altLoc/insertion/segment identity와 serial이 고정 열에 맞아야
하고 invalid/duplicate atom serial만 1-based coordinate order로 정규화해 loss로 알린다. Coordinate는 F8.3,
occupancy/B-factor는 optional F6.2이며 selected model의 unit cell은 모두 같아야 한다. Cell geometry는 보존하지만
현재 공통 model이 space group/Z를 Workspace에 유지하지 않으므로 `P 1`, Z=1을 출력하고 symmetry loss를 보고한다.
Explicit bond endpoint는 보존하지만 bond order, angle/dihedral/improper, velocity, physical time과 arbitrary property는
typed loss다. Hybrid-36, ANISOU, LINK/SSBOND, TER/sequence/assembly record와 99,999 atom 초과 구조는 지원하지 않는다.

mmCIF writer는 CIF 1.1 quoting과 missing marker를 적용해 `_atom_site` multi-model coordinate와
`_struct_conn` explicit bond/order를 출력한다. Label/auth atom·residue·asym/sequence identity, alt ID,
insertion code, formal charge, occupancy/B-factor, later-model presence와 일정한 unit cell을 보존한다. 첫 selected
model은 complete해야 하며 atom-site ID와 duplicate/missing model number는 deterministic하게 정규화하고 loss로
보고한다. 현재 공통 model에 없는 entity 관계, space-group/assembly 및 arbitrary dictionary category는 발명하지
않고 unknown marker 또는 typed loss로 노출한다. 한 번의 save는 active object 한 data block만 출력한다.

PQR은 atom/residue/optional chain identity, Cartesian coordinate, partial charge와 radius를 저장한다.
`partial_charge` float property의 unit은 `elementary_charge|e`, `pqr.radius`는 `angstrom|nanometer`여야
한다. Writer는 단위를 PQR의 elementary charge/Å로 변환하며 property가 없거나 radius가
non-positive이면 값을 발명하지 않고 hard error를 반환한다. `pqr.radius`가 없을 때만
explicit `vdw_radius`를 fallback으로 인정한다. PQR은 single-frame format으로 취급한다. Selected frame에
unit cell이 있으면 VMD PQR 0.6 호환 `CRYST1` record로 geometry를 보존하고 space group/Z는 알 수 없으므로
`P 1`/`1` 대체를 loss report에 기록한다.

MOL/SDF는 MDL V2000의 single/double/triple/aromatic bond order, formal charge, isotope와 radical을
보존한다. V2000은 record당 atom/bond가 각각 최대 999이고 coordinate는 F10.4이므로 범위를 벗어난 값이나
unknown bond order는 hard error다. MOL은 SDF data field를 저장하지 않아 loss를 보고한다. SDF는 active
object 하나를 한 record와 `$$$$` delimiter로 내보내며 현재 multi-object batch writer는 없다.

MOL2는 explicit `mol2.atom_type` SYBYL type, partial charge와 presence, substructure ID/name, atom/bond
status, single/double/triple/aromatic/amide bond 및 `CRYSIN` cell을 저장한다. Reader에서 얻은 valid positive
atom/bond identifier와 bond endpoint 순서를 보존한다. Writer는 일반 PDB atom name으로 SYBYL type을 추정하지
않으며 explicit type이 없으면 hard error다. Dummy/query/not-connected bond와 FEATURE/SET/UNITY 같은 optional
section은 아직 출력하지 않는다. Active object 하나만 출력하므로 multi-molecule batch export는 후속 범위다.

PSF는 zero-frame topology에서도 동작하고 `PSF EXT XPLOR`을 출력한다. 모든 atom에 explicit
`psf.atom_type`, `partial_charge`, `mass`가 있어야 하며 일반 element/name으로 force-field typing을
발명하지 않는다. Bond/angle/dihedral/improper와 duplicate torsion term을 보존한다. Coordinate frame,
bond order, chain/alternate location/formal charge 및 보존하지 못한 auxiliary force-field section은 loss
table에 나타난다.

GRO는 stable atom order의 여러 frame, nm 좌표, optional nm/ps velocity, ps time과 3/9-value unit cell을
저장한다. Position precision은 1..15이며 velocity는 같은 field width에서 한 자리 더 많은 소수 자릿수를
사용한다. Atom/residue name은 5자, 번호는 0..99999 범위를 요구한다. Connectivity, charge, explicit element와
임의 property는 loss report에 기록한다. Frame마다 velocity 유무가 다른 것은 허용하지만 한 frame 안의
atom별 velocity 유무가 섞인 데이터는 reader가 거부한다. `--comment`는 frame title을 바꾸지만 typed physical
time이 있으면 frame마다 `t=`를 다시 붙인다. 기존 title의 parseable `t=`는 typed time으로 갱신하며 typed
time 없이 parseable title time만 남아 있으면 stale scientific metadata로 판단해 export를 거부한다.

G96는 하나의 `TITLE` 뒤에 frame별 optional `TIMESTEP`, required `POSITION/POSITIONRED`, optional
`VELOCITY/VELOCITYRED`와 `BOX`를 순서대로 기록한다. Writer는 topology identity를 보존하기 위해 full
`POSITION`/`VELOCITY`와 고정 F15.9를 사용한다. `--precision`이 9가 아니어도 파일 규격의 9자리로 출력하고
그 차이를 loss report에 기록한다. 물리 시간이 있지만 step이 없으면 frame index를 합성해 loss로 알리며,
step만 있고 시간이 없으면 `TIMESTEP`을 생략하고 loss로 알린다. Connectivity, charge, explicit element와
arbitrary property는 저장하지 않는다.
