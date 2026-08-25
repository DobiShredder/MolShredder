# Python API prototype

## Persistent analysis results

```python
created = molshredder.invoke("analyze center", {
    "selection": "all", "mode": "com", "result-name": "protein-com",
})
detail = molshredder.invoke("result get", {"id": str(created["data"]["result_id"])})
molshredder.invoke("result hide", {"id": "1"})
molshredder.invoke("result export", {
    "id": "1", "path": "protein-com.json", "output-format": "json",
})
```

GUI와 CLI도 같은 Registry/Workspace operation을 호출한다. Stable result identity, algorithm/unit/PBC/
missing-data provenance, source staleness, overlay와 failure-atomic export contract은
[Persistent analysis results](ANALYSIS_RESULTS.md)를 참고한다.

## Trajectory attachment

Trajectory attach도 GUI/CLI와 동일한 mandatory mapping policy를 사용한다.

```python
molshredder.invoke("traj load", {
    "path": "run.h5md", "mapping": "exact",
    "cache-mib": "256", "prefetch-frames": "4",
})
molshredder.invoke("traj load", {
    "path": "reordered.dcd", "mapping": "explicit",
    "atom-map": "3,2,1", "expected-topology-version": "1",
})
```

성공 data의 `atom_mapping`과 `semantics`는 policy/identity strength, compared axes, source/canonical unit,
channel availability, conversion과 missing-data policy를 보존한다. 상세 계약은
[Trajectory attachment](TRAJECTORY_ATTACHMENT.md)를 참고한다.

Python `invoke`는 canonical operation의 최종 상태를 동기적으로 반환한다. Desktop의 background attach/
seek도 별도 구현이 아니라 같은 Workspace plan/build/commit kernel을 bounded scheduler에서 실행하므로
mapping, provenance, cancellation과 failure-atomic 결과 계약이 일치한다.

## Atomic batch load

```python
result = molshredder.invoke("load batch", {
    "paths": "protein.pdb;ligand.sdf",
    "names": "protein;ligand",
    "file-format": "auto",
})
```

`paths`와 선택적인 `names`는 semicolon-delimited이며 name 수는 path 수와 같아야 한다. 모든 input이
parse/build/name 검증을 통과한 뒤 한 번만 commit한다. 오류나 cancellation에서는 기존 Workspace가
변하지 않는다. C++ background orchestration의 memory, progress와 stale-generation 경계는
[Bounded task execution](TASK_EXECUTION.md)을 참고한다.

## Render settings

Python은 CLI와 GUI가 사용하는 같은 typed operation을 호출한다.

```python
molshredder.invoke("setting set", {
    "name": "sphere_scale",
    "value": "1.5",
    "scope": "global",
})

result = molshredder.invoke("setting get", {
    "name": "sphere_scale",
    "scope": "global",
})
```

Atom/bond override에는 topology의 non-zero 64-bit stable ID인 `target`을 추가한다. Operation envelope와 failure-atomic
규칙은 CLI, GUI ActionAdapter와 동일하다.

## Object lifecycle

```python
molshredder.invoke("object rename", {"object": "current", "name": "protein"})
molshredder.invoke("object reorder", {"object": "protein", "position": "1"})
molshredder.invoke("object delete", {"object": "protein"})
molshredder.invoke("object topology-retain", {
    "atom-ids": "3,1", "expected-version": "1",
})
```

Position은 1-based이며 result envelope에 stable `object_id`, `scene_node_id`, old/new position,
ordered object ID와 delete cleanup count가 포함된다. 이 operation은 CLI와 Desktop panel이
사용하는 것과 동일하다.

MolShredder는 pybind11로 생성한 실제 CPython extension module을 제공한다. 현재 prototype은 Python
3.12 ABI로 빌드되며 CLI와 toolkit-independent GUI action이 사용하는 동일한 C++ registry,
normalization, validation, handler와 result envelope를 호출한다.

## Build tree에서 사용

```bash
conda activate molshredder
cmake --preset dev
cmake --build --preset dev
PYTHONPATH=build/dev python -c "import molshredder; print(molshredder.invoke('version'))"
```

Install staging에서는 module이 `lib/molshredder/python/`에 놓인다.

```bash
cmake --install build/dev --prefix build/stage
PYTHONPATH=build/stage/lib/molshredder/python python -c \
  "import molshredder; print(molshredder.__version__)"
```

Windows PowerShell에서는 `$env:PYTHONPATH = "build/stage/lib/molshredder/python"`으로 지정한다.
정식 wheel/embedded-Python packaging은 installer 단계에서 결정한다.

## API

```python
import molshredder

molshredder.__version__
molshredder.invoke(command_name: str, arguments: dict[str, str] = {}) -> dict
molshredder.run_script(
    path: str,
    arguments: list[str] = [],
    working_directory: str | None = None,
    trusted: bool = False,
) -> dict
```

예:

