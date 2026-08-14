# MolShredder

MolShredder는 구조 시각화, molecular editing, 고품질 rendering과 대규모 molecular dynamics
trajectory 분석을 하나의 desktop application에 통합하는 고성능 molecular viewer다.

## 핵심 목표

- PyMOL 수준의 세련된 molecular representation과 publication workflow
- VMD 수준의 MD trajectory playback, PBC와 time-series analysis
- COM, centroid, distance, RMSD 등 빈번한 계산을 GUI·CLI·Python에 기본 내장
- GUI로 표현 가능한 operation을 최대한 편입하고 workflow 중심 menu, context panel과 searchable command palette 제공
- UI·UX·CLI 전반의 안전한 default, 일관된 용어, actionable error와 progressive disclosure
- topology와 trajectory를 분리한 bounded-memory streaming
- macOS, Linux, Windows 동시 지원
- 각 platform에서 개발 도구 없이 사용할 수 있는 installer 제공

PyMOL과 VMD는 capability 및 behavior reference이며 MolShredder는 두 제품과 별개의 독립
프로젝트다. 제품명, 로고, UI 자산 또는 제한 source를 복제하는 것을 목표로 하지 않는다.

## 현재 상태

프로젝트 초기 설계와 C++20/CMake 기반 typed operation/frontend scaffold가 준비된 상태다. 현재
executable은 registry 기반 help, version command와 interactive console을 제공한다. 같은 C++
dispatcher를 toolkit-independent GUI action adapter와 실제 CPython extension module도 호출한다.
Alias는 실행 전에 canonical command로 확장되어 history에 재현 가능한 형태로 저장된다.
Immutable topology, typed atom property, coordinate frame/source와 molecular-system foundation도
구현됐다. PDB 3.3, PDBx/mmCIF/BinaryCIF 0.3.x, APBS-compatible PQR, MDL MOL/SDF V2000, Tripos MOL2, GROMACS GRO,
CHARMM/NAMD PSF, Amber PRMTOP/RST7/MDCRD/NetCDF, H5MD, LAMMPS custom text dump, Scripps BINPOS, GROMOS-96 G96,
VMD VTF와 plain multi-frame XYZ native
reader가 synthetic fixture에서 동작한다. PQR은 partial charge/radius를 typed property로 보존하고 sphere에
적용한다. MOL/SDF는 formal charge, isotope, radical 및 single/double/triple/aromatic bond를 보존하며
multi-record SDF를 failure-atomic object batch로 읽는다. MOL2는 SYBYL atom type, partial charge presence,
substructure, aromatic/amide bond와 triclinic `CRYSIN`을 보존하고 여러 molecule을 atomic batch로 읽는다.
GRO는 가변 정밀도 nm 좌표, optional nm/ps 속도, ps 물리 시간과 3/9-value unit cell을 보존하며
stable atom identity의 concatenated frame을 하나의 trajectory로 읽는다. G96은 ordered full/reduced
F15.9 block의 identity, 좌표, 속도, step/time과 triclinic box를 보존한다. PSF는 force-field atom type,
charge, mass와 ordered connectivity를 가진 zero-frame topology로 읽고 DCD/XTC/TRR/MDCRD/NetCDF/H5MD/RST7/LAMMPS/BINPOS attach를 지원한다.
PRMTOP은 Amber atom type/index, 18.2223 charge scale, mass, GB radius/screen과 signed proper/improper
connectivity를 zero-frame topology로 보존한다. RST7은 one-frame 좌표, AKMA velocity, time/temperature와
3/6-value triclinic cell을 active topology에 붙인다. MDCRD는 fixed-width multi-frame 좌표를 offset-indexed
source로 열고 PRMTOP `BOX_DIMENSIONS` angle과 trajectory box length를 결합한다.
Amber NetCDF convention 1.0은 CDF-1/2/5와 NetCDF-4/HDF5 container에서 좌표, velocity, force,
time, temperature와 triclinic cell을 random-access로 읽고 scale factor 및 CPPTRAJ integer compression을 해제한다.
H5MD 1.x는 임의의 particle group에서 static/time-dependent position, velocity, force, mass, charge,
species, image, ID/presence와 orthorhombic/triclinic box를 HDF5 hyperslab random access로 읽는다. SI unit
expression을 Å·ps 기준으로 정규화하고 source ID로 topology 순서를 복원하며, position unit이 없으면 사용자가
Å 또는 nm를 명시한다. 외부 HDF5 link/storage와 부분 주기 box는 명시적으로 거부한다.
LAMMPS dump는 snapshot offset만 index하고 atom row 순서 대신 `id`를 topology source serial에 매핑한다.
Orthogonal/restricted-triclinic box와 `x/xs/xu/xsu` 좌표를 지원하며 dump에 없는 Å/nm 단위는 사용자가 명시한다.
Scripps BINPOS는 `fxyz` 뒤 atom count와 float32 Cartesian Å record를 indexed source로 열며 topology atom
count를 이용해 little/big-endian을 구분한다. Format에 없는 cell, step/time과 velocity/force는 생성하지 않는다.
PDB/mmCIF/MOL/SDF/MOL2/PSF/PQR/XYZ/GRO/G96 writer는 selected frame 또는 topology를
failure-atomic output에 쓰고 표현하지 못하는 channel을 typed semantic-loss table로 반환한다.
BinaryCIF는 MessagePack container와 ByteArray/FixedPoint/IntervalQuantization/RunLength/Delta/
IntegerPacking/StringArray encoding 및 column mask를 native decode하고 기존 PDBx topology builder를 공유한다.
APBS-compatible ASCII OpenDX regular scalar grid는 origin, 세 개의 skewed delta vector, z-fastest scalar
ordering과 float32/float64 precision을 typed volume scene object로 읽는다. GUI, CLI와 Python에서 같은
`volume load/list` operation을 사용하지만 isosurface/slice/direct-volume rendering은 아직 구현되지 않았다.
MRC2014/CCP4 map은 little/big-endian scalar mode 0/1/2/6/12, extended header offset, axis permutation,
triclinic cell, grid start와 physical origin을 같은 typed grid로 읽는다. Standard가 handedness를 완전히
정의하지 않는 사실과 complex/packed/skew mode 제한도 metadata/capability에 노출한다.
schema v2는 24개 native format의 read/write, multi-frame/multi-structure, volume channel과 streaming limitation을
machine-readable하게 제공한다.
Immutable scene object tree와 renderer-independent orbit/pan/dolly camera foundation도 구현됐다.
Lines/sticks/spheres와 protein ribbon/cartoon의 backend-neutral instance/indexed-mesh packet,
atom/bond/residue picking 및 결정론적 headless CPU reference renderer도 구현됐다. Cartoon은 독립
STRIDE-method v0 state, chain-break와 orientation continuity를 사용한다. Qt/Metal viewport는 indexed
cartoon mesh와 GPU-instanced line/stick/sphere를 실제로 렌더링한다. Click GPU ID pass는 atom/bond/residue를
비동기 readback해 canonical `picked` named selection과 QML feedback으로 연결한다. Hover/highlight,
production object hierarchy/analysis UI, nucleic-acid cartoon, broad format writers와 installer는 아직 구현되지 않았다.
여러 structure를 load하면 Objects panel에서 active object와 visibility를 바꿀 수 있으며 renderer는 모든
visible object representation을 한 scene에 합성한다. 같은 상태 전환은 `object list/activate/visibility`
command와 Python API에도 제공된다.
Desktop trajectory panel은 active topology에 DCD/XTC/TRR/MDCRD/NetCDF/H5MD/RST7/LAMMPS/BINPOS를 attach하고 zero-based seek, first/last step,
play/pause, once/loop/rock, forward/reverse와 FPS를 같은 canonical trajectory action으로 제어한다. Qt timer는
경과 milliseconds만 `traj tick`에 전달하며 frame이 실제로 바뀔 때만 composite render packet을 교체한다.
Boolean/field predicate와 명시적 `@name` reference를 가진 foundation atom
selection parser/evaluator 및 static/dynamic named-selection lifecycle도 구현됐다.
Raw-coordinate 단일 frame의 geometric centroid, provenance-aware COM 및 atom-distance C++ kernel도
구현됐다. Explicit mass가 없을 때는 versioned CIAAW 기반 estimated element mass를 사용한다.
Stateful Workspace의 `load`, `select`, `show`, `analyze center`, `measure distance`가 GUI action,
native console과 Python에서 같은 reader, selection evaluator, representation 및 analysis kernel을
호출한다. Toolkit-independent Analyze presentation model은 같은 result envelope에서 center point와
distance atom-anchor marker, label, structured fields 및 text/JSON view를 생성한다.
Out-of-core trajectory source는 little/big-endian CHARMM/NAMD/X-PLOR DCD, float/double XDR TRR 및
compressed/uncompressed XTC의 frame index와 random seek를 지원한다. TRR의 triclinic cell, velocity와
force channel 및 XTC compression precision도 단위 provenance와 함께 보존한다. Thread-safe payload-budgeted LRU와
cancellation 가능한 async prefetch가 source 위에서 전체 trajectory 적재를 방지한다. Normalized
range/stride timeline은 once/loop/rock 및 forward/reverse playback과 방향성 prefetch hint를 제공한다.
Triclinic cell의 exact closest-lattice minimum image, 원자별 `[0,1)` wrap과 순차 trajectory
continuity unwrap도 core에 구현됐다. Atom distance의 raw/minimum-image mode는 Workspace, CLI,
GUI action과 Python command path에서 공유된다. Bond-aware molecule join/whole은 아직 없다.
Active topology에는 `traj load`로 DCD/TRR/XTC/MDCRD/NetCDF/H5MD/RST7/LAMMPS/BINPOS를 bounded cache와 함께 attach할 수 있고, zero-based
`traj frame` seek, inclusive range/stride, once/loop/rock playback, pause, FPS와 elapsed-time tick이
selected frame, existing representation, analysis와 scene system을 transactionally 함께 갱신한다.
Fractional frame time과 bounded catch-up은 desktop Qt event-loop scheduler에도 연결됐다. 현재 cache miss
decode와 representation rebuild는 GUI thread에서 실행되므로 background seek/load와 incremental coordinate
GPU upload는 아직 필요하다.
Object당 generation-aware prefetch worker가 현재 direction/range의 다음 frame을 cache에 채우며 seek,
range와 pause는 stale read-ahead를 supersede/cancel한다. Reader 내부에서 진행 중인 단일 decode 취소와
adaptive window는 아직 없다.
Attached trajectory의 inclusive frame range에서 centroid/COM과 atom distance를 계산하는 내장
time-series operation도 구현됐다. 계산은 current viewport frame을 바꾸지 않고 frame별 source
step/time, unit, selection/PBC/missing/mass provenance를 typed table에 보존한다. 같은 table은 GUI
presentation, native text/CSV/JSON과 Python list로 공유된다. Active-frame contact search는
bond exclusion과 raw/exact-triclinic minimum-image를 지원하는 spatial cell list로 CLI/GUI/Python에
내장됐다. Explicit typing provenance와 D-H-A geometry를 사용하는 active-frame H-bond 분석도 같은
경로에 내장됐다. Contact/H-bond trajectory frame count와 pair/triple occupancy도 typed table로 제공한다.
Persistent result와 plot은 아직 없다.
Protein secondary structure는 제한 license의 STRIDE binary를 포함하지 않고 독립적인
`molshredder-stride-method-v0` C++ kernel로 계산한다. Backbone phi/psi, inferred amide-H,
empirical H-bond pattern에서 H/G/I/E/B/T/C residue table을 만들며 exact STRIDE parity는 아직
검증되지 않았음을 result provenance에 명시한다. H-bond와 beta-bridge 후보는 cutoff-derived spatial
index와 sparse bond graph로 제한해 일반적인 단백질에서 residue-pair 전수 검사를 피한다.
Weighted proper rigid-body fitting과 trajectory RMSD/RMSF도 같은 analysis path에 구현됐다. Fit selection과
score/output selection을 분리하고 uniform/mass weight, reference frame, missing pair, Å/nm conversion과
pre-fit provenance를 기록한다. RMSF는 optional alignment 뒤 per-atom online population fluctuation과
observation count를 반환한다. PBC reconstruction, outlier rejection, RMSD matrix와 plot은 아직 없다.

