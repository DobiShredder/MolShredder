# MolShredder

MolShredder는 molecular structure visualization, editing, rendering과 molecular dynamics
trajectory 분석을 하나의 application에 통합하는 고성능 molecular viewer다.

## 목표

- PyMOL 수준의 molecular representation과 publication workflow
- VMD 수준의 trajectory playback, PBC 처리와 time-series analysis
- 가능한 많은 structure, trajectory, volumetric 및 quantum-chemistry format 지원
- macOS, Linux, Windows용 desktop application과 installer 제공
- 자주 쓰는 분석 기능을 외부 script 없이 GUI, CLI와 Python에서 제공
- GUI에서 실행할 수 있는 모든 기능을 CLI와 Python에서도 동일하게 호출
- 대규모 trajectory를 위한 bounded-memory streaming과 가속 가능한 C++ core

PyMOL과 VMD는 기능 및 동작의 reference이며 MolShredder는 두 제품과 독립적으로 개발된다.

## 개발 환경

공통 개발 환경은 conda로 관리한다.

```bash
conda env create -f environment.yml
conda activate molshredder
```

이미 환경이 있다면 다음 명령으로 갱신한다.

```bash
conda env update -n molshredder -f environment.yml --prune
```

## 빌드와 테스트

Core, CLI와 Python module:

```bash
conda activate molshredder
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Qt desktop application:

```bash
cmake --preset desktop
cmake --build --preset desktop
ctest --preset desktop --output-on-failure
```

빈 prefix에 local staging install을 만들 수 있다. 이 단계는 개발용 설치 검증이며 서명된 installer는 아니다.

```bash
cmake --install build/desktop --prefix build/stage
./build/stage/bin/molshredder system info --format json
./build/stage/molshredder_desktop.app/Contents/MacOS/molshredder_desktop
```

현재 CMake staging layout은 Linux/Windows에서 각각 `bin/molshredder_desktop`과
`bin/molshredder_desktop.exe`를 사용한다. 이 개발용 layout은 최종 installer 형식이 아니며 원격 platform
checkpoint가 통과하기 전에는 지원 완료로 간주하지 않는다.

Public repository의 `Cross-platform CI`는 수동 dispatch 시 세 OS에서 warnings-as-errors Qt Desktop 전체 suite,
빈 prefix install, 실제 graphics backend와 설치본 daily workflow를 실행하도록 구성되어 있다. macOS는 Metal,
Linux는 Xvfb 아래 Vulkan/OpenGL, Windows는 Direct3D 11/12를 명시적으로 확인한다.

## 실행

```bash
./build/dev/molshredder --help
./build/dev/molshredder console
./build/dev/molshredder system version --format json
./build/dev/molshredder system info --format json
```

Python에서는 같은 command dispatcher를 호출한다.

```bash
PYTHONPATH=build/dev python -c \
  "import molshredder; print(molshredder.invoke('version'))"
