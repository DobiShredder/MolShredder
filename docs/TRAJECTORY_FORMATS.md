# Trajectory format support

상태: DCD/TRR/XTC foundation reader contract  
검증 기준일: 2026-08-14

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

Public `TrrMetadata`의 channel boolean은 적어도 한 frame에 해당 channel이 존재함을 뜻한다. Frame별
optional channel은 `CoordinateFrame`에서 다시 확인해야 한다. Header block size, atom-count stability,
precision과 payload boundary를 index 단계에서 검증한다.
Box block의 3×3 행렬이 모두 0이면 GROMACS의 non-periodic/infinite-cell 표현으로 간주해 unit cell을
비워 둔다. 일부만 퇴화하거나 non-finite인 행렬은 오류다.

현재 coordinate model은 positions를 필수로 하므로 positionless TRR frame을 거부한다. 현대 trajectory
frame에 쓰이지 않는 `ir/e/top/sym` legacy block도 조용히 건너뛰지 않고 unsupported input으로 거부한다.
Virial과 pressure block은 size를 검증하고 건너뛰지만 아직 model property로 노출하지 않는다. Writer,
append, lambda-specific semantics와 file mutation detection은 후속 범위다.

TRR 구현은 별도 runtime library를 링크하거나 GROMACS/standalone xdrfile source를 복사하지 않은 native
reader다. Format semantics와 unit은 다음 공개 자료를 기준으로 했다.

- [GROMACS file-format reference](https://manual.gromacs.org/current/reference-manual/file-formats.html)
- [GROMACS source repository](https://gitlab.com/gromacs/gromacs)
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