## 예정 환경

- C++20 이상
- CMake와 CMake Presets
- Qt 6.8.3 Quick/QRhi 기반 optional desktop UI
- macOS, Linux, Windows
- Python automation interface

현재 개발용 conda environment 이름은 `molshredder`다. macOS arm64 환경에서 Python
3.12.13, CMake 4.4.2, Ninja 1.13.2, Apple Clang 21, Qt 6.8.3과 SPIRV-Tools를 확인했다.
Qt desktop target은 실제 Apple M5 Metal QRhi에서 cartoon mesh와 HiDPI screenshot smoke를 통과했다.

최종 dependency와 최소 OS version은 prototype 및 CI 검증 후 확정한다.

## 설치와 실행

아직 installer와 executable이 제공되지 않는다. 목표 배포 형태는 다음과 같다.

- macOS application bundle과 signed/notarized installer
- Linux portable 또는 distribution-friendly installer/package
- Windows signed installer와 uninstall integration

구체적인 설치법은 첫 artifact가 생성되면 이 절에 검증된 명령만 추가한다.

## 개발과 테스트

repository root에서 다음 표준 workflow를 사용한다.

```bash
conda activate molshredder
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

실행과 install staging 예시는 다음과 같다.

```bash
./build/dev/molshredder --version
./build/dev/molshredder system version
./build/dev/molshredder version
./build/dev/molshredder system version --format json
./build/dev/molshredder console
PYTHONPATH=build/dev python -c "import molshredder; print(molshredder.invoke('version'))"
cmake --install build/dev --prefix build/stage
```

Optional Qt Quick/QRhi desktop prototype은 다음처럼 빌드한다.

```bash
cmake --preset desktop
cmake --build --preset desktop
ctest --preset desktop -R desktop.gpu_smoke --output-on-failure
./build/desktop/molshredder_desktop.app/Contents/MacOS/molshredder_desktop
```

현재 viewport는 indexed cartoon mesh, unit-sphere/unit-cylinder instancing, pixel-width line quad,
depth, MSAA request, directional shading와 render-thread-safe packet snapshot을 Metal에서 검증한다.
`Open` dialog 또는 `--open`으로 PDB/mmCIF/BCIF/PQR/MOL/SDF/MOL2/PSF/GRO/G96/VTF/XYZ/OpenDX/MRC/CCP4를 shared GUI action/Workspace에 load하고
lines/sticks/spheres/ribbon/cartoon을 전환할 수 있다. Save dialog는 suffix에 따라 active current frame을
canonical PDB/mmCIF/PQR/MOL/SDF/MOL2/PSF/GRO/G96/XYZ writer로 내보내고 semantic-loss item count를 status에 표시한다. Mouse left-drag orbit, right-drag pan,
wheel dolly와 double-click framing은
core `scene::Camera`를 사용한다. Left click은 atom/bond/residue GPU ID를 읽어 `picked` selection으로
저장하고 하단 badge에 표시한다. Objects panel은 active/visibility를, trajectory panel은 attach·seek·step·
playback mode/direction/FPS를 제어한다. Visual selection highlight와 analysis panel은 다음 desktop slice다.
Qt가 없는 환경에서는 기존 dev/release core build가 유지된다.

```bash
./build/desktop/molshredder_desktop.app/Contents/MacOS/molshredder_desktop \
  --open=path/to/structure.pdb --trajectory=path/to/run.dcd \
  --trajectory-unit=angstrom --representation=sticks