```

검토한 local Python script는 명시적 trust와 함께 동일 Workspace에서 실행할 수 있다.

```bash
./build/dev/molshredder script run --path analysis.py --trust true
```

```python
molshredder.run_script("analysis.py", ["trajectory.dcd"], trusted=True)
```

Desktop에서는 상단 `Run Script`에서 file을 선택하고 권한 경고를 확인한 뒤 실행한다.
실행 중에도 UI는 응답하며 viewer 편집은 잠시 잠긴다. Cancel은 cooperative request이므로 실행 중인 Python code가
반환된 뒤 취소가 확정된다.
상단 `System`은 같은 `system info` operation의 build, dependency와 실제 graphics runtime 결과를 읽기 쉬운 panel로
표시한다. 값이 runtime에서 제공되지 않으면 추정하지 않고 `Not reported`로 표시한다.
상단 `Views`에서는 현재 camera를 이름으로 저장·복원·삭제할 수 있다. 같은 기능은
`view store/recall/list/delete/clear` command와 Python `invoke()`에서도 제공된다. 같은 panel과
`view export-pymol/import-pymol`에서 PyMOL `get_view/set_view` 18-value camera를 복사하거나 붙여넣을 수 있다.
Recall과 import는 PyMOL 호환 easing으로 부드럽게 이동하며 command의 `--duration`과 `--hand`로 제어한다.
Selection 입력과 Center, Fit, Orient, Set pivot, Reset all도 제공하며 각각 공용
`view center/zoom/orient/origin/reset` operation을 호출한다. Orient는 선택 좌표의 principal axes를 화면 축에
정렬하고 회전된 bounds를 다시 frame한다.
Object reference와 XYZ 입력은 selection/coordinate 기반 object pivot, object-only reset과 명시 camera pivot을
같은 `view origin/reset` operation으로 제공한다.
State control에서 current, all 또는 1-based explicit coordinate state를 선택할 수 있고,
selection-based clipping도 같은 scope를 사용한다.
같은 panel의 Axis navigation에서 camera-local X/Y/Z 이동과 model-origin pivot 회전을 수행하며,
CLI·Python은 `view move/turn`으로 동일한 typed operation을 호출한다.
Projection mode와 degree FOV는 기본적으로 target-plane scale을 유지하는 `view projection`으로 전환하며,
고급 사용자는 raw switch를 선택할 수 있다. 긴 Views workflow는 scroll할 수 있다.
Portable stereo는 같은 panel 또는 `stereo set`에서 side-by-side/cross-eye/wall-eye/anaglyph 및
row/column/checkerboard interleave, eye swap, separation과 angle scale을 제어하며 CLI·GUI·Python이 같은
operation을 사용한다. Anaglyph는 true/gray/color/half-color/optimized 합성을 제공하고, 미구현
quad-buffer/OpenVR는 명시적으로 보고한다.
같은 panel에서 7개 clipping mode와 현재 near/far range를 조절·조회할 수 있으며
`view clip/get-clip`과 Python `invoke()`도 동일한 기능을 제공한다.
상단 `Analyze`는 COM/centroid, atom distance, contacts와 trajectory RMSD를 계산하고 persistent result의
provenance, overlay, JSON/CSV export와 lifecycle을 관리한다. CLI·Python은 같은 `analyze`, `measure`,
`result` operation을 호출한다.
Trajectory attach는 기본 `exact` identity 검증을 사용하며, 상단 mapping control에서 verified index order나
stable-ID explicit map을 선택할 수 있다. Position/cell/time/velocity/force channel은 canonical unit과 drift
contract를 통과한 뒤 Workspace에 commit된다. Desktop attach와 seek는 progress/cancel이 있는 bounded
background task이며 rapid seek에서는 마지막 요청만 반영한다. CLI와 Python은 같은 kernel의 최종 결과를
동기적으로 반환한다.

명령과 desktop 실행 방법은 아래 문서를 참조한다.

- [Command reference](docs/COMMANDS.md)
- [Python API](docs/PYTHON_API.md)
- [Automation and script execution](docs/AUTOMATION.md)
- [Desktop application](docs/DESKTOP_VIEWPORT.md)

## 문서

- [지원 file format](docs/FORMAT_SUPPORT.md)
- [Trajectory format과 runtime](docs/TRAJECTORY_FORMATS.md)
- [Trajectory mapping과 channel/unit contract](docs/TRAJECTORY_ATTACHMENT.md)
- [Volumetric data](docs/VOLUMETRIC_DATA.md)
- [Selection language](docs/SELECTIONS.md)
- [Analysis](docs/BASIC_ANALYSIS.md)
- [Persistent analysis results](docs/ANALYSIS_RESULTS.md)
- [Rendering](docs/RENDERING.md)
- [Render setting service](docs/RENDER_SETTINGS.md)
- [Data model](docs/DATA_MODEL.md)
- [Bounded task execution](docs/TASK_EXECUTION.md)
- [Dependency](docs/DEPENDENCIES.md)
- [Build support configuration](docs/SUPPORT_CONFIGURATION.md)
- [Evidence-gated capability support table](docs/CAPABILITY_SUPPORT.md)

## 라이선스

MolShredder 자체 작성 코드는 [GPL-3.0-or-later](LICENSE)로 배포한다. Third-party component에는
각 component의 license가 적용된다.

- [License policy](docs/LICENSE_POLICY.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Trademark policy](TRADEMARKS.md)

## 기여

기여 절차는 [CONTRIBUTING.md](CONTRIBUTING.md)를 참조한다.
