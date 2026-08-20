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

## 실행

```bash
./build/dev/molshredder --help
./build/dev/molshredder console
./build/dev/molshredder system version --format json
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

명령과 desktop 실행 방법은 아래 문서를 참조한다.

- [Command reference](docs/COMMANDS.md)
- [Python API](docs/PYTHON_API.md)
- [Automation and script execution](docs/AUTOMATION.md)
- [Desktop application](docs/DESKTOP_VIEWPORT.md)

## 문서

- [지원 file format](docs/FORMAT_SUPPORT.md)
- [Trajectory format과 runtime](docs/TRAJECTORY_FORMATS.md)
- [Volumetric data](docs/VOLUMETRIC_DATA.md)
- [Selection language](docs/SELECTIONS.md)
- [Analysis](docs/BASIC_ANALYSIS.md)
- [Rendering](docs/RENDERING.md)
- [Data model](docs/DATA_MODEL.md)
- [Dependency](docs/DEPENDENCIES.md)

## 라이선스

MolShredder 자체 작성 코드는 [GPL-3.0-or-later](LICENSE)로 배포한다. Third-party component에는
각 component의 license가 적용된다.

- [License policy](docs/LICENSE_POLICY.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Trademark policy](TRADEMARKS.md)

## 기여

기여 절차는 [CONTRIBUTING.md](CONTRIBUTING.md)를 참조한다.
