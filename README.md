# MolShredder

MolShredder는 molecular structure visualization과 molecular dynamics trajectory 작업을 하나의
일관된 GUI, CLI 및 Python API로 제공하는 데스크톱 molecular viewer 프로젝트입니다. PyMOL과 VMD의
검증된 workflow를 기능·동작 reference로 사용하지만, 코드와 제품 정체성은 독립적으로 개발합니다.

주요 설계 목표는 다음과 같습니다.

- 세련된 molecular representation, camera, selection 및 publication workflow
- 대규모 trajectory의 bounded-memory loading, playback 및 분석
- COM, centroid, distance, RMSD, contact 등 자주 사용하는 분석의 기본 내장
- GUI, CLI와 Python이 공유하는 typed operation 및 scientific kernel
- 영어·한국어 UI와 macOS, Linux, Windows 지원
- 일반 사용자를 위한 플랫폼별 installer 제공

## 요구사항

- Conda 또는 Miniconda
- CMake 3.25 이상과 Ninja
- C++20 compiler
- Qt Desktop 빌드에는 `environment.yml`에 고정된 Qt package 사용

Windows에서는 Visual Studio Build Tools의 **Desktop development with C++** workload가 필요하며,
빌드 명령은 Developer PowerShell 또는 x64 Native Tools Command Prompt에서 실행해야 합니다.

## 개발 환경

```bash
git clone https://github.com/DobiShredder/MolShredder.git
cd MolShredder
conda env create -f environment.yml
conda activate molshredder
```

이미 환경이 있다면 다음과 같이 갱신합니다.

```bash
conda env update -n molshredder -f environment.yml --prune
```

## 빌드

Core, CLI와 Python module:

```bash
cmake --preset dev
cmake --build --preset dev
```

Qt Desktop application:

```bash
cmake --preset desktop
cmake --build --preset desktop
```

전체 검증이 필요할 때는 해당 preset의 test suite를 실행합니다.

```bash
ctest --preset dev --output-on-failure
ctest --preset desktop --output-on-failure
```

## 실행

CLI:

```bash
./build/dev/molshredder --help
./build/dev/molshredder console
./build/dev/molshredder system info --format json
```

Windows에서는 `./build/dev/molshredder` 대신 `build\dev\molshredder.exe`를 사용합니다.

Desktop executable 위치:

| 플랫폼 | 경로 |
|---|---|
| macOS | `build/desktop/molshredder_desktop.app/Contents/MacOS/molshredder_desktop` |
| Linux | `build/desktop/molshredder_desktop` |
| Windows | `build\desktop\molshredder_desktop.exe` |

구조 파일을 시작과 함께 열 수도 있습니다.

```bash
./build/desktop/molshredder_desktop --open=protein.pdb --representation=cartoon
```

macOS에서는 위 표의 application bundle 내부 executable 경로를 사용합니다. Desktop 언어는 OS locale을
따르며 `--language=en`, `--language=ko` 또는 `--language=system`으로 지정할 수 있습니다.

## Python API

Build tree의 extension module은 다음과 같이 사용할 수 있습니다.

```bash
PYTHONPATH=build/dev python -c \
  "import molshredder; print(molshredder.invoke('system info'))"
```

```python
import molshredder

molshredder.invoke("load", {"path": "protein.pdb"})
molshredder.invoke("show", {"representation": "cartoon", "selection": "all"})
result = molshredder.invoke(
    "analyze center",
    {"selection": "all", "mode": "com", "unit": "angstrom"},
)
```

Python, CLI와 GUI는 동일한 operation registry와 Workspace kernel을 호출합니다. 외부 Python script는
application과 동일한 권한으로 실행되므로 신뢰할 수 있는 script만 명시적으로 허용해야 합니다.

## Local staging install

```bash
cmake --install build/desktop --prefix build/stage
```

Staging install은 dependency와 설치 layout을 검사하기 위한 개발용 설치입니다. 배포용 installer와 signing은
별도의 release pipeline에서 다룹니다.

## 문서

- [Command reference](docs/COMMANDS.md)
- [Python API](docs/PYTHON_API.md)
- [Desktop application](docs/DESKTOP_VIEWPORT.md)
- [Automation and script execution](docs/AUTOMATION.md)
- [Localization](docs/LOCALIZATION.md)
- [File format support](docs/FORMAT_SUPPORT.md)
- [Trajectory formats](docs/TRAJECTORY_FORMATS.md)
- [Trajectory attachment contract](docs/TRAJECTORY_ATTACHMENT.md)
- [Selection language](docs/SELECTIONS.md)
- [Analysis](docs/BASIC_ANALYSIS.md)
- [Rendering](docs/RENDERING.md)
- [Capability support](docs/CAPABILITY_SUPPORT.md)
- [Dependencies and build support](docs/DEPENDENCIES.md)

## 라이선스

MolShredder 자체 작성 코드는 [GPL-3.0-or-later](LICENSE)로 배포합니다. Third-party component에는 각
component의 license가 적용됩니다.

- [License policy](docs/LICENSE_POLICY.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Trademark policy](TRADEMARKS.md)

기여 방법은 [CONTRIBUTING.md](CONTRIBUTING.md)를 참고하세요.
