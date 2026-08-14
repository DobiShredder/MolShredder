# Trajectory command foundation

Trajectory command는 active molecular object's topology에 DCD/TRR/XTC/MDCRD/Amber NetCDF/H5MD/RST7/LAMMPS/BINPOS coordinate source를 붙이고,
같은 state를 GUI action, native console, Python과 canonical session replay에서 공유한다. 이 문법은
MD vertical slice에서 검증 중인 additive command이며 foundation grammar v1의 다섯 leaf command를
변경하지 않는다.

```text
molshredder analyze trajectory center [--selection EXPR] [--mode centroid|com]
                                      [--first INDEX] [--last INDEX] [--stride N]
                                      [--missing error|skip]
                                      [--precision 0..15]
                                      [--unit angstrom|nanometer]
molshredder analyze trajectory distance --from EXPR --to EXPR
                                        [--pbc raw|minimum-image]
                                        [--first INDEX] [--last INDEX] [--stride N]
                                        [--precision 0..15]
                                        [--unit angstrom|nanometer]
molshredder analyze trajectory rmsd|rmsf [--selection EXPR]
                                        [--fit-selection EXPR]
                                        [--reference INDEX]
                                        [--first INDEX] [--last INDEX] [--stride N]
                                        [--fit rigid|none]
                                        [--weight uniform|mass]
                                        [--missing error|skip]
                                        [--precision 0..15]
                                        [--unit angstrom|nanometer]
molshredder traj load --path PATH [--file-format auto|dcd|trr|xtc|mdcrd|crd|netcdf|nc|ncdf|h5md|rst7|lammps|binpos]
                                      [--coordinate-unit auto|angstrom|nanometer]
                                      [--particle-group NAME]
                                      [--cache-mib POSITIVE_INTEGER]
                                      [--prefetch-frames NON_NEGATIVE_INTEGER]
molshredder traj frame --frame ZERO_BASED_INDEX
molshredder traj play [--mode once|loop|rock]
                      [--direction forward|reverse] [--steps NON_NEGATIVE_INTEGER]
molshredder traj range [--first ZERO_BASED_INDEX] [--last ZERO_BASED_INDEX]
                       [--stride POSITIVE_INTEGER]
                       [--mode once|loop|rock] [--direction forward|reverse]
molshredder traj speed --fps POSITIVE_NUMBER
molshredder traj tick --elapsed-ms NON_NEGATIVE_NUMBER
molshredder traj pause
```

네 `analyze trajectory` command는 attached cache의 frame을 읽되 playback/current scene state를
변경하지 않는다. Inclusive first/last, positive stride, metadata/provenance table 및 CSV/JSON 계약은
[Time-series analysis](TIME_SERIES_ANALYSIS.md)에 둔다.

Stateful native workflow는 각 명령을 별도 process로 실행하지 않고 `molshredder console`에서 수행한다.
Python에서는 같은 순서로 `molshredder.invoke()`를 호출한다.

## Attach

`traj load`는 active topology의 atom 수를 reader에 전달해 trajectory와 topology mismatch를 attach
전에 거부한다. `auto`는 case-insensitive `.dcd`, `.trr`, `.xtc`, `.mdcrd`, `.crd`, `.nc`, `.ncdf`, `.netcdf`, `.h5md`, `.rst7`, `.restrt`, `.inpcrd`, `.inprst`
suffix를 사용하고 알 수 없는 suffix는 추정하지 않는다. 기본 cache budget은 256 MiB이며 decoded scientific payload의 상한이지 process RSS
hard limit은 아니다.

RST7은 by definition one-frame in-memory restart다. 좌표 Å, time ps, optional temperature K와 3/6-value
box를 보존하고 native AKMA velocity는 20.455를 곱해 Å/ps로 정규화한다. NATOM이 active topology와 다르면
attach 전에 실패한다. 1–2 atom file에서 optional trailing block이 velocity인지 box인지 구분할 수 없는
경우에는 추정하지 않고 오류를 반환한다.

MDCRD/CRD는 active topology atom count로 frame boundary를 찾고 open 시 byte offset만 index한다. 3-value
box length에는 matching PRMTOP `BOX_DIMENSIONS` angle이 필요하며, 없으면 90°를 발명하지 않고 attach를
거부한다. Format 자체에 step/time이 없으므로 frame index를 simulation step이나 physical time으로 표시하지 않는다.

Amber NetCDF는 `.nc/.ncdf/.netcdf` 또는 `--file-format netcdf`로 열고 global
`Conventions=AMBER`, `ConventionVersion=1.0`, `spatial=x,y,z` 및 channel shape/unit을 검증한다.
Classic/64-bit/NetCDF-4 storage, coordinate/velocity/force/time/temperature/cell scale factor와
CPPTRAJ의 `compressedpos/vel/frc` integer compression을 지원한다. Cell length/angle은 함께 있어야 하며
현재 core가 표현하지 못하는 부분 주기 cell은 invalid cell로 실패한다. REMD metadata와 writer는 아직 없다.

H5MD는 `.h5md` 또는 `--file-format h5md`로 연다. `--particle-group`을 주면 `/particles/NAME`을
선택하고, 생략하면 `trajectory`를 우선한 뒤 단 하나의 group만 자동 선택한다. 여러 group이 모호하면
추정하지 않고 이름을 요구한다. Position unit은 H5MD `unit`을 우선하며 없을 때만
`--coordinate-unit angstrom|nanometer`가 필요하다. Static/dynamic `id`로 source atom을 topology
`source_serial`에 재배치하고 fill value와 `presence`로 frame별 missing atom을 보존한다. 독립 channel
timeline은 source step으로 position frame과 맞추며, 맞지 않는 optional channel은 해당 frame에서 생략한다.
현재 arbitrary `/observables`, partial-periodic box, writer와 external/virtual HDF5 storage는 지원하지 않는다.

