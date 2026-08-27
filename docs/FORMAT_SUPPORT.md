# Structure and volume format support

상태: versioned native read/write foundation
검증 기준일: 2026-08-16

MolShredder core는 native PDB, PDBx/mmCIF, BinaryCIF 0.3.x, PQR, MDL MOL V2000, SDF V2000, Tripos MOL2,
CHARMM/NAMD PSF, Amber PRMTOP/RST7/MDCRD/NetCDF, GROMACS GRO, GROMOS-96 G96, VMD VTF 및 plain XYZ structure/coordinate reader와
PDB/PDBx-mmCIF/PQR/MOL/SDF/MOL2/PSF/GRO/G96/XYZ writer를 제공한다.
APBS-compatible ASCII OpenDX와 MRC2014/CCP4 regular scalar grid는 독립 typed volume object로
read/write한다.
Structure public API는 `molshredder/io/structure_reader.hpp`와 `structure_writer.hpp`이고 trajectory
I/O API는 `trajectory_reader.hpp`와 `trajectory_writer.hpp`, volume API는 `volume_reader.hpp`와
`volume_writer.hpp`다. Structure와 volume은 memory
content 또는 file path를 읽고, writable structure format은
memory 또는 failure-atomic file로 쓸 수 있다. Format을
명시하거나 첫 meaningful record로 자동 판별한다. Reader는 application object ID를 만들지 않고
`StructureDocument` 안에 topology와 coordinate source를 반환하므로 application layer가 object
tree identity를 할당한다.
PQR과 PDB는 ATOM record만으로는 occupancy/B-factor와 charge/radius가 구분되지 않을 수
있으므로 PQR은 `.pqr` suffix 또는 explicit `--file-format pqr`로 선택한다.

## Capability matrix

| Channel | PDB 3.3 read/write | PDBx/mmCIF read/write | PQR read/write | MOL/SDF V2000 read/write | MOL2 read/write | PSF read/write | GRO read/write | G96 read/write | plain XYZ read/write |
|---|---|---|---|---|---|---|---|---|---|
| Atom/residue identity | `ATOM`, `HETATM`, altLoc, chain, resSeq, iCode, segID, serial | `_atom_site` label 및 author identifier, alt ID, atom-site ID | atom/residue/optional chain/serial을 보존; iCode/segID/altLoc는 loss 보고 | element/query `*`와 synthetic residue; atom/residue 이름은 loss 보고 | atom ID/name, substructure ID/name와 chain; valid source atom/bond ID 보존 | atom ID/name, segment, residue ID/name/insertion; writer는 coordinate order로 atom ID 정규화 | fixed-width residue/atom number와 5-character name; valid atom number 보존 | full block은 I5 residue, 5-character names, I7 atom ID; reduced 첫 frame은 synthetic unknown identity | element 또는 `X`; synthetic `MOL` residue/index identity, writer는 identity loss 보고 |
| Chemistry | element, optional formal charge | element, nullable formal charge | inferred element, partial charge와 PQR radius | element, formal charge, isotope, radical, mass difference, atom parity 및 single/double/triple/aromatic bond | SYBYL atom type, optional partial charge/presence, single/double/triple/aromatic/amide bond와 status | force-field atom type, partial charge, mass, bond/angle/dihedral/improper와 duplicate torsion multiplicity; bond order 없음 | atom/residue name 기반 conservative element inference; connectivity/charge loss 보고 | full identity name 기반 conservative element inference; connectivity/charge loss 보고 | element만 보존, chemistry loss 보고 |
| Coordinate | fixed-column Cartesian Å, float64; writer는 F8.3 | `Cartn_x/y/z` Å, float64 | Cartesian Å, writer precision 0..15 | Cartesian Å; writer는 V2000 fixed F10.4 | Cartesian Å; writer precision 0..15 | topology-only, zero frame; DCD/XTC/TRR/MDCRD/NetCDF/H5MD/RST7/LAMMPS/BINPOS를 ID/count로 attach | Cartesian nm, 가변 precision; optional velocity nm/ps | Cartesian nm, fixed F15.9; optional velocity nm/ps | Cartesian Å; writer precision 0..15 |
| Multiple frame/structure | model은 frame | model은 frame, 여러 data block은 여러 structure | single frame/structure | MOL은 single record, SDF는 여러 record를 여러 structure로 반환; record당 single frame | 여러 `MOLECULE`을 여러 structure로 반환; molecule당 single frame | single topology/zero frame | stable identity의 concatenated frame을 한 structure로 결합 | ordered frame block을 stable identity의 한 structure로 결합 | stable atom order의 연속 block을 frame으로 결합 |
| Missing atom | explicit presence mask; writer 첫 model은 complete, later model은 record omission | explicit presence mask | 표현 불가; hard failure | 표현 불가; hard failure | 표현 불가; hard failure | 해당 없음(좌표 없음) | 표현 불가; hard failure | 표현 불가; hard failure | 표현 불가; hard failure |
| Unit cell | `CRYST1`; writer는 model 간 constant geometry와 P 1/Z=1 symmetry loss | `_cell` | 표현 불가; loss 보고 | 표현 불가; loss 보고 | triclinic `CRYSIN`과 optional group/setting | 표현하지 않음 | 3-value orthorhombic 또는 9-value triclinic box | optional 3/9-value `BOX` block | 표현 불가; loss 보고 |
| Explicit bond | deduplicated `CONECT`, unknown order | `_struct_conn`와 bond order | 표현 불가; loss 보고 | V2000 atom pair와 bond order 보존 | atom pair, bond ID/order/status 보존 | bond와 higher-order connectivity를 ordered term으로 보존; bond order 없음 | 표현 불가; loss 보고 | 표현 불가; loss 보고 | 표현 불가; loss 보고 |
| Source metadata | entry/title/space group | data-block scalar item | optional `REMARK`; typed loss 보고 | 세 header line, unknown property line 및 unique SDF data field | molecule type/charge type/extra, substructure rows와 unknown section | title, variant와 unmodeled auxiliary section count | frame title 및 optional `t=` physical time ps | mandatory `TITLE`, optional TIMESTEP step/time과 full/reduced block kind | frame comment |