```

Console은 현재 versioned internal form인 `invoke "system version"`을 실행하며 `help`, `history`,
`exit`/`quit` control을 제공한다. 실제 molecular vertical slice를 검증하기 전에는 전체
사용자-facing grammar를 고정하지 않는다.

Text/JSON/CSV result envelope와 typed table의 schema 및 stdout/stderr contract는
[Result envelope](docs/RESULT_FORMAT.md)에 문서화되어 있다.
첫 vertical slice의 provisional `load/select/show/analyze center/measure distance` 문법은
[Foundation command grammar](docs/COMMANDS.md)에 문서화되어 있다. 현재 이 다섯 명령은 실제
Workspace operation에 연결되어 있다. Atom distance는 `--pbc raw|minimum-image`를 실행하며 아직
구현되지 않은 selection reduction mode는 명시적 `unsupported`를 반환한다.
현재 Python import와 `invoke` contract는 [Python API prototype](docs/PYTHON_API.md)에 문서화되어
있다.
Topology, property, coordinate와 frame lifetime contract는 [Data model](docs/DATA_MODEL.md)에
문서화되어 있다.
현재 PDB/mmCIF/BCIF/PQR/MOL/SDF/MOL2/PSF/GRO/G96/VTF/XYZ read/write channel과 명시적 limitation은
[Structure format support](docs/FORMAT_SUPPORT.md)에 문서화되어 있다.
[Volumetric data](docs/VOLUMETRIC_DATA.md)는 typed scalar grid, OpenDX ordering/unit 계약과 아직 구현되지
않은 volume rendering 범위를 설명한다.
[Structure writing and semantic loss](docs/STRUCTURE_WRITING.md)는 `format list`, `save`, atomic publish와
loss-report 계약을 설명한다.
Scene hierarchy, transform, camera coordinate와 interaction contract는
[Scene and camera](docs/SCENE_AND_CAMERA.md)에 문서화되어 있다.
Representation packet, reference image/picking과 GPU prototype 경계는
[Rendering](docs/RENDERING.md)에 문서화되어 있다.
[Desktop viewport prototype](docs/DESKTOP_VIEWPORT.md)은 Qt/QRhi build, thread/resource contract와
현재 Metal 검증 범위를 설명한다.
[Molecular objects and visibility](docs/OBJECTS.md)는 multi-object state, active/visibility command와
desktop packet composition을 설명한다.
Atom selection grammar와 named-selection lifecycle은
[Selections](docs/SELECTIONS.md)에 문서화되어 있다.
Centroid, COM와 raw atom-distance 계약은
[Basic analysis](docs/BASIC_ANALYSIS.md)에 문서화되어 있다.
[Periodic boundary conditions](docs/PBC.md)는 triclinic minimum image, atom wrap과 trajectory unwrap의
계약 및 molecule-aware reconstruction 한계를 설명한다.
Stateful load/select/show sequence와 현재 lifetime 제한은
[Workspace workflow](docs/WORKSPACE.md)에 문서화되어 있다.
[Analysis presentation](docs/ANALYSIS_PRESENTATION.md)은 GUI result/marker/label contract와 Qt 연결 전
한계를 설명한다.
[Foundation session format](docs/SESSION_FORMAT.md)은 versioned canonical command journal과 replay
limitation을 설명한다.
[Trajectory format support](docs/TRAJECTORY_FORMATS.md)는 DCD/TRR/XTC/MDCRD/Amber NetCDF/H5MD/RST7 read channel, memory model과 제한을
설명한다.
[Trajectory runtime](docs/TRAJECTORY_RUNTIME.md)은 cache budget, LRU, lease와 async prefetch 계약을
설명한다.
[Trajectory commands](docs/TRAJECTORY_COMMANDS.md)는 attach, zero-based seek, deterministic play와
session replay 계약을 설명한다.
[Trajectory time-series analysis](docs/TIME_SERIES_ANALYSIS.md)는 centroid/COM, atom distance, frame range,
cancellation과 table provenance 계약을 설명한다.
[Rigid alignment, RMSD and RMSF](docs/ALIGNMENT_AND_FLUCTUATION.md)는 weighted fit, degeneracy, reference와
per-atom fluctuation 정의를 설명한다.

## 예정 repository 구조

```text
.
├── cmake/       # CMake helper와 dependency 설정
├── packaging/   # macOS, Linux, Windows installer 설정
├── scripts/     # 개발·release automation
├── src/         # application과 library source
└── tests/       # unit, integration, conformance와 packaging tests
```

## 공개 범위와 라이선스

MolShredder 자체 작성 코드는 [`GPL-3.0-or-later`](LICENSE)로 배포한다. 사용·수정·상업적 이용과
재배포는 GPL 조건에 따라 허용되며, covered binary나 수정본을 배포할 때에는 corresponding source와
고지 의무를 따라야 한다. Third-party component에는 각각의 원래 license와 notice가 적용된다.
상세 범위는 [License policy](docs/LICENSE_POLICY.md), dependency 고지는
[Third-party notices](THIRD_PARTY_NOTICES.md), 이름·로고 사용은 [Trademark policy](TRADEMARKS.md)를
참조한다.

## 기여

Contribution은 `GPL-3.0-or-later`와 Developer Certificate of Origin sign-off를 사용한다. 자세한
절차는 [CONTRIBUTING.md](CONTRIBUTING.md)에 있다. Issue를 열 때에는 OS, hardware/GPU, 재현 단계와
사용한 input format을 포함하는 것을 권장한다.