LAMMPS custom text dump는 format에 length unit이 없으므로 `--coordinate-unit angstrom|nanometer`를 반드시
명시한다. `auto`는 오류다. `units real`/`metal`의 native distance는 Å이지만 다른 unit style을 이름만 보고
추정하지 않는다. Atom row는 frame마다 순서가 바뀔 수 있어 `id` column을 active topology의 complete unique
source serial에 매핑하며, ID가 없거나 set이 다르면 attach 전에 실패한다. GUI의 `Traj Å`/`Traj nm` control도
같은 parameter를 canonical action에 전달한다.

Scripps BINPOS는 `.binpos` suffix 또는 `--file-format binpos`로 연다. Active topology atom count를 각
frame record와 대조하고 이를 이용해 legacy native-endian file의 little/big byte order를 판별한다. 좌표는
float32 Cartesian Å이며 format에 없는 unit cell, step/time, velocity/force를 생성하지 않는다.

기본 read-ahead는 현재 direction/mode/range에서 방문할 다음 4 frame이다. `--prefetch-frames 0`은
비활성화하며 양수 값은 sequence 크기 안에서 deduplicate된다. Attach, seek, range, step play와 tick은
새 timeline hint generation을 예약하고 pause는 pending read-ahead를 취소한다. Command response의
`prefetch_generation/state/requested_count/completed_count`로 비동기 상태를 관찰할 수 있다.

Scheduler는 호출마다 thread를 만들지 않고 object당 C++20 `jthread` 하나를 사용한다. 새 generation이
오면 queued request를 교체하며 이미 decode 중인 한 frame이 끝난 뒤 stale generation의 나머지 frame을
중단한다. 현재 reader API가 decode 중 cancellation을 받지 않으므로 진행 중인 file read 자체를 강제
중단하지 못하고 scheduler destruction도 그 read 완료를 기다린다.

Reader open, non-zero known frame count, cache 생성, frame 0 decode, 기존 representation rebuild,
새 `MolecularSystem` 및 scene snapshot 생성이 모두 성공한 뒤에만 Workspace를 교체한다. 실패하면 기존
system, scene, representation과 trajectory state는 바뀌지 않는다.

## Frame과 play

`traj frame`의 frame index는 VMD-style zero-based다. Seek 대상 frame decode와 모든 기존
representation rebuild가 성공한 뒤 timeline을 commit한다. 이후 `show`, centroid/COM와 distance는
선택된 frame lease를 사용한다.

`traj play`는 testable한 deterministic transition command다. `steps`만큼
`PlaybackTimeline`을 전진하고 도착 frame을 decode한 뒤 representation과 timeline을 함께 commit한다.
Once/loop/rock 및 forward/reverse boundary 의미는 [Trajectory runtime](TRAJECTORY_RUNTIME.md)을 따른다.
응답은 frame, normalized sequence 위치/크기, mode/direction/playing, source step/time과 rebuild count를
포함한다.

`traj range`는 inclusive first/last/positive stride를 normalized sequence로 만들고 direction endpoint로
seek한 paused state를 설치한다. Last를 생략하면 source의 마지막 frame이다. Range 교체, explicit
`traj frame`과 `traj pause`는 오래된 wall-time accumulator를 reset한다.

`traj speed`는 positive finite FPS를 설정한다. `traj tick`은 외부 UI/event loop가 측정한 elapsed
milliseconds를 전달한다. `PlaybackClock`은 fractional transition을 다음 tick까지 보존한다. 한 tick의
catch-up는 최대 120 transition이며 UI stall로 생긴 나머지 integer backlog도 버리지 않고 후속 tick에
처리한다. Backlog는 10억 transition 미만으로 제한해 비정상 elapsed input을 거부한다. Once mode가
endpoint에서 정지하면 backlog를 reset한다. Pause 상태의 valid elapsed tick은 frame을 바꾸지 않는다.

Clock, timeline, frame decode와 representation rebuild는 copy에서 계산하고 전부 성공한 뒤 commit한다.
따라서 backing file read failure나 invalid range/speed/tick은 이전 frame, fractional time과 packet을
보존한다.

Desktop viewport는 16 ms precise Qt timer에서 실제 elapsed milliseconds만 `traj tick`에 전달한다.
QML은 transition이나 boundary를 계산하지 않는다. Frame이 바뀌지 않은 fractional tick은 trajectory
state만 동기화하고 render packet/GPU geometry를 다시 올리지 않는다. Attach, seek, first/last step,
play/pause, once/loop/rock, direction과 FPS control도 모두 같은 canonical action을 호출한다.

Canonical session에 `traj load`, `traj frame`, `traj play`를 기록하면 external trajectory path를 다시
열어 state를 재현한다. 따라서 session은 아직 self-contained가 아니며 파일 변경/hash 검증 정책도
후속 범위다.

## 현재 한계

- adaptive window/priority와 GUI event-loop completion integration
- background cache-miss decode, rapid-seek cancellation과 frame-drop/backpressure policy
- desktop editable range/stride, physical-time display와 topology mapping policy UI
- in-flight single-frame decode cancellation 및 cross-object worker pool
- 여러 molecule의 synchronized playback
- coordinate-dependent selection과 incremental GPU representation update
- trajectory append/delete/write, topology detach와 original coordinate-source restore
- file identity/hash 확인, live file mutation 감지와 transactional whole-session replay
