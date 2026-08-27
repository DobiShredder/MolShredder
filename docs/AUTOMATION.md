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

상태를 공유할 필요가 없으면 kill 가능한 child process를 사용한다.

```bash
molshredder script run-isolated \
  --path report.py --arguments-json '["input.dat"]' --trust true \
  --timeout-ms 30000 --max-output-bytes 8388608 \
  --environment-policy minimal --format json
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

isolated = molshredder.run_script_isolated(
    "report.py", ["input.dat"], trusted=True, timeout_ms=30_000
)
task = molshredder.run_script_isolated_async(
    "report.py", ["input.dat"], trusted=True, timeout_ms=30_000
)
task.cancel()
result = task.result(timeout_ms=2_000)
```

Script 자체에서는 public API만 사용한다.

```python
import molshredder

loaded = molshredder.invoke("load", {"path": "structure.pdb"})
center = molshredder.invoke("com", {"selection": "protein"})
```

## Desktop GUI

상단 `Run Script`를 선택하면 `.py` file picker와 trust confirmation이 차례로 열린다. 기본값인 isolated child
process는 parent Workspace를 노출하지 않고 timeout/cancel 시 child를 kill한다. In-process를 선택하면 script가
MolShredder와 같은 filesystem/network/process 권한을 갖고 실패 전 변경이 남을 수 있음을 명시한다. 승인한 script는
CLI/Python과 같은 `script run-isolated` 또는 `script run` operation을 사용한다. 완료 또는 실패 후 stdout/stderr는 좌측 output panel에
표시되며 닫기 버튼으로 지울 수 있다. Script가 structure, representation 또는 trajectory state를 변경하면 object
panel과 scene packet을 같은 Workspace에서 다시 동기화한다. 실행은 GUI event loop 밖의 전용 worker에서 진행된다.
동시에 Workspace를 편집하지 않도록 실행 중 trajectory playback을 정지하고 viewer 입력을 overlay로 차단한다.
`Request cancellation`은 shared cancellation token을 설정하며 Python code가 반환된 직후의 checkpoint에서 취소
결과를 확정한다.

Isolated worker protocol v1은 matching Python runtime을 `-I`로 시작하고 script stdout/stderr, exception type와
성공 여부를 단일 bounded JSON response로 반환한다. Non-zero exit, crash, direct file-descriptor output처럼 protocol을
깨는 child는 `script_failed`이며 parent Workspace는 항상 그대로다. Timeout은 1..3,600,000 ms, output budget은
1..67,108,864 byte 범위다.
Child environment 기본값 `minimal`은 HOME/LANG/LC_ALL/PATH/SYSTEMROOT/TEMP/TMP/TMPDIR/USERPROFILE/WINDIR만
전달한다. `inherit`은 호출자가 명시할 때만 전체 environment를 전달한다. Async API는 동일 protocol을 background
task로 실행하며 progress, bounded result wait, cancellation과 single completion event를 제공한다.

## 결과와 실패

성공 결과에는 source path/SHA-256, Python runtime, duration, working directory, stdout/stderr, canonical nested
invocation과 committed mutation count가 포함된다. Syntax/runtime error는 `script_failed` result-envelope v2로
반환되므로 CLI는 exit code 2를 사용한다. 실패 전에 완료된 operation은 rollback하지 않으며
`error.details.partial_mutation`, `mutations_committed`, stdout/stderr와 nested history로 명시한다.

## 보안과 현재 제한

- In-process Python은 sandbox가 아니며 MolShredder process와 같은 filesystem, network와 process 권한을 가진다.
- Process isolation도 OS sandbox가 아니다. 명시적으로 신뢰한 code는 child에서 filesystem/network/process 권한을
  그대로 가지며, 현재 보장하는 경계는 parent address space/Workspace 비노출, bounded protocol과 hard kill이다.
- Project/session/startup에서 발견된 script는 자동 실행하지 않는다.
- Source는 canonical regular `.py` file이어야 하고 기본 크기 상한은 8 MiB다.
- Python 실행은 process-global cwd/stdout/stderr를 임시 변경하므로 현재 service가 한 번에 하나씩 직렬화한다.
- Script가 실행 중인 동안 다른 script를 재귀 실행하는 것은 거부한다. 일반 `invoke()` 중첩은 지원한다.
- Arbitrary Python bytecode를 안전하게 강제 중단할 수 없어 hard timeout은 제공하지 않는다. Desktop cancel은
  cooperative request이며 시작 전 또는 Python 반환 직후 checkpoint에서 반영된다. 무한 loop나 block된 native call을
  즉시 종료하지는 못한다.
- Desktop worker가 실행되는 동안 같은 mutable Workspace를 만지는 viewer editing과 trajectory timer는 일시 정지한다.
- `.pml`, explicit text, GUI argument editor, environment allowlist와 OS-native sandbox profile은 후속 capability다.