Provider-neutral `format list` registry schema v3는 BCIF, H5MD, OpenDX와 MRC/CCP4를 포함한 24개 native
structure/trajectory/volume format을 함께 열거하고 extension,
read/write, multi-frame, multi-structure, random-access, streaming, data channel, limitation, implementation 및
provider identity/version/origin/trust/license를 typed JSON으로 반환한다. Read/write 방향별 availability와 unavailable
reason, channel, limitation 및 typed-loss 지원도 별도 row다. 모든 I/O command의 `--provider`와 성공 result가 같은
provider provenance를 사용하며 explicit override는 silent fallback하지 않는다. 상세 계약은
[Format provider contract](FORMAT_PROVIDER_CONTRACT.md)에 둔다. 이 registry와 fixture가 실제 capability의
authoritative inventory이며 이 표는 사람용 설명이다.

## OpenDX regular scalar volume

OpenDX reader는 `gridpositions`, origin, 세 delta vector, 동일한 `gridconnections`와 rank-0
`float`/`double` inline array 하나를 읽는다. Scalar는 z-fastest 순서를 유지하고 axis-aligned grid로
근사하지 않으므로 skewed delta도 그대로 보존한다. Format에 coordinate unit이 없기 때문에 기본 APBS
Angstrom 또는 사용자가 지정한 nanometer provenance를 기록하고 scalar unit은 발명하지 않는다.

Writer는 float32/float64, skewed delta, singleton axis와 z-fastest scalar order를 보존하며 coordinate/scalar
unit 및 auxiliary metadata의 비표현을 typed loss로 보고하고 failure-atomic file publish를 사용한다.
현재 binary/irregular/vector/multiple-field OpenDX와 out-of-core I/O는 명시적으로 지원하지 않는다. Typed grid와
command/scene 및 marching-tetrahedra isosurface 연결은 완료됐지만 slice와 direct volume rendering은 아직 없다. 자세한 계약은
[Volumetric data](VOLUMETRIC_DATA.md)에 둔다.

## MRC2014/CCP4 scalar volume

MRC reader는 1024-byte header와 `NSYMBT` extended-header offset을 bounds-check하고 `MACHST`에 따라
little/big-endian scalar mode 0/1/2/6/12와 mode 16 RGB grayscale을 float32 grid로 복원한다. IMOD
stamp/flag가 있는 mode 0은 signed/unsigned 의미를 보존한다. `MAPC/MAPR/MAPS` permutation을
logical X/Y/Z shape와 z-fastest buffer로 재배열하며, cell length/angle와 sampling count에서 triclinic delta를
계산한다. Nonzero ORIGIN을 우선하고 그렇지 않으면 permuted start index를 사용하며 충돌은 metadata에 남긴다.

Complex mode 3/4, packed mode 101, CCP4 skew transform과 extended-header payload 해석은 아직 지원하지
않는다. MRC2014 handedness ambiguity는 임의 반전하지 않고 provenance로 노출한다. Header statistics는
source metadata이고 authoritative scalar range는 실제 decoded values에서 계산한다.

Writer는 canonical crystallographic basis를 little-endian mode 2 MRC2014로 출력하고 logical z-fastest
buffer를 column-fastest disk order로 재배열한다. Angstrom geometry, space group과 scalar statistics를
기록하며 float32 narrowing, handedness, scalar unit, metadata와 label normalization은 typed loss다.
Arbitrarily rotated basis는 silent projection하지 않고 OpenDX export를 안내한다. File publish는
failure-atomic이며 overwrite와 cancellation을 지원한다.

Static source values는 typed topology property에, model별 occupancy/B-factor와 missingness는 frame
property에 보존한다. `.`와 `?`인 numeric mmCIF value나 blank PDB value를 임의의 과학 값으로
대체하지 않고 finite placeholder와 별도의 `*_present` boolean column으로 표현한다. Later model은
model 1의 immutable atom identity에 맞춰 배치하며 누락은 허용하지만 새 atom이나 duplicate
identity는 오류다.

PDB reader는 HEADER deposition date와 REMARK, 구조로 직접 소비하지 않는 record 및 CONECT 원문을
`molecule_remarks` metadata로 보존한다. 이는 VMD molfile molecule-metadata channel과 대응하지만 원문 record를
구조 의미로 자동 해석했다는 뜻은 아니다.

