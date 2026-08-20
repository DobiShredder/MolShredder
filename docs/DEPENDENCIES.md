# Dependency policy

## 현재 direct dependency

MolShredder는 다음 direct library dependency를 CMake에서 exact release archive와 SHA-256으로
pin한다. 개발 도구는 `environment.yml`에 기록한다.

| 도구 | 범위 | 용도 |
|---|---|---|
| Python 3.12 | development/runtime ABI | CPython extension과 explicit in-process script runtime |
| CMake 3.25 이상, 5 미만 | development/build | cross-platform build generation |
| Ninja 1.11 이상 | development/build | 공통 local/CI build executor |
| CLI11 2.6.2 | build/static headers | Native CLI parsing과 subcommand/help adapter |
| pybind11 3.1.0 | build/header-only | CPython extension module 생성 |
| msgpack-cxx 8.0.0 | build/header-only | BinaryCIF MessagePack container decode와 synthetic fixture encode |
| Qt 6.8.3 | optional desktop/runtime | Qt Quick UI와 QRhi GPU viewport |
| netCDF-C 4.9 이상, 5 미만 | core/runtime | Amber NetCDF classic/64-bit/NetCDF-4 trajectory I/O |
| HDF5 1.14 이상, 3 미만 | core/runtime | H5MD 1.x hierarchy, metadata와 random-access dataset I/O |
| SPIRV-Tools | desktop build tool | multi-backend `.qsb` shader optimization |
| Native threading runtime | system/runtime | async trajectory prefetch와 synchronization |
| xdrfile/Chemfiles-derived code | compiled source | XTC compressed-coordinate decoding |
| VMD molfile ABI 18 declarations | compiled source/interface | Optional dynamic format-provider adapter |

Compiler와 platform SDK는 conda에 고정하지 않고 각 OS의 native toolchain을 사용한다.
Thread support는 CMake `Threads::Threads` portable target으로 link하며 별도 vendored library를
추가하지 않는다.
선택된 OS/architecture/compiler와 HDF5/netCDF/Python version 및 compile feature는 generated
`molshredder-support.json`과 `system info`에서 확인한다. 이 metadata는 실제 GPU backend 실행 검증이나
최소 지원 OS 약속을 대신하지 않는다.

CLI는 script command를 실제로 호출할 때 embedded CPython을 초기화한다. Python extension은 host interpreter를
사용하며 `libpython`을 별도로 link하지 않는다. macOS에서 두 runtime을 한 module에 link하지 않도록 automation
static library는 Python header만 사용하고 CLI executable이 `Development.Embed`, extension target이
`Development.Module` link mode를 각각 소유한다.

- macOS: Apple Clang과 macOS SDK
- Linux: CI에서 pin한 GCC 또는 Clang과 system SDK
- Windows: Visual Studio Build Tools의 MSVC와 Windows SDK

동시성 회귀는 지원되는 Unix compiler에서 opt-in `tsan` preset으로 실행한다. 이 preset은
`MOLSHREDDER_ENABLE_THREAD_SANITIZER=ON`을 사용해 MolShredder C++ target에
ThreadSanitizer와 frame pointer를 적용하며 `io.molfile_provider` fixture만 build/test한다.
Windows/MSVC에서는 ThreadSanitizer runtime을 제품 요구사항으로 간주하지 않고 configure 단계에서 명시적으로
거부한다. 일반 `dev`, `release`, desktop 및 세 OS CI build에는 sanitizer instrumentation을 적용하지 않는다.

## VMD molfile ABI 18 provenance

- Official headers: public VMD plugin documentation의 `vmdplugin.h` revision 1.35와
  `molfile_plugin.h` revision 1.112
- ABI baseline: 18
- License: UIUC Open Source License; full notice는 `THIRD_PARTY_NOTICES.md`에 보존한다.
- Linkage: platform loader(`dlopen`/`dlsym`/`dlclose` 또는
  `LoadLibraryW`/`GetProcAddress`/`FreeLibrary`)를 사용하며 VMD main code를 link하지 않는다.
- Distribution: 이 vertical slice는 VMD plugin source/binary를 bundle하지 않는다. 후속 plugin은
  file별 license와 dependency audit를 별도로 통과해야 한다.
- Trust boundary: application-bundled approved directory, user-approved directory 또는 명시적
  file path만 discovery 대상이다.
- Lifecycle: ABI/type/callback과 duplicate를 registry commit 전에 검증하며 plugin-owned callback
  table은 역순 `vmdplugin_fini`와 unload까지 library lifetime으로 보호한다.

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

## msgpack-cxx provenance

- BinaryCIF normative specification: `molstar/BinaryCIF` commit
  `ce75b24289746edc28dcef9a703afca2c7e74d81`
- Independent behavior cross-check: `molstar/molstar` commit
  `5bd9cb1f3075347db16aa0a46f771907e5889a29`; no Mol* source was copied or vendored
