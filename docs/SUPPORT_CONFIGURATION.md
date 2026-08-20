# Build support configuration

MolShredder는 configure 시점의 실제 build 정보를 versioned JSON manifest와 typed `system info` operation으로
노출한다. 이 정보는 platform 지원을 광고하는 목록이 아니라 현재 binary가 어떤 환경과 option으로 만들어졌는지
설명하는 진단 자료다.

```bash
molshredder system info --format json
```

Python과 GUI adapter도 같은 canonical operation을 사용한다.

```python
import molshredder

result = molshredder.invoke("system info")
```

Result의 `data`에는 다음 v1 field가 있다.

- `configuration_schema_version`, `project_version`, `build_configuration`
- `platform`: CMake target OS와 architecture
- `toolchain`: C++ compiler ID/version과 C++ standard
- `features`: desktop, embedded Python, HDF5, netCDF와 ThreadSanitizer compile option
- `dependencies`: configure에서 선택한 HDF5, netCDF와 Python version
- `runtime.graphics`: graphics lifecycle status, API/backend, RHI 여부, device name/type와 가능한 vendor/device ID,
  failure reason

같은 configure source로 `molshredder-support.json`을 생성하고 install tree의
`share/molshredder/`에 배치한다. Schema는
`share/molshredder/schemas/support-configuration-v1.schema.json`에 설치된다. CLI, Python과 toolkit-independent
GUI adapter parity test는 runtime field와 generated manifest가 일치하는지 검사한다.

Headless CLI/Python/GUI adapter는 `runtime.graphics.status=unavailable`과 이유를 반환하며 GPU가 존재한다고 추정하지
않는다. Desktop은 scene graph 초기화 전 `not_initialized`, 성공 후 `ready`, Qt error에서는 `failed`를 기록한다.
Ready 상태는 Qt가 실제 선택한 Metal/Direct3D/OpenGL/Vulkan API, QRhi backend와 device identity를 포함한다.
QRhi의 portable `QRhiDriverInfo`가 driver version을 제공하지 않으므로 `driver_version`은 현재 `null`이다.
Desktop 상단 `System` panel은 이 JSON을 다시 계산하지 않고 같은 typed operation 결과를 파싱해 build, dependency와
graphics runtime을 표시한다. `null` 또는 제공되지 않은 값은 `Not reported`로 명시하며 CLI 명령도 함께 안내한다.
Panel smoke는 표시 직전에 사용한 source JSON이 `MolecularViewport::systemInfoJson()`과 exact equality임을 검사한다.

아직 CPU capability, GPU feature/limit, driver-version platform adapter, fallback decision history, optional plugin
discovery와 installer dependency closure는 포함하지 않는다. `features.desktop=true`만으로 특정 GPU backend가 실행
검증됐다고 해석하면 안 되며 `runtime.graphics.status=ready`와 platform smoke evidence를 함께 확인해야 한다.
