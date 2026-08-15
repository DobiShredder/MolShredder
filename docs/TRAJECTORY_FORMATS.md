# Trajectory format support

상태: DCD/TRR/XTC/MDCRD/Amber NetCDF/H5MD indexed trajectory와 RST7 restart read/write contract
검증 기준일: 2026-08-16

MolShredder의 첫 out-of-core trajectory source는 CHARMM/NAMD/X-PLOR DCD를 읽는다. Public API는
`molshredder/io/trajectory_reader.hpp`의 `open_dcd()`와 `DcdCoordinateSource`다. Topology는 DCD에
포함되지 않으므로 caller가 별도 structure를 load하고 expected atom count를 전달해야 한다.

## DCD capability

| Channel | 현재 지원 |
|---|---|
| Byte order | little-endian, big-endian 자동 감지 |
| Record marker | 32-bit Fortran record marker |
| Dialect | CHARMM/NAMD float delta, X-PLOR double delta |
| Coordinates | frame별 X/Y/Z float32, Å 단위 |
| Unit cell | CHARMM extra 48-byte cell record, angle 또는 cosine encoding |
| Frame identity | header start step + frame index × save interval |
| Access | open 시 frame offset index 생성, 이후 random seek |
| Memory | index O(frame count), decode O(atom count); 전체 trajectory를 적재하지 않음 |

Unit cell의 DCD 순서 `A, gamma, B, beta, alpha, C`를 세 lattice vector로 변환하므로 orthogonal 및
triclinic cell을 표현한다. Angle 세 값이 모두 [-1, 1]이면 cosine encoding으로 해석하고 아니면
degree로 해석한다. Invalid/degenerate cell은 조용히 버리지 않고 frame decode error로 반환한다.

Reader는 header/title/atom count, 모든 leading/trailing record marker와 indexed frame count를
검증한다. Expected topology atom count mismatch, truncated record, non-finite coordinates/cell,
unsupported 4D 및 fixed-atom trajectory를 명시적으로 거부한다. Header의 raw delta는 dialect에 맞게
보존하지만 DCD variant마다 timestep 의미가 다를 수 있어 foundation에서는 physical-time unit으로
추정하지 않는다.

## I/O와 lifetime

Open은 파일을 한 번 순회해 frame offset만 저장한다. `read_frame(i)`는 파일을 다시 열고 해당 offset로
seek한 뒤 unit cell과 세 coordinate array만 decode해 immutable `CoordinateFrame` lease를 반환한다.
따라서 동시에 보유한 frame lease 수만큼만 coordinate memory가 유지된다. 현재 매 read마다 file을
여는 구현은 correctness-first foundation이다. 별도 `FrameCache`가 bounded LRU와 cancellable async
prefetch를 제공하며 persistent file handle과 production worker scheduling은 후속 최적화다.

## 현재 limitation

- 64-bit Fortran record marker와 2^30-scale huge-file marker workaround
- Fixed-atom/free-index DCD, 4D coordinate block과 velocity block
- Corrupted marker recovery, writer와 append
- XTC writer/append와 compressed payload recovery
- Persistent concurrent file-handle policy와 production decoder pool
- Raw delta의 engine-specific physical-time conversion

지원 범위는 DCD 확장자 전체가 아니라 위 matrix로 한정한다. Synthetic DCD는 test 실행 중 직접
생성되며 repository에 외부 trajectory binary를 포함하지 않는다.

## Normative/reference material