- Version: 8.0.0, tag `cpp-8.0.0`
- Source: `https://github.com/msgpack/msgpack-c/archive/refs/tags/cpp-8.0.0.tar.gz`
- SHA-256: `f634fb7052da4478096f2a02dfb6d91174e5836b317afb006375249ccb086aa8`
- License: BSL-1.0
- Linkage: header-only private implementation dependency of the BinaryCIF reader; configured with
  `MSGPACK_NO_BOOST` so no Boost runtime or header dependency enters MolShredder
- Install boundary: the FetchContent subdirectory is `EXCLUDE_FROM_ALL`; Release staging contains no
  msgpack header, CMake package or exported target because MolShredder public headers do not require it
- Security boundary: MessagePack collection/string/bin/depth limits, exact single-root consumption and
  BinaryCIF row/encoding expansion limits are enforced before topology construction
- Distribution obligation: full Boost Software License 1.0 notice is retained in `THIRD_PARTY_NOTICES.md`

## netCDF-C provenance

- Development version verified: netCDF-C 4.10.0 from conda-forge
- Version range: `libnetcdf>=4.9,<5`; release/CI lock must pin an exact artifact
- Source release: `https://github.com/Unidata/netcdf-c/releases/tag/v4.10.0`
- License: BSD-3-Clause, official `v4.10.0/COPYRIGHT`
- Linkage: dynamic through the imported `netCDF::netcdf` CMake target; core reader
  calls the public C API
- Format coverage: classic CDF-1, 64-bit-offset CDF-2, 64-bit-data CDF-5 and
  NetCDF-4/HDF5 as enabled by the selected runtime build
- Threading: all netCDF API access is serialized inside the native Amber reader;
  no thread-safety guarantee from a particular distribution is exposed to callers
- Distribution obligation: copyright, license conditions and disclaimer are
  reproduced in `THIRD_PARTY_NOTICES.md`
- Packaging gate: installer dependency collection must include and audit the exact
  dynamically loaded netCDF-C, HDF5 and compression/runtime closure for each OS;
  a development conda environment is not evidence of a self-contained installer
- Development install behavior: CLI and Python module retain the active external
  link directory through `INSTALL_RPATH_USE_LINK_PATH` on RPATH platforms so a
  prefix installed inside the declared environment remains runnable. Release
  installers must replace this environment-specific path with bundled, relative
  runtime paths during the platform packaging/fixup stage.

## HDF5 provenance

- Development version verified: HDF5 2.1.0 from conda-forge
- Version range: `hdf5>=1.14,<3`; release/CI lock must pin an exact artifact
- Source release: `https://github.com/HDFGroup/hdf5/releases/tag/hdf5_2.1.0`
- License: BSD-3-Clause plus the enhancement grant and retained laboratory notices in the official HDF5 license
- Linkage: dynamic through the distribution's HDF5 CMake target; the native H5MD reader calls only the public C API
- Access model: persistent read-only file/dataset handles and requested-frame hyperslabs; all HDF5 API access is
  serialized because a distribution-specific thread-safety build is not exposed to callers
- Trust boundary: external links and datasets backed by external or virtual storage are rejected; one selected local
  particle group is treated as the authoritative trajectory source
- Distribution obligation: the complete HDF5 copyright/license/laboratory notice is reproduced in
  `THIRD_PARTY_NOTICES.md`
- Packaging gate: installer dependency collection must include and audit the exact HDF5 library and compression/filter
  runtime closure on macOS, Linux and Windows. Development RPATH behavior is not self-contained installer evidence.

## OpenDX format reference

- Normative behavior reference: APBS OpenDX scalar-data documentation
- Implementation: independently authored C++ parser; no APBS/OpenDX library or source dependency
- Runtime/build dependency: none
- Supported contract: bounded ASCII regular rank-0 float/double scalar grid only
- Distribution obligation: none beyond MolShredder's own license; the reference documentation is linked, not copied

## MRC2014/CCP4 format reference

- Normative behavior references: CCP-EM MRC2014 specification and CCP4 MAPLIB documentation
- Implementation: independently authored C++20 decoder; no CCP4, mrcfile or cryo-EM library source is copied or linked
- Runtime/build dependency: none
- Supported contract: bounded scalar mode 0/1/2/6/12 map with byte-order and axis-permutation handling
- Distribution obligation: none beyond MolShredder's own license; reference documentation is linked, not copied

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

수동으로 실행하는 GitHub Actions의 macOS/Linux/Windows matrix도 같은 `environment.yml`로
`molshredder` 환경을 만든다. 각 단계는 shell activation에 의존하지 않고 `conda run`으로 실행한다.
Configure에는 환경의 Python prefix를 `CMAKE_PREFIX_PATH`와 `Python3_ROOT_DIR`로 전달한다.
HDF5는 OS별 package-config file 유무에 의존하지 않도록 CMake `FindHDF5` module로 header와
shared library를 찾고, netCDF는 제공되는 package config를 사용한다. 이 개발 환경 검증은
최종 installer의 shared-library closure 검증을 대체하지 않는다.