```python
version = molshredder.invoke("version")
support = molshredder.invoke("system info")
center = molshredder.invoke("com", {"selection": "protein"})
series = molshredder.invoke(
    "analyze trajectory distance",
    {"from": "index 1", "to": "index 2", "first": "0", "stride": "10"},
)
columns = series["data"]["table"]["columns"]
rows = series["data"]["table"]["rows"]
rmsf = molshredder.invoke(
    "analyze trajectory rmsf",
    {"selection": "name CA", "fit-selection": "backbone", "reference": "0"},
)
contacts = molshredder.invoke(
    "analyze contacts",
    {"first": "chain A", "second": "chain B", "cutoff": "4.0"},
)
hbonds = molshredder.invoke(
    "analyze hbonds",
    {"donors": "chain A", "acceptors": "chain B", "cutoff": "3.5"},
)
contact_occupancy = molshredder.invoke(
    "analyze trajectory contacts",
    {"selection1": "protein", "cutoff": "4", "report": "occupancy"},
)
secondary_structure = molshredder.invoke(
    "analyze secondary-structure", {"selection": "protein"}
)

molshredder.invoke("show", {"representation": "sticks", "selection": "protein"})
molshredder.invoke("hide", {"representation": "lines", "selection": "resn HOH"})
molshredder.invoke("as", {"representation": "cartoon", "selection": "chain A"})
visibility = molshredder.invoke(
    "toggle", {"representation": "everything", "selection": "resn LIG"}
)
# visibility["data"] includes the resolved primitive mask and membership counts.

molshredder.invoke("view set", {"target-x": "3", "distance": "25"})
molshredder.invoke(
    "view center",
    {"selection": "chain A", "move-origin": "true", "state": "current"},
)
molshredder.invoke(
    "view orient",
    {"selection": "polymer", "state": "all", "duration": "0.35"},
)
molshredder.invoke(
    "view zoom",
    {"selection": "protein", "buffer": "2", "complete": "true", "state": "all",
     "duration": "0.35", "hand": "1"},
)
molshredder.invoke("view origin", {"selection": "resn LIG", "state": "2"})
molshredder.invoke("view origin", {"position": "1.5,-2,3.25"})
molshredder.invoke(
    "view origin", {"object": "current", "selection": "polymer", "state": "all"}
)
molshredder.invoke("view reset", {"object": "current"})
molshredder.invoke("view reset", {"duration": "0.35", "hand": "1"})
molshredder.invoke(
    "view clip",
    {"mode": "atoms", "distance": "2", "selection": "protein", "state": "all"},
)
clip_range = molshredder.invoke("view get-clip")
molshredder.invoke("view move", {"axis": "x", "distance": "5"})
molshredder.invoke("view turn", {"axis": "z", "angle": "15"})
molshredder.invoke(
    "view projection",
    {"mode": "orthographic", "field-of-view-degrees": "45",
     "preserve-scale": "true"},
)
# Convenience aliases normalize to the same operation
molshredder.invoke("orthoscopic")
molshredder.invoke(
    "stereo set",
    {"enabled": "true", "mode": "side_by_side", "swap-eyes": "false",
     "shift-percent": "2.0", "angle-scale": "2.1",
     "anaglyph-mode": "optimized"},
)
stereo_capabilities = molshredder.invoke("stereo modes")
molshredder.invoke(
    "stereo set",
    {"enabled": "true", "mode": "checkerboard", "swap-eyes": "false"},
)
molshredder.invoke("view store", {"name": "active site close-up"})
views = molshredder.invoke("view list")
molshredder.invoke(
    "view recall",
    {"name": "active site close-up", "duration": "0.35", "hand": "1"},
)
pymol_view = molshredder.invoke("view export-pymol")
molshredder.invoke(
    "view import-pymol",
    {"values": pymol_view["data"]["text"], "duration": "0.35", "hand": "1"},
)

script = molshredder.run_script(
    "analysis.py", ["trajectory.dcd", "--stride", "10"], trusted=True
)
```

Return value는 [result envelope v2](RESULT_FORMAT.md)의 Python dictionary 표현이다. Operation
validation/unsupported error도 임의의 Python exception으로 바꾸지 않고 동일한 stable error
envelope로 반환한다. Typed table은 JSON과 같은 `columns` 및 scalar `rows`를 native Python list로
반환한다. Decimal-precision result `Number`도 Python에서는 float다. Python argument conversion 자체가
실패한 경우에는 pybind11의 `TypeError`가
발생할 수 있다.

`run_script`는 사용자가 명시적으로 선택하고 `trusted=True`로 승인한 local `.py` file만 실행한다. Script에서
다시 `import molshredder`하고 `invoke`를 호출하면 실행을 시작한 동일 Registry/Workspace가 사용된다. 성공 결과에는
stdout/stderr, canonical nested invocation, mutation count, source SHA-256, interpreter와 working directory가
들어간다. Runtime/syntax error도 exception으로 변환하지 않고 `script_failed` envelope를 반환하며 같은 정보와
`partial_mutation`을 `error.details`에 보존한다. 자세한 보안·수명 계약은 [Automation](AUTOMATION.md)을 따른다.

현재 Python API는 command-dispatch smoke surface다. Typed molecular object, NumPy zero-copy
coordinate view, async task/cancellation, embedded console과 plugin API는 data/trajectory vertical
slice에서 수명·thread·GIL contract를 정한 뒤 추가한다.