PDB writer는 selected current/all frame을 순차적으로 wwPDB 3.3 fixed-column record로 출력한다. Atom serial,
atom/residue/chain/altLoc/insertion/segment identity, element/formal charge, optional occupancy/B-factor, later-model
presence, 일정한 triclinic cell geometry와 explicit bond endpoint를 보존한다. Source serial이 I5에 맞지 않거나
중복이면 coordinate order로 정규화하고, coordinate F8.3/occupancy·B-factor F6.2 범위 밖 값은 hard error다.
Space group/Z가 현재 Workspace topology에 유지되지 않으므로 cell이 있으면 P 1/Z=1을 명시하고 symmetry loss를
보고한다. Bond order, higher connectivity, velocity/time, arbitrary property와 PDB의 non-coordinate record는 loss
report 또는 아래 limitation으로 노출한다.

mmCIF lexer/writer는 CIF 1.1의 data block, case-insensitive data name, key/value item, single-level loop,
single/double quote, line-start semicolon text field, comment 및 `.`/`?` missing token을 처리한다.
Loop는 한 category의 column만 가져야 하며 value 수가 column 수의 양의 배수인지 검증한다.
여러 `data_` block은 document 안의 여러 structure로 반환한다.
긴 label/auth chain과 quoted atom identifier는 고정 ABI field로 자르지 않고 원문 string으로 보존한다.
VMD의 historical `mmcif` 0.2 registration은 callback이 동작하지 않는 stub이고, `pdbx` 0.14가 별도 실제
구현이다. Native 경로는 두 registration의 일반 structure/writer 기능을 대체하지만 PDBx의 실험적
`_ihm_sphere_obj_site` raw graphics는 아직 scene object로 가져오지 않는다.

mmCIF writer는 active structure를 한 data block으로 내보내며 `_atom_site`의 label/auth identity, model number,
coordinate, occupancy/B-factor, formal charge와 `_struct_conn` endpoint/order를 기록한다. 첫 model complete와
model 전체의 constant cell을 요구하고 later-model missing atom은 row omission으로 보존한다. Entity/assembly/
space-group 관계와 전체 원본 dictionary는 현재 model에 없으므로 생성하지 않으며 atom-site ID/model number
정규화, decimal precision, higher connectivity 및 생략 metadata를 typed loss report에 기록한다.

