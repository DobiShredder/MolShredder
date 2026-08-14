# Dependency policy

## 현재 direct dependency

MolShredder는 다음 direct library dependency를 CMake에서 exact release archive와 SHA-256으로
pin한다. 개발 도구는 `environment.yml`에 기록한다.

| 도구 | 범위 | 용도 |
|---|---|---|
| Python 3.12 | development/runtime ABI | automation과 CPython extension target |
| CMake 3.25 이상, 5 미만 | development/build | cross-platform build generation |
| Ninja 1.11 이상 | development/build | 공통 local/CI build executor |
| CLI11 2.6.2 | build/static headers | Native CLI parsing과 subcommand/help adapter |
| pybind11 3.1.0 | build/header-only | CPython extension module 생성 |
| Qt 6.8.3 | optional desktop/runtime | Qt Quick UI와 QRhi GPU viewport |
| SPIRV-Tools | desktop build tool | multi-backend `.qsb` shader optimization |
| Native threading runtime | system/runtime | async trajectory prefetch와 synchronization |
| xdrfile/Chemfiles-derived code | compiled source | XTC compressed-coordinate decoding |

Compiler와 platform SDK는 conda에 고정하지 않고 각 OS의 native toolchain을 사용한다.
Thread support는 CMake `Threads::Threads` portable target으로 link하며 별도 vendored library를
추가하지 않는다.

- macOS: Apple Clang과 macOS SDK
- Linux: CI에서 pin한 GCC 또는 Clang과 system SDK
- Windows: Visual Studio Build Tools의 MSVC와 Windows SDK

## 재현 전략

1. `environment.yml`은 세 OS에서 공유하는 direct 개발 dependency의 범위를 정의한다.
2. OS별 exact lock은 실제 CI와 dependency 집합이 안정화된 뒤 생성한다.
3. transitive package를 `environment.yml`에 직접 복사하지 않는다.
4. C++ library를 추가할 때는 version/source hash, license, 세 OS 지원, linkage, binary size와
   installer notice 요구를 dependency registry에 기록한다.
5. Qt desktop은 exact 6.8.3 minor에 고정하고 동적 link한다. QRhi는 minor-version source/binary
   compatibility guarantee가 없으므로 Qt upgrade마다 desktop compile/render regression이 필요하다.

## Qt Quick/QRhi provenance

- Version: Qt 6.8.3 (`qt6-main=6.8.3` from conda-forge for development)
- Modules: Core, Gui/GuiPrivate, Qml, Quick, ShaderTools
- License: selected modules are available under LGPL-3.0-only/GPL licensing; artifact별 Qt SBOM과 license를
  release packaging에서 다시 수집한다.
- Linkage: desktop target only, dynamic. Headless core/CLI/Python build에는 Qt가 필요하지 않다.
- Compatibility: `QQuickRhiItem` is public, while QRhi uses `Qt6::GuiPrivate` and offers limited minor-version
  compatibility. The project therefore pins 6.8.3 exactly.
- Distribution: bundled library replacement/relinking rights, corresponding Qt source or written offer,
  license/copyright notice와 user installation information을 installer gate에서 제공한다.
- Reference: the independently adapted lifecycle follows Qt's BSD-3-Clause RHI Texture Item example; its
  notice is retained in `THIRD_PARTY_NOTICES.md`.

SPIRV-Tools is a build-only dependency used by Qt ShaderTools/qsb optimization. Generated `.qsb` artifacts are
packaged, not the `spirv-opt` executable. Exact build-tool lock과 its Apache-2.0 notice는 release SBOM에서
검증한다.

## CLI11 provenance

- Source: `https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.6.2.tar.gz`
- SHA-256: `c6ea6b2e5608b3ea8617999bd5f47420c71b2ebdb8dc4767c1034d1da5785711`
- License: BSD-3-Clause
- Linkage: header-only, native CLI target에만 사용
- Distribution obligation: binary documentation/materials에 copyright, conditions와 disclaimer 보존

## pybind11 provenance

- Source: `https://github.com/pybind/pybind11/archive/refs/tags/v3.1.0.tar.gz`
- Release: `https://github.com/pybind/pybind11/releases/tag/v3.1.0` (2026-08-06)
- SHA-256: `ef712655692a2e9bf7bb7874c022564a45f91d847ddee987e720cd9e28849665`
- License: BSD-3-Clause
- Linkage: header-only binding generator, Python extension target에만 사용
- Supported baseline: Python 3.9+, MSVC 2022+, GCC/Clang, Windows/Linux/macOS
- Current build target: Python 3.12 CPython ABI-specific extension
- Distribution obligation: source/binary redistribution에 copyright, conditions와 disclaimer 보존

Python extension은 현재 wheel이 아니라 CMake install tree의
`lib/molshredder/python/`에 배치한다. Python minor-version ABI와 runtime bundling 정책은 installer
단계에서 wheel 또는 embedded-runtime prototype을 비교한 뒤 확정한다.

## XTC decoder provenance

- xdrfile source: `https://ftp.gromacs.org/pub/contrib/xdrfile-1.1.4.tar.gz`
- xdrfile archive SHA-256: `e3c587c5ff24441a092fe2f3bc1dc03667bf126558f437161e779bfbcce48022`
- xdrfile license: BSD-2-Clause
- Chemfiles source: `https://github.com/chemfiles/chemfiles`
- Chemfiles reference commit: `51a17e85027c7b31ef3ae1531ed42281e578e513`
- Chemfiles license: BSD-3-Clause
- Use: source algorithm adapted into the bounds-checked C++20 implementation in
  `src/io/xtc_reader.cpp`; neither full library is linked or vendored
- Distribution obligation: both copyright/conditions/disclaimers and Chemfiles
  non-endorsement clause are installed through `THIRD_PARTY_NOTICES.md`

## 환경 생성

```bash
conda env create -f environment.yml
conda activate molshredder
```

기존 환경은 다음처럼 direct dependency constraint에 맞춰 갱신할 수 있다.

```bash
conda env update -n molshredder -f environment.yml --prune
```
