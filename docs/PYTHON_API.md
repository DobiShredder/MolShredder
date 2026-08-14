# Python API prototype

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
```

예:

```python
version = molshredder.invoke("version")
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
```

Return value는 [result envelope v1](RESULT_FORMAT.md)의 Python dictionary 표현이다. Operation
validation/unsupported error도 임의의 Python exception으로 바꾸지 않고 동일한 stable error
envelope로 반환한다. Typed table은 JSON과 같은 `columns` 및 scalar `rows`를 native Python list로
반환한다. Decimal-precision result `Number`도 Python에서는 float다. Python argument conversion 자체가
실패한 경우에는 pybind11의 `TypeError`가
발생할 수 있다.

현재 Python API는 command-dispatch smoke surface다. Typed molecular object, NumPy zero-copy
coordinate view, async task/cancellation, embedded console과 plugin API는 data/trajectory vertical
slice에서 수명·thread·GIL contract를 정한 뒤 추가한다.