- [VMD molfile plugin format inventory](https://www.ks.uiuc.edu/Research/vmd/plugins/molfile/)
- [VMD dcdplugin source documentation](https://www.ks.uiuc.edu/Research/vmd/plugins/doxygen/dcdplugin_8c-source.html)
- [VMD molfile plugin license](https://www.ks.uiuc.edu/Research/vmd/plugins/pluginlicense.html)

구현 코드는 공식 문서와 공개된 binary record behavior를 기준으로 독립 작성했으며 VMD plugin source를
repository에 복사하거나 vendor하지 않았다.

## TRR capability

`open_trr()`는 GROMACS portable XDR TRR을 open할 때 frame offset을 index하고 이후 요청 frame만
decode한다. XDR scalar는 network byte order이며 다음 channel을 지원한다.

| Channel | 현재 지원 |
|---|---|
| Precision | frame별 float32 또는 float64, trajectory의 mixed precision 표시 |
| Coordinates | nm에서 Å로 변환, 원본 precision 유지 |
| Velocities | nm/ps에서 Å/ps로 변환, optional |
| Forces | kJ mol^-1 nm^-1에서 축별 kJ mol^-1 Å^-1 atom property로 변환, optional |
| Unit cell | 3×3 row-major triclinic basis를 nm에서 Å로 변환, optional |
| Frame identity | signed simulation step, physical time(ps), lambda와 nre |
| Access | open 시 O(frame count) offset index, decode O(atom count) random seek |
| Write | current frame의 float32/64 coordinate, optional velocity/force/cell과 step/time/lambda/nre |

Public `TrrMetadata`의 channel boolean은 적어도 한 frame에 해당 channel이 존재함을 뜻한다. Frame별
optional channel은 `CoordinateFrame`에서 다시 확인해야 한다. Header block size, atom-count stability,
precision과 payload boundary를 index 단계에서 검증한다.
Box block의 3×3 행렬이 모두 0이면 GROMACS의 non-periodic/infinite-cell 표현으로 간주해 unit cell을
비워 둔다. 일부만 퇴화하거나 non-finite인 행렬은 오류다.

현재 coordinate model은 positions를 필수로 하므로 positionless TRR frame을 거부한다. 현대 trajectory
frame에 쓰이지 않는 `ir/e/top/sym` legacy block도 조용히 건너뛰지 않고 unsupported input으로 거부한다.
Virial과 pressure block은 size를 검증하고 건너뛰지만 아직 model property로 노출하지 않는다.

`traj save --file-format trr`는 current frame을 portable big-endian XDR로 쓴다. Positions와 optional
velocity는 typed coordinate/time unit에서 nm와 nm/ps로 변환하고, `force_x/y/z` 전체가
`kJ mol^-1 angstrom^-1`일 때 force block을 `kJ mol^-1 nm^-1`로 되돌린다. Coordinate, velocity 또는
force 중 하나라도 float64이면 전체 frame을 float64로 쓰고 그 외에는 float32다. Source step, physical
time과 `trr.lambda`는 필수이며 누락 시 0을 발명하지 않고 실패한다. Partial force triplet, missing atom,
non-finite 값과 32-bit header 한계도 오류다. Auxiliary property/metadata와 float32 time/lambda/cell
narrowing은 typed loss로 반환한다. Multi-frame append, virial/pressure/energy write와 file mutation
detection은 후속 범위다.

TRR 구현은 별도 runtime library를 링크하거나 GROMACS/standalone xdrfile source를 복사하지 않은 native
reader/writer다. Format semantics와 unit은 다음 공개 자료를 behavior reference로 사용했으며 source나
support header를 복사·link·bundle하지 않았다.

- [GROMACS file-format reference](https://manual.gromacs.org/current/reference-manual/file-formats.html)
- [GROMACS source repository](https://gitlab.com/gromacs/gromacs)
- [VMD current gromacsplugin registration/source](https://www.ks.uiuc.edu/Research/vmd/plugins/doxygen/gromacsplugin_8C-source.html)
- [VMD legacy TRR plugin notes](https://www.ks.uiuc.edu/Research/vmd/plugins/molfile/trrplugin.html)
- [Chemfiles supported-format matrix](https://chemfiles.org/chemfiles/latest/formats.html) (교차검증용)

## XTC capability

`open_xtc()`는 GROMACS XTC의 fixed-size uncompressed frame(9 atoms 이하)과 compressed 3D integer
coordinates를 읽는다. Topology는 포함되지 않으므로 caller가 expected atom count를 제공할 수 있다.

| Channel | 현재 지원 |
|---|---|
| Variant | legacy magic 1995, 64-bit payload-length magic 2023, mixed file 표시 |
| Coordinates | XDR float 또는 compressed integer bitstream, nm→Å float32 |
| Precision | compressed frame의 scale factor를 `xtc.precision` metadata로 보존 |
| Unit cell | float32 3×3 triclinic basis nm→Å; all-zero는 non-periodic |
| Frame identity | signed step, physical time(ps) |
| Access | variable frame payload를 open 시 O(frame count) index하고 O(atom count) random decode |

Decoder는 coordinate range, bit count, small-coordinate table index, run length, integer addition,
payload length/padding과 file boundary를 검증한다. Water처럼 인접한 coordinate를 swap하는 XTC run
encoding 및 2²⁴를 넘는 축 범위의 개별 bit-width path도 지원한다. 전체 trajectory coordinate를
memory에 적재하지 않으며 decode된 frame lease는 공통 `FrameCache`에 넣을 수 있다.

현재 writer/append, 손상 payload recovery, 1995 format의 약 2.98억 atom 한계에 근접한 실제 파일 및
2023 long format의 초대형 payload stress test는 없다. Open index는 frame envelope를 검증하지만 모든
compressed bitstream은 해당 frame을 처음 decode할 때 검증한다.

XTC integer decompression은 permissive source를 투명하게 재사용한 부분이다. xdrfile 1.1.4
(BSD-2-Clause) 알고리즘과 Chemfiles commit
`51a17e85027c7b31ef3ae1531ed42281e578e513`(BSD-3-Clause)의 modern bounds/long-format 처리에서
adapt했으며 전체 고지는 `THIRD_PARTY_NOTICES.md`에 보존한다. VMD main source는 사용하지 않았다.

## Amber RST7 capability

`open_amber_restart()`는 `.rst7/.restrt/.inpcrd/.inprst` ASCII restart 한 frame을 읽으며
`open_trajectory()`와 `traj load`에도 `TrajectoryFormat::rst7`으로 연결된다. 현재 frame은
`serialize_trajectory_frame()` 또는 failure-atomic `write_trajectory_frame_file()`로 다시 쓸 수 있다.

| Channel | 현재 지원 |
|---|---|
| Coordinates | F12.7 Cartesian Å, float64 |
| Velocities | optional AKMA value ×20.455 → Å/ps, source scale metadata |
| Frame metadata | optional time ps와 temperature K |
| Unit cell | 3 lengths(90°) 또는 3 lengths+alpha/beta/gamma |
| Access/memory | known one frame, in-memory random access; current-frame write |

Active topology의 atom count와 NATOM을 attach 전에 대조한다. Coordinate 뒤 optional payload는
none, velocity, box 또는 velocity+box만 허용하며 incomplete data를 거부한다. NATOM 1–2에서는 trailing
3/6 value가 velocity와 box 중 어느 것인지 형식만으로 구별되지 않는 경우가 있어 추정 대신 실패한다.
Writer는 Cartesian coordinate, optional velocity/time/temperature/cell을 원래 typed metadata에 따라
F12.7 restart로 기록한다. Å/ps velocity는 Amber raw scale로 역변환하고 orthogonal cell은 length 3개,
triclinic cell은 length/angle 6개를 쓴다. 없는 velocity, time 또는 cell을 0으로 발명하지 않는다.
Temperature만 있고 time이 없거나 writer output도 1–2 atom optional-block ambiguity를 만들면 hard error다.
Fixed precision, source step과 auxiliary property/metadata omission은 typed loss report로 반환하며 overwrite,
cancellation 또는 I/O 오류는 partial target을 publish하지 않는다. NetCDF restart와 ambiguity override는 아직 없다. Normative layout과 unit은
[Amber official file formats](https://ambermd.org/FileFormats.php)의 coordinate/restart specification을 따른다.

## Amber MDCRD capability

`open_amber_ascii_trajectory()`는 `.mdcrd/.crd` formatted Amber trajectory를 matching topology atom count로
index하며 `open_trajectory()`와 `traj load`의 `mdcrd|crd` 선택에 연결된다.

| Channel | 현재 지원 |
|---|---|
| Coordinates | 폭 8 fixed field, 줄당 최대 10개 Cartesian Å float64 |
| Unit cell | optional 3 lengths + PRMTOP angle 또는 3 lengths+alpha/beta/gamma |
| Frame metadata | title; format에는 step/time이 없음 |
| Access/memory | open 시 O(frame count) byte-offset index, decode O(atom count) random seek |

각 coordinate line의 정확한 field 수, finite value, frame 전체의 box layout과 topology atom count를 검증한다.
3-value box를 90°로 추정하지 않고 PRMTOP `BOX_DIMENSIONS` angle을 요구한다. 3원자 미만에서는 다음 coordinate
line과 3/6-value box를 안전하게 구별할 수 없어 multi-line input을 명시적으로 거부한다. REMD/HREMD/RXSGLD
header, compressed input, MDVEL/MDFRC semantic과 writer/append는 후속 범위다.

Normative layout은 [Amber official file formats](https://ambermd.org/FileFormats.php)을 사용했고,
[Amber-MD CPPTRAJ formatted trajectory implementation](https://github.com/Amber-MD/cpptraj/blob/master/src/Traj_AmberCoord.cpp)은
frame packing과 box detection behavior를 교차 확인하는 reference로만 사용했다. 코드는 독립 구현이며 CPPTRAJ
source를 복사하거나 vendor하지 않았다.

## Amber NetCDF capability

`open_amber_netcdf()`는 Amber NetCDF convention 1.0 trajectory를 netCDF-C public API로 열고,
`open_trajectory()` 및 `traj load`의 `netcdf|nc|ncdf` 선택과 `.nc/.ncdf/.netcdf` 자동 감지에 연결된다.
Topology는 포함하지 않으므로 active topology atom count와 `atom` dimension을 attach 전에 대조한다.

| Channel | 현재 지원 |
|---|---|
| Container | CDF-1 classic, CDF-2 64-bit offset, CDF-5 64-bit data, NetCDF-4/HDF5 |
| Coordinates | `coordinates(frame,atom,spatial)` float32/float64 Å 또는 `compressedpos` NC_INT/`icompressfac` |
| Velocities | optional Å/ps standard 또는 integer-compressed channel |
| Forces | optional kcal/mol/Å standard 또는 integer-compressed channel; frame atom property `force.x/y/z` |
| Frame metadata | optional time ps, temperature K와 source program/title/application |
| Unit cell | paired length Å와 alpha/beta/gamma degree로 구성한 full triclinic cell |
| Access/memory | persistent read-only handle, O(atom count) requested-frame decode, global netCDF API serialization |

Reader는 `Conventions=AMBER`, `ConventionVersion=1.0`, non-zero frame/atom, `spatial=3`와 label `x,y,z`,
각 variable의 exact dimension order와 physical unit을 검증한다. Standard channel의 `scale_factor`는 stored value에
곱하고 CPPTRAJ integer compression은 stored integer를 positive `icompressfac`로 나눈다. `_FillValue`, default fill,
non-finite value, zero scale, standard/compressed duplicate와 cell length/angle의 단독 존재를 오류로 처리한다.
netCDF API는 distribution별 thread-safety 차이를 감추기 위해 process-wide mutex로 직렬화한다.

현재 REMD replica metadata, NetCDF restart convention, partial-periodic cell, writer/append와 remote dataset은
노출하지 않는다. Runtime은 netCDF-C와 선택된 NetCDF-4/HDF5 dependency closure가 필요하며 installer는 OS별 exact
artifact와 license/SBOM을 수집해야 한다. Convention과 variable semantics는
[AmberTools manual](https://ambermd.org/AmberTools.php), container/API는
[Unidata netCDF-C documentation](https://docs.unidata.ucar.edu/netcdf-c/current/)을 normative reference로 사용했다.
CPPTRAJ commit `19a0bb7fd63396bcf274df34bf51f55e9f5db671`은 integer compression과 optional channel behavior를
교차 확인하는 reference로만 사용했으며 source를 복사하거나 vendor하지 않았다.

## H5MD 1.x capability

`open_h5md()`는 HDF5 C API로 H5MD 1.x particle trajectory를 열고 `open_trajectory()`, `.h5md` suffix와
`traj load --file-format h5md`에 연결된다. `/h5md/version`, `author/name`, `creator/name`을 검증하고
`/particles/<name>` 아래에서 명시한 group을 선택한다. Group을 생략하면 `trajectory`를 우선하고, 그것도
없을 때 particle group이 정확히 하나여야 한다. 여러 group을 임의로 합치거나 첫 group을 선택하지 않는다.

| Channel | 현재 지원 |
|---|---|
| Coordinates | static `(N,3)` 또는 time-dependent `(T,N,3)` float32/float64 position |
| Timeline | explicit integer `step(T)`와 optional `time(T)`, 또는 fixed scalar increment와 optional offset |
| Atom data | static/time-dependent ID, presence, velocity, force, mass, charge, species와 integer image vector |
| Unit cell | 3D orthorhombic length vector 또는 triclinic row-vector edges, static/time-dependent |
| Access/memory | persistent read-only HDF5 handles, requested-frame hyperslab decode O(storage atom count), process-wide API serialization |

H5MD `unit`은 SI base unit symbol, signed integer power, multiplication/division과 numeric scale factor로
해석한다. Position/box는 Å, time은 ps, velocity는 Å/ps로 정규화하며 force는 원래 unit provenance와 함께
typed frame property로 보존한다. Position에 unit이 없으면 과학 값을 추정하지 않고
`--coordinate-unit angstrom|nanometer`를 요구한다. Unit attribute와 override가 동시에 있으면 file metadata가
authoritative하다.

`id`가 있으면 각 frame의 source particle ID를 active topology의 complete unique `source_serial`에 배치한다.
Dynamic ID와 fill value로 storage slot이 바뀌어도 identity를 유지하며 duplicate/unknown ID는 실패한다.
Coordinate fill 또는 `presence=false`인 topology atom은 frame presence mask로 남긴다. Velocity, force, mass,
charge, species와 image는 각 element의 source step이 position step과 일치할 때만 해당 frame에 붙이며,
일치하지 않는 optional sample을 가까운 frame으로 보간하지 않는다. ID와 box timeline은 identity/PBC를
결정하므로 필요한 step이 없으면 frame decode를 실패시킨다.

Box boundary는 세 축 모두 `periodic`이거나 모두 `none`인 경우만 지원한다. 현재 `UnitCell`이 축별 주기성을
표현하지 못하므로 mixed boundary를 full periodic으로 잘못 축소하지 않고 거부한다. Arbitrary
`/observables`, 링크된 여러 particle group, writer/append, variable particle storage width와 partial-periodic
cell은 후속 범위다. External HDF5 link 및 external/virtual dataset storage는 같은 파일처럼 보이면서 다른
경로를 읽는 것을 막기 위해 거부한다.

Schema와 unit semantics는 [H5MD specification 1.1 work version](https://h5md.nongnu.org/h5md.html)과
[H5MD units module](https://h5md.nongnu.org/modules/units.html)을 normative reference로 사용했다. 구현은
H5MD application source를 복사하지 않은 native reader다. Runtime은 HDF5 library와 compression filter closure가
필요하며 최종 installer는 OS별 binary, license와 SBOM을 함께 수집해야 한다.

## LAMMPS custom text dump capability

`open_lammps_dump()`는 `.lammpstrj/.lammpstraj/.dump`의 `ITEM:` snapshot offset만 저장하고 요청 frame을
다시 seek/decode한다. LAMMPS는 atom row를 snapshot마다 정렬한다고 보장하지 않으므로 `ITEM: ATOMS id ...`를
필수로 요구하며, 그 ID set을 active topology의 complete unique source serial에 정확히 매핑한다. Row order를
atom identity로 추정하는 fallback은 없다.

| Channel | 현재 지원 |
|---|---|
| Coordinates | wrapped Cartesian `x/y/z`, scaled wrapped `xs/ys/zs`, unwrapped Cartesian `xu/yu/zu`, scaled unwrapped `xsu/ysu/zsu` 중 snapshot당 정확히 하나 |
| Cell | orthogonal bounds 또는 restricted-triclinic `xy/xz/yz`; true bounds와 lattice vector 복원 |
| Identity/state | positive unique atom `id`, non-negative timestep, boundary flags와 non-zero box origin |
| Custom columns | coordinate/id 외 column을 frame별 typed integer/float64/text atom property로 보존 |
| Access/memory | open 시 O(frame count) offset index와 O(atom count) validation, random decode O(atom count) |

Text dump에는 simulation `units` style이 기록되지 않으므로 generic `open_trajectory()`와 `traj load`는
`coordinate-unit=angstrom|nanometer`를 명시하도록 요구한다. SI/CGS/micro/electron/LJ reduced unit을 Å 또는
nm로 몰래 해석하지 않는다. General triclinic `BOX BOUNDS abc origin`, ID가 없는 dump, atom subset/count 변화,
복수 coordinate triplet과 multi-file `%`/`*` sequence는 현재 hard unsupported다. Velocity/force/custom compute
column은 값과 원래 이름을 보존하지만 time/force unit을 알 수 없으므로 typed velocity/force channel로
승격하지 않는다.

Grammar와 box 변환은 [LAMMPS dump command documentation](https://docs.lammps.org/dump.html) 및
[LAMMPS triclinic box documentation](https://docs.lammps.org/Howto_triclinic.html)을 normative reference로
사용했다. 구현은 외부 reader source를 복사하거나 vendor하지 않은 native parser다.

## Scripps BINPOS capability

`open_binpos()`는 `.binpos`의 four-byte `fxyz` magic 뒤 반복되는 signed 32-bit atom count와 `3*N`
float32 Cartesian coordinate record를 읽는다. Active topology count와 첫 record를 little/big-endian으로 각각
해석해 byte order를 결정하고 모든 frame count 및 exact file-size multiple을 open 시 검증한다. Frame offset만
보존하며 요청 frame payload만 reopen/seek/decode한다.

| Channel | 현재 지원 |
|---|---|
| Coordinates | Cartesian float32 Å |
| Identity | active topology order와 매 frame atom count |
| Access/memory | fixed-size O(frame count) offset index, random decode O(atom count) |
| Portability | little/big-endian detection과 conversion |

BINPOS에는 unit cell, source step, physical time, velocity/force, title 또는 unit marker가 없다. CPPTRAJ/Scripps
behavioral convention에 따라 coordinates를 Å로 해석하되 나머지 metadata를 발명하지 않는다. Compressed input,
varying atom count, byte-order-ambiguous atom count, non-finite/truncated payload와 writer/append는 현재 지원하지 않는다.

Layout은 Amber-MD CPPTRAJ의 공식 공개
[`Traj_Binpos.cpp` at commit `19a0bb7`](https://github.com/Amber-MD/cpptraj/blob/19a0bb7fd63396bcf274df34bf51f55e9f5db671/src/Traj_Binpos.cpp)와
[pytraj supported-format table](https://amber-md.github.io/pytraj/latest/read_and_write.html)을 reference로 확인했다.
Reader는 source를 복사/vendor하지 않고 독립 구현했으며 CPPTRAJ보다 추가로 cross-endian validation을 제공한다.