BinaryCIF reader는 format version `0.3.x`의 MessagePack hierarchy를 bounds-checked decode한다. 공식
ByteArray, FixedPoint, IntervalQuantization, RunLength, Delta, IntegerPacking과 StringArray encoding을 적용
역순으로 복원하며 little-endian numeric type, `rowCount`, `srcSize`, dictionary offset/index와 column mask
`0/1/2`를 검증한다. Decoded category는 text serialization을 거치지 않고 mmCIF의 typed block/topology builder로
전달되므로 `_atom_site`, `_struct_conn`, `_cell`, multi-model과 multi-block 의미가 같은 경로를 사용한다.
현재 `.bcif` raw MessagePack read-only이며 `.bcif.gz`, writer, non-PDBx volume BinaryCIF와 arbitrary retained
category table은 후속 범위다. 2026-08-14 RCSB model server의
[`1CRN.bcif`](https://models.rcsb.org/1crn.bcif), SHA-256
`9b0b12f855cbe1ec15b45d893e28c8f22cf4a616565474dae85d69af30648d86`을 local-only 실제 파일 교차검증에 사용했다.
Normative format reference는 BinaryCIF repository commit `ce75b24289746edc28dcef9a703afca2c7e74d81`이고,
Mol* commit `5bd9cb1f3075347db16aa0a46f771907e5889a29`은 source 복사 없이 behavior cross-check에만 사용했다.

Plain XYZ는 positive atom count, required comment line 및 정확히 `label x y z` 네 column을 읽는다. Label은
IUPAC element symbol, periodic-table ordinal `0..118` 또는 unknown site `X`이며 ordinal은 canonical symbol로
정규화한다. `.xyz`와 XMol 호환 `.xmol` suffix는 같은 native reader/writer를 선택한다. 연속 block은 같은 atom
count/order/element일 때에만 frame으로 결합한다. 현재 extended XYZ property/lattice column은 조용히 버리지 않고
source line이 있는 error로 거부한다.

PQR은 APBS가 사용하는 whitespace-delimited `record serial atom residue [chain] residue-number x y z
charge radius` 10/11-field form을 읽는다. Charge는 elementary charge, radius는 Å 단위의 static
float64 property로 보존한다. PQR에 element field가 없으므로 conventional atom name, elemental
residue와 명확한 halogen/iron name에서 conservative inference한다. Sphere representation은 이 `pqr.radius`
property를 실제 반지름으로 사용한다. PQR의 Poisson–Boltzmann radius를 일반
van der Waals radius와 같은 property로 모호하게 합치지 않는다. VMD PQR 0.6과 호환되는 optional
`CRYST1 a b c alpha beta gamma` record는 Å 단위 triclinic cell로 보존하며 duplicate, non-finite 또는
degenerate cell을 거부한다. Multi-model record는 암묵적으로 flatten하지 않는다.

MOL/SDF reader는 V2000 fixed-width counts/atom/bond block을 읽고 `M  CHG`, `M  ISO`, `M  RAD`를
typed chemistry로 반영한다. Single/double/triple/aromatic bond order를 구분하며 현재 core가 표현하지 못하는
unspecified bond는 단순 결합으로 낮추지 않고 오류로 거부한다. V2000 query bond type
5–8(single/double, single/aromatic, double/aromatic, any)과 bond stereo code 0/1/3/4/6은 core typed
semantics로 보존한다. SDF의 각 `$$$$`
record는 document의 독립 structure가 되고 data field는 `sdf.data.*` metadata로 보존된다. Workspace는 모든
record의 system과 scene node를 먼저 구성한 뒤 한 번에 commit하므로 duplicate name이나 build 실패가
일부 record만 남기지 않는다. Explicit `--name base`는 `base_1`, `base_2`, …로 확장된다.

MOL/SDF writer는 exactly one selected frame과 V2000의 999 atom/bond 한계를 적용한다. Formal charge,
isotope, radical, mass difference, atom parity와 구현된 bond order를 기록하며 SDF는 현재 active object 하나를
한 record로 출력한다. MOL export에서 SDF data field가 빠지거나 coordinate가 F10.4로 양자화되는 사실은
loss report에 포함한다. Duplicate SDF tag와 원래 tag 순서는 현 metadata map이 표현하지 못하므로 현재
reader는 duplicate tag를 거부하고 writer는 deterministic key order로 정규화한다.

MOL2 reader는 각 `@<TRIPOS>MOLECULE` record의 declared ATOM/BOND/SUBSTRUCTURE count를 실제 row와 먼저
대조한다. ATOM의 ID/name/Cartesian coordinate/SYBYL type/substructure/optional charge/status, BOND의
ID/endpoints/type/status, SUBSTRUCTURE chain과 `CRYSIN`을 typed topology/frame data로 보존한다. `nc`는 실제
topology bond로 만들지 않고 ID/endpoint/status provenance로 보존한다. `NO_CHARGES`와
명시적 zero charge를 `partial_charge_present`로 구분한다. Multi-molecule document는 Workspace에서 SDF와 같은
failure-atomic ordered object batch가 된다.

MOL2 writer는 exactly one selected frame과 모든 atom의 explicit `mol2.atom_type`을 요구한다. Valid positive
source atom/bond ID, original bond endpoint order, substructure ID/name/chain, charge/status, aromatic/amide bond와
retained `nc` record 및 triclinic cell을 보존한다. 일반 element/name만으로 SYBYL typing을 발명하지 않는다. Residue에서 정규화한
SUBSTRUCTURE row, 생략한 optional section과 표현하지 못하는 property는 loss report에 포함한다.

PSF reader는 첫 `PSF` header와 `NTITLE`/`NATOM`을 요구하고 standard CHARMM/X-PLOR, `NAMD`
whitespace-delimited 및 `EXT` identity 폭을 처리한다. Atom ID, segment, residue ID와 insertion, residue/atom name,
force-field atom type, elementary charge와 dalton mass를 typed topology로 만든다. `NBOND`, `NTHETA`, `NPHI`,
`NIMPHI` 및 `NCRTERM` 8-atom CMAP의 ordered atom reference를 검증하며 X-PLOR의 duplicate
dihedral/improper/CMAP term multiplicity도 보존한다.
PSF에는 좌표가 없으므로 atom count가 일치하는 zero-frame coordinate source를 명시적으로 만들고, 이후
`traj load` 또는 GUI Attach trajectory로 DCD/XTC/TRR/MDCRD/NetCDF/H5MD/RST7/LAMMPS/BINPOS를 붙인다. 좌표가 생기기 전 representation/analysis는
actionable `not_found`로 실패하고 GUI는 topology가 정상적으로 로드됐음을 별도로 표시한다.

TRR은 indexed random-access read와 current-frame write를 지원한다. Writer는 float32/64 coordinate,
optional velocity/force/triclinic cell, signed step, physical time, lambda와 nre를 portable XDR로 보존한다.
Source step/time/lambda가 없거나 force axis가 일부만 있으면 scientific zero를 발명하지 않고 실패하며,
multi-frame append와 virial/pressure/energy block은 아직 지원하지 않는다.

DCD는 32-bit Fortran record, little/big endian, X-PLOR/CHARMM delta, optional unit cell과 canonical fixed/free atom
trajectory를 indexed random-access로 읽는다. Current-frame writer는 little-endian CHARMM24 float32 coordinate와
실제로 존재하는 unit cell만 출력한다. Source step/raw delta 합성, float32 narrowing 및 저장할 수 없는
time/velocity/property는 typed loss다. 64-bit marker, 4D block과 multi-frame append는 tracked gap이다.

PSF writer는 coordinate frame을 요구하거나 출력하지 않고 `PSF EXT XPLOR` topology를 생성한다. 모든 atom에
explicit `psf.atom_type`, `partial_charge`, `mass`가 있어야 하며 일반 element로 force-field typing을 발명하지
않는다. Bond order, coordinate frame, chain/alternate-location/formal charge 및 unmodeled auxiliary section은 typed
loss report에 기록한다. CMAP이 있으면 header와 `NCRTERM`으로 보존한다. 현재 model이 보존하지 못하는 lone-pair, Drude/CHEQ data는
조용히 버리지 않고 reader에서 `unsupported`로 거부한다.

## Amber PRMTOP/RST7/MDCRD/NetCDF family

| Format | 역할 | 보존 channel | 현재 방향 |
|---|---|---|---|
| PRMTOP/parm7 | force-field topology | atom/residue name, atomic number, Amber atom type/index, charge, mass, bond/angle/proper/improper multiplicity, GB radius/screen, BOX_DIMENSIONS template, section comment provenance | read-only, zero frame |
| RST7/restrt/inpcrd | restart coordinate | one-frame Cartesian Å, optional AKMA velocity→Å/ps, time ps, temperature K, 3/6-value unit cell | `traj load` read, `traj save` current-frame write |
| MDCRD/CRD/CRDBOX | formatted trajectory | multi-frame Cartesian Å, optional 3/6-value unit cell, frame title | indexed random-access read; CRD coordinate-only 또는 CRDBOX length-preserving current-frame write |
| NetCDF | binary trajectory/restart | Cartesian Å, velocity Å/ps, force kcal/mol/Å, time ps, temperature K, triclinic cell, scale factor와 integer compression; AMBERRESTART는 single frame | netCDF-C random-access read-only, `.ncrst` 포함 |
| H5MD | HDF5 particle trajectory | SI-unit-normalized position/velocity/force, ID/presence, mass/charge/species/image, step/time, orthorhombic/triclinic box | HDF5 hyperslab random-access read-only; arbitrary observables/partial-periodic box 미지원 |
| LAMMPS dump | custom text snapshots | ID-mapped x/xs/xu/xsu coordinates, timestep/time/units, vx/vy/vz, orthogonal/restricted-triclinic cell, boundary/origin, custom atom columns | indexed random-access read-only; explicit Å/nm 및 header 일치 필요 |
| BINPOS | Scripps binary positions | Cartesian float32 Å, frame atom count, detected little/big byte order | indexed random-access read 및 canonical little-endian current-frame write; no cell/time/velocity/force |

PRMTOP parser는 `%VERSION`, `%FLAG`, `%FORMAT`의 fixed-width A/I/E/F field를 읽고 POINTERS와 실제
identity/connectivity record count를 대조한다. `CHARGE`는 Amber 공식 18.2223 scale을 elementary charge로
정규화한다. Dihedral coordinate index는 absolute-index/3 규칙으로 atom을 찾고 fourth field sign으로
proper/improper를 구분한다. `%FLAG`와 `%FORMAT` 사이의 ordered `%COMMENT`는
`amber.comment.<SECTION>` provenance로 보존하며, section 밖의 comment는 malformed input으로 거부한다.
Chamber `CHARMM_*` section과 perturbation topology는 현재 model로 축소하지 않고
`unsupported`로 거부한다. 전체 force constant/Lennard-Jones/exclusion array는 아직 model에 보존되지 않으므로
writer를 제공하지 않는다.

RST7은 active topology atom count를 요구하고 한 frame을 in-memory source로 만든다. Native velocity는
20.455 scale을 적용해 Å/ps로 만들며 source unit/scale을 frame metadata에 남긴다. 6-value box는
length/alpha/beta/gamma로 triclinic cell을 구성하고 legacy 3-value box는 90-degree cell로 처리한다.
1–2 atom restart에서 optional block이 velocity인지 box인지 구별되지 않으면 과학 값을 추정하지 않고 실패한다.
Writer는 coordinate/velocity/time/temperature/cell을 typed frame metadata에서 가져오고 velocity scale을
Amber raw unit으로 되돌린다. Fixed F12.7 precision과 저장할 수 없는 frame metadata는 loss report로 노출하며,
missing atom, untyped velocity time unit, temperature-without-time 및 ambiguous small-system output은 거부한다.

MDCRD는 title 뒤의 `3*N`개 폭 8 실수를 한 줄 최대 10개씩 읽고 open 시 frame byte offset만 보존한다.
`read_frame(i)`는 요청 frame만 다시 열어 decode하므로 coordinate memory는 O(atom count), index는 O(frame count)다.
3-value box는 length만 포함하므로 PRMTOP `BOX_DIMENSIONS`의 angle을 결합하고, topology angle이 없으면 attach를
거부한다. 6-value box는 file의 alpha/beta/gamma를 사용한다. REMD/RXSGLD header, velocity/force archive,
3원자 미만 multi-frame/box ambiguity와 compressed input은 아직 지원하지 않는다. CRD writer는 current frame의
coordinate만 Å 단위 F8.3으로 출력하고 unit cell omission을 typed loss로 반환한다. Explicit CRDBOX writer는
세 cell length를 추가하고 matching PRMTOP shared angle을 요구한다. 서로 다른 세 angle은 표현할 수 없어
거부한다. Velocity, source step/time과 auxiliary metadata는 typed loss이며 multi-frame write/append는 후속이다.

GRO reader는 title, positive atom count, fixed-width residue/atom identity와 가변 정밀도 좌표를 읽는다.
연속 frame은 atom/residue identity와 순서가 첫 frame과 같아야 하며 optional velocity는 한 frame 안에서
모든 atom에 있거나 모두 없어야 한다. 좌표는 nm, 속도는 nm/ps, title의 유효한 `t=` 값은 ps로 보존한다.
Box row는 3-value orthorhombic 및 공식 9-value triclinic 순서를 지원한다. Atom name에는 element field가
없으므로 보수적으로 추정하고 불명확한 site는 atomic number 0과 typed inference flag로 남긴다.

GRO writer는 current 또는 all frame을 쓸 수 있으며 position precision 1..15를 지원한다. Source atom number가
모두 유효하고 고유하면 보존하고 아니면 순차 번호로 정규화해 loss를 보고한다. 5-character identity와
0..99999 numbering 범위를 벗어나면 hard error다. Bond/charge와 임의 property는 GRO가 표현하지 못하므로
typed loss report에 포함한다. File writer는 frame을 순차 처리하지만 현재 GRO reader/source 자체는 전체
concatenated trajectory를 memory에 보유하므로 대형 MD trajectory에는 XTC/TRR/DCD attach path를 우선한다.
Title의 `t=`는 typed physical time과 동기화한다. Comment override도 frame별 time을 유지하며 parseable title
time만 있고 typed time이 없는 모순은 그대로 재출력하지 않고 오류로 처리한다.

G96 reader는 mandatory `TITLE` 뒤의 frame block 순서를 검증한다. 각 frame은 optional `TIMESTEP`, required
`POSITION` 또는 `POSITIONRED`, optional matching `VELOCITY`/`VELOCITYRED`, optional `BOX`로 구성된다.
Full row는 I5/name5/name5/I7 identity와 F15.9 vector를 읽고 reduced row는 F15.9 vector만 읽는다. 첫 frame이
reduced면 standalone load를 위해 unknown synthetic identity를 명시적으로 만들며 이를 element inference flag로
표시한다. Later frame의 atom count/full identity mutation, full/reduced position-velocity mode mismatch와 out-of-order
unknown block은 오류다. 좌표 nm, 속도 nm/ps, TIMESTEP time ps와 source step을 typed frame metadata로 보존한다.

G96 writer는 mandatory `TITLE` 한 번과 selected current/all frame을 full block으로 순차 출력한다. 좌표와 속도는
F15.9, TIMESTEP time은 GROMACS writer 관행의 F15.6을 사용한다. `--precision`과 fixed precision의 차이,
missing step 합성/step-only omission, source ID normalization 및 표현할 수 없는 topology/property를 loss report로
반환한다. Reader는 현재 전체 text와 frame을 memory에 보유하므로 대형 trajectory에는 indexed XTC/TRR/DCD를 우선한다.
VMD 1.4 callback과 달리 공식 GROMACS block 순서에 있는 initial `POSITIONRED`, velocity, source step과 physical
time을 보존한다. VMD callback이 추가로 인식하는 `REFPOSITION`과 compressed wrapper는 아직 지원하지 않는다.

VTF reader는 공식 VTF 2.4 syntax의 default atom template, 누적 atom property, atom ID/range, direct/chain bond,
line continuation, ordered/indexed coordinate block과 frame별 unit cell을 읽는다. Name/type/residue/segment/chain,
atomic number, altLoc/insertion, charge, radius, occupancy, B-factor와 mass는 core identity 또는 typed property로
보존하고 좌표와 cell 길이는 Å이다. 첫 frame은 모든 atom을 ordered form으로 정의해야 하며 이후 sparse indexed
또는 partial ordered frame은 이전 좌표/cell을 상속한다. 불완전한 첫 frame을 zero coordinate로 채우거나 unknown
option을 버리지 않는다. Multi-frame VTF는 load 즉시 공통 playback/cache controller에 등록되어 GUI timeline과
`traj frame`, trajectory analysis에서 별도 attach 없이 사용된다. 현재 reader는 전체 frame을 memory에 보유하며
VTF combined file만 지원한다. 독립 VSF+VCF pairing, userdata, gzip, writer와 explicit time/step은 후속 범위다.
Normative reference는 [VTF format specification](https://github.com/olenz/vtfplugin/wiki/VTF-format)과
[VMD vtfplugin documentation](https://www.ks.uiuc.edu/Research/vmd/plugins/doxygen/vtfplugin_8c-source.html)이며,
reference source는 commit `0435a0e5ccaee73dac36eda0e597260424b17906`에 고정했다. 코드를 복사하거나 vendor하지 않았다.
VMD catalog의 family version 1.3과 달리 current callback은 VTF/VSF/VCF를 각각 2.4로 등록한다. VTF는
structure·bond·timestep, VSF는 structure·bond, VCF는 host가 atom count를 제공하는 coordinate-only callback이다.
Callback timestep은 velocity를 노출하지 않고 `physical_time`을 항상 0으로 반환한다. Tcl userdata parser와 zlib
입력은 compile-time optional path이므로 core dependency로 채택하지 않았다. Author upstream은 Unlicense와
public-domain intent를 선언하지만 VMD 배포본의 plugin-tree 기본 UIUC 조건과의 관계를 임의로 단정하지 않고 두 근거를
함께 provenance에 기록했다. `.vsf`/`.vcf` auto-detection 및 topology-coordinate pairing은 MOL-03 provider contract
이후 구현할 tracked gap이며, 현재 combined `.vtf` 지원을 세 독립 format 지원으로 표시하지 않는다.

XYZ writer는 selected current frame 또는 known-count source의 모든 frame을 순차적으로 읽는다. File output은
같은 directory의 temporary file을 완전히 flush한 뒤 target을 교체하고, 취소·decode·flush·publish 실패 시
temporary file을 제거한다. Existing target은 기본적으로 보존하며 `--overwrite true`일 때만 교체한다.
Residue, connectivity, charge, typed properties, cell/time/velocity와 decimal quantization은 channel/count/message
loss table로 반환한다. Missing atom처럼 valid plain XYZ로 표현할 수 없는 상태는 export 자체를 실패시킨다.
VMD XYZ 1.3 callback이 trailing column과 later-frame label을 버리고 float32로 읽는 동작은 재현하지 않는다.
Native는 extra column, element mutation, non-finite coordinate와 invalid ordinal을 거부하고 float64를 보존한다.
VMD callback의 periodic-table mass/radius 합성은 file payload가 아니므로 source property로 위장하지 않으며, 전체
118-element shared chemistry table이 고정될 때까지 별도 tracked gap으로 유지한다.

PQR writer는 exactly one selected frame과 numeric `partial_charge`/`pqr.radius` property를 요구한다.
`pqr.radius`가 없는 다른 format object은 explicit numeric `vdw_radius`를 fallback export source로 쓸 수 있다.
없는 charge/radius를 element default로 발명하지 않는다. Connectivity, formal charge, explicit element,
altLoc/iCode/segID, 추가 property와 frame metadata는 typed loss report에 기록한다. Unit cell이 있으면
`CRYST1` geometry를 기록하고 알 수 없는 space group/Z는 `P 1`/`1`로 내보낸 사실을 typed loss로 보고한다.

## Validation과 오류

Reader는 다음을 입력 경계에서 거부한다.

- 잘못되거나 누락된 required numeric/identity field
- unknown element, invalid formal charge와 non-finite coordinate
- duplicate first-model identity/serial 및 later-model topology mutation
- partial 또는 degenerate unit cell
- unknown explicit connectivity endpoint
- nested/unclosed PDB model, malformed CIF quote/text field/loop/data item
- invalid/truncated XYZ block, non-finite coordinate, unknown element, changing frame topology와 extra column
- invalid PQR field count/number, duplicate serial, non-finite charge/coordinate, non-positive radius와 multi-model
- truncated/oversized MOL record, non-finite coordinate, invalid atom/bond index, unsupported query/stereo bond,
  invalid CHG/ISO/RAD pair, malformed/duplicate SDF data field와 embedded record delimiter
- missing/duplicate MOL2 section/ID, count-row mismatch, invalid atom/substructure/bond endpoint, non-finite charge/cell,
  unsupported dummy/query/not-connected bond와 non-comment preamble
- malformed GRO count/fixed field/precision, negative identity, non-finite coordinate/velocity, mixed velocity presence,
  invalid 3/9-value cell 및 concatenated frame identity mutation
- missing/out-of-order G96 block/END, malformed F15 vector/TIMESTEP/BOX, negative identity, position/velocity mode·count·identity
  mismatch 및 concatenated frame topology mutation
- missing/duplicate PSF section, title/atom/connectivity count mismatch, duplicate/unknown atom ID, inconsistent residue
  identity, invalid charge/mass/reference, malformed CMAP과 extra Drude/CHEQ atom field 및 unsupported non-empty lone-pair section
- missing/duplicate PRMTOP flag/format, malformed fixed field, POINTERS/section count mismatch, invalid residue pointer,
  coordinate-index/reference/parameter index, Chamber section과 perturbation topology
- malformed/truncated RST7 coordinate/optional block, non-finite value, atom-count mismatch, degenerate cell과
  small-system velocity/box ambiguity
- malformed/truncated MDCRD fixed-width row, non-finite value, topology count mismatch, missing box angle,
  inconsistent frame/box layout와 small-system ambiguity

Parse error message에는 source name과 1-based line을 포함한다. File open failure는 stable
`not_found` error로 반환하고 parse/semantic failure는 `invalid_argument`로 반환한다.

## 현재 limitation

- File API는 전체 structure text를 memory에 읽는다. Out-of-core trajectory reader와 별개이며,
  대형 mmCIF incremental tokenizer는 performance profiling 후 추가한다.
- PDB hybrid-36 serial, `ANISOU`, `LINK`/`SSBOND`, secondary-structure/assembly/transform record와
  dictionary-implied polymer bond는 아직 해석하지 않는다.
- mmCIF/BCIF의 non-atom category는 syntax/container validation만 하고 retained table로 노출하지 않는다.
  `_chem_comp_bond`, assembly와 anisotropic displacement는 아직 지원하지 않는다.
- Explicit connectivity가 없을 때 distance로 bond를 추정하지 않는다. Chemical Component
  Dictionary 기반 bond 생성은 별도 topology-enrichment 단계다.
- Incremental/bounded-memory mmCIF I/O, multi-data-block batch writer, MOL V3000, extended XYZ, property schema와 random-access XYZ index는 아직 없다. PQR은
  explicit element를 저장하지 않으며 multi-frame/PBC/connectivity를 표현하지 못한다.
- PDB writer는 fixed-column 99,999 atom/9,999 model 범위이며 hybrid-36, `ANISOU`, `TER`, `LINK`/`SSBOND`,
  `HELIX`/`SHEET`, sequence/assembly/transform, varying model cell과 full header dictionary를 아직 출력하지 않는다.
- MOL/SDF는 enhanced stereo collection, query atom, Sgroup, reaction record와 duplicate/ordered SDF
  tag를 아직 표현하지 않는다. SDF writer는 active object 한 record만 쓰며 multi-object batch export는 후속이다.
- MOL2는 dummy/query/unknown bond, FEATURE/SET/UNITY의 typed interpretation, substructure의 모든
  optional field와 multi-molecule batch export를 아직 지원하지 않는다. Writer는 explicit SYBYL type이 없는
  다른 format object를 거부하며 atom typing operation은 별도 후속 기능이다.
- PSF는 coordinates, bond order와 force-field parameter value를 저장하지 않는다. Reader는 auxiliary
  donor/acceptor/exclusion/group section의 count만 provenance로 남기며 이를 아직 analysis semantics로 사용하지
  않는다. Writer는 EXT identity 폭과 6-character atom type을 요구한다. Lone-pair, Drude/CHEQ와 CHARMM
  numeric atom-type table 연동은 후속 model 확장 범위다.
- PRMTOP reader는 standard Amber topology만 지원한다. Force parameter/Lennard-Jones/exclusion/polarization array,
  Chamber와 perturbation semantics는 아직 공통 energy model에 보존하지 않으며 PRMTOP writer도 없다.
  RST7은 single-frame in-memory read/write이고 NetCDF restart 및 1–2 atom optional-block override는 아직 없다.
  MDCRD writer, compressed stream, REMD/RXSGLD header와 small-system ambiguity override도 아직 없다.
- GRO identity는 각 5-character field로 제한되고 connectivity나 explicit element를 저장하지 않는다. Reader는
  concatenated trajectory 전체를 memory에 보유하며 random-access indexing과 streaming은 아직 없다. Malformed
  `t=` title은 title 그대로 보존하지만 물리 시간으로 만들지 않는다.
- G96는 full identity의 residue/atom name을 5자로 제한하고 explicit element/connectivity를 저장하지 않는다.
- Core의 inferred/user residue kind와 polymer classification은 현재 native structure format writer가 직접
  round-trip하지 않으며 export report의 `residue_semantics` channel로 count한다. Source residue/chain identity
  보존과 normalized chemical classification 보존은 별도 channel이다.
  Reduced first-frame load는 원래 identity를 복원할 수 없어 synthetic unknown atom을 사용한다. Reader는 in-memory이며
  gzip/`.Z`, incremental indexing과 external-topology POSITIONRED mapping은 아직 없다.

따라서 현재 support는 registry에 적힌 channel/direction만 의미하며 extension 이름만으로 완전한
round-trip을 주장하지 않는다.

## Normative reference

- [wwPDB PDB format 3.3 introduction](https://www.wwpdb.org/documentation/file-format-content/format33/sect1.html)
- [wwPDB PDB coordinate records](https://www.wwpdb.org/documentation/file-format-content/format33/sect9.html)
- [wwPDB PDB crystallographic records](https://www.wwpdb.org/documentation/file-format-content/format33/sect8.html)
- [wwPDB PDB format 3.3 full index](https://www.wwpdb.org/documentation/file-format-content/format33/v3.3.html)
- [wwPDB PDBx/mmCIF syntax](https://mmcif.wwpdb.org/docs/tutorials/mechanics/pdbx-mmcif-syntax.html)
- [wwPDB PDBx/mmCIF coordinate section](https://mmcif.wwpdb.org/docs/user-guide/resources/coordinate_section.html)
- [BinaryCIF format and encoding specification](https://github.com/molstar/BinaryCIF/blob/master/encoding.md)
- [IUCr CIF 1.1 syntax](https://www.iucr.org/resources/cif/spec/version1.1/cifsyntax)
- [Open Babel XYZ format documentation](https://openbabel.org/docs/FileFormats/XYZ_cartesian_coordinates_format.html)
- [libAtoms extended XYZ specification](https://github.com/libAtoms/extxyz)
- [PDB2PQR PQR molecular structure format](https://pdb2pqr.readthedocs.io/en/v3.3.0/formats/pqr.html)
- [APBS molecular structure input formats](https://apbs.readthedocs.io/en/stable/formats/)
- [APBS OpenDX scalar data format](https://apbs.readthedocs.io/en/stable/formats/opendx.html)
- [CCP-EM MRC2014 specification](https://www.ccpem.ac.uk/mrc-format/mrc2014/)
- [CCP4 MAPLIB map-format documentation](https://www.ccp4.ac.uk/html/maplib.html)
- [BIOVIA CTfile Formats 2020](https://discover.3ds.com/sites/default/files/2020-08/biovia_ctfileformats_2020.pdf)
- [NIH CHARMM MOL2 technical description](https://hpcwebapps.cit.nih.gov/apps/charmm/c42b2html/mmff.html)
- [UCSF Chimera MOL2 save semantics](https://www.rbvi.ucsf.edu/chimera/docs/UsersGuide/savemodel.html)
- [OVITO MOL2 reader documentation](https://www.ovito.org/manual/reference/file_formats/input/mol2.html)
- [GROMACS file-format reference](https://manual.gromacs.org/documentation/current/reference-manual/file-formats.html)
- [GROMACS G96 I/O implementation reference](https://github.com/gromacs/gromacs/blob/main/src/gromacs/fileio/g96io.cpp)
- [NAMD 3.0 file formats](https://www.ks.uiuc.edu/Research/namd/3.0/ug/node11.html)
- [NAMD tutorial PSF field and connectivity semantics](https://www.ks.uiuc.edu/Training/SumSchool/materials/sources/tutorials/02-namd-tutorial/namd-tutorial-html/node21.html)
- [VMD open-source PSF molfile plugin reference](https://www.ks.uiuc.edu/Research/vmd/plugins/doxygen/psfplugin_8c-source.html)
- [VMD plugin license policy](https://www.ks.uiuc.edu/Research/vmd/plugins/)
- [Amber official file formats: PRMTOP and coordinate/restart](https://ambermd.org/FileFormats.php)
- [Amber-MD CPPTRAJ Amber restart implementation reference](https://github.com/Amber-MD/cpptraj/blob/master/src/Traj_AmberRestart.cpp)

Synthetic fixtures는 MolShredder용으로 직접 작성했으며 외부 structure data를 복사하지 않았다.
