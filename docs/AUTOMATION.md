# Automation and external scripts

MolShredder는 CLI와 Python API에서 사용자가 명시적으로 선택한 local Python script를 실행한다. Script 안의
`molshredder.invoke()`는 별도 임시 viewer가 아니라 실행을 시작한 Registry와 Workspace를 사용한다.

## CLI

```bash
molshredder script run \
  --path analysis.py \
  --arguments-json '["trajectory.dcd","--stride","10"]' \
  --working-directory /path/to/work \
  --trust true \
  --format json
```

`--trust true`는 필수다. `arguments-json`은 string만 포함하는 JSON array이며 `sys.argv[0]`은 canonical script
path, 이후 항목은 array 순서대로 설정된다. Working directory를 생략하면 script file의 parent directory를
사용한다. 실행 뒤 `sys.argv`와 process working directory를 복원한다.

## Python API

```python
import molshredder

result = molshredder.run_script(
    "analysis.py",
    ["trajectory.dcd", "--stride", "10"],
    working_directory="/path/to/work",
    trusted=True,
)
```

Script 자체에서는 public API만 사용한다.

```python
import molshredder

loaded = molshredder.invoke("load", {"path": "structure.pdb"})
center = molshredder.invoke("com", {"selection": "protein"})
```

## 결과와 실패

성공 결과에는 source path/SHA-256, Python runtime, duration, working directory, stdout/stderr, canonical nested
invocation과 committed mutation count가 포함된다. Syntax/runtime error는 `script_failed` result-envelope v2로
반환되므로 CLI는 exit code 2를 사용한다. 실패 전에 완료된 operation은 rollback하지 않으며
`error.details.partial_mutation`, `mutations_committed`, stdout/stderr와 nested history로 명시한다.

## 보안과 현재 제한

- In-process Python은 sandbox가 아니며 MolShredder process와 같은 filesystem, network와 process 권한을 가진다.
- Project/session/startup에서 발견된 script는 자동 실행하지 않는다.
- Source는 canonical regular `.py` file이어야 하고 기본 크기 상한은 8 MiB다.
- Python 실행은 process-global cwd/stdout/stderr를 임시 변경하므로 현재 service가 한 번에 하나씩 직렬화한다.
- Script가 실행 중인 동안 다른 script를 재귀 실행하는 것은 거부한다. 일반 `invoke()` 중첩은 지원한다.
- Arbitrary Python bytecode를 안전하게 강제 중단할 수 없어 hard timeout/cancellation은 제공하지 않는다. 시작 전과
  반환 직후 cancellation만 확인한다.
- `.pml`, explicit text, environment allowlist, GUI `Run Script`와 worker-process isolation은 후속 capability다.
