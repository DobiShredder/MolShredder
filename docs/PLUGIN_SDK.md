# MolShredder plugin SDK (version 1.0)

상태: manifest·협상·trust store·worker host·in-process C ABI·compatibility kit(local macOS 검증)

Plugin은 MolShredder에 **읽기 전용 typed operation**을 추가한다. Operation은 GUI, CLI, Python에서 같은 typed action(`plugin <namespace> <operation>`)으로 보이고
선택한 원자의 좌표를 읽을 수는 있지만 Workspace를 바꾸지는 못한다. v1은 분석 command를 위한 SDK이고 파일 형식 provider와 representation·UI extension은 ABI가 안정된 뒤 minor 버전으로 추가한다.

## 실행 방식

| | out-of-process worker (기본) | in-process (신뢰 구성) |
|---|---|---|
| 방식 | host가 plugin 프로그램을 자식 process로 시작하고 stdin/stdout으로 JSON lines를 주고받는다 | host가 승인된 shared library를 `dlopen`/`LoadLibrary`하고 C ABI를 호출한다 |
| 격리 | crash·timeout·취소 후 종료가 host를 죽이지 않는다. 환경 변수는 비우고 cwd는 plugin directory다 | **없다.** process 전체 권한으로 실행되며 timeout을 강제할 수 없다 |
| 언어 | 아무 언어(Python helper 제공) | C ABI를 내보내는 언어 |
| license | 제한 없음 | GPL-3.0-or-later와 호환되는 license만(manifest 단계에서 거부) |
| 승인 | `plugin approve` | `plugin approve --allow-in-process true`를 명시해야 한다 |

신뢰하지 않거나 검토하지 않은 코드는 worker로만 실행한다.

## Manifest (`molshredder-plugin.json`)

```json
{
  "schema_version": 1,
  "id": "org.example.sample", "namespace": "sample", "name": "Sample", "version": "1.2.3", "license": "MIT",
  "sdk": {"abi_major": 1, "abi_minor": 0},
  "execution": {"mode": "worker", "command": ["run.sh", "--flag"], "files": ["plugin.py"]},
  "required_capabilities": ["host.selection.atoms"],
  "optional_capabilities": ["progress", "cancellation", "result.table"],
  "operations": [{"name": "centroid", "summary": "...",
                  "parameters": [{"name": "selection", "type": "text", "default": "all"}]}],
  "examples": [{"operation": "centroid", "arguments": {"selection": "all"}}]
}
```

- `id`는 3~64자 소문자(`.`, `_`, `-` 허용), `namespace`는 2~32자 소문자이며 `list`/`approve`/`revoke`/`invoke`/`status`/`check`는 쓸 수 없다. Operation 이름은 `plugin <namespace> <operation>`이 된다.
- `execution.command[0]`(worker) 또는 `execution.library`(in-process)는 manifest 폴더 안의 상대 경로여야 하고 symlink로 폴더를 벗어나면 거부한다. `execution.files`에 적은 파일(스크립트, 데이터)도
  entry와 함께 hash로 고정된다. Launcher 스크립트가 실제 코드를 실행하는 plugin은 반드시 적는다.
- Parameter type은 `text`, `number`, `integer`, `unsigned_integer`, `boolean`이다. Host는 registry에서 type과 choices를 검증한 뒤 같은 type의 JSON 값으로 plugin에 전달한다.
- `undo`는 `not_applicable`만 허용한다(읽기 전용).

## 호환 정책과 capability 협상

- `sdk.abi_major`가 host와 같아야 한다. `sdk.abi_minor`가 host보다 크면 거부하고 host보다 작으면 허용한다(하위 호환). Minor 버전은 필드를 끝에 추가하는 변경이다.
- `required_capabilities`를 host가 제공하지 않으면 거부하고 `optional_capabilities` 중 모르는 것은 조용히 제외한다. v1 capability: `progress`, `cancellation`, `result.table`, `host.selection.atoms`.
- 설치된 plugin의 manifest·entry·고정 파일이 승인 뒤 한 바이트라도 바뀌면 시작 전에 거부한다.

## 신뢰 정책

- 목록(`plugin list --directory D`)은 manifest를 읽기만 하고 아무것도 실행하지 않는다. 실행은 `plugin approve --manifest M`으로 사용자가 그 파일을 명시 승인한 뒤에만 가능하다.
- 승인은 manifest·entry·고정 파일의 SHA-256과 경로를 `~/.molshredder/plugin-trust.json`(`MOLSHREDDER_PLUGIN_TRUST_STORE` 또는 `--trust-store`로 변경)에 기록한다. 깨진 store는 빈 store로 취급하지 않고 오류다.
- 승인된 plugin의 typed operation은 다음 시작 때 등록된다(plugin 코드는 실행하지 않음). 지금 바로 쓰려면 `plugin invoke --plugin ID --operation NAME --arguments-json '{...}'`다.
- 해시 변경, 철회(`plugin revoke`), 협상 실패는 그 plugin의 operation을 등록하지 않고 `plugin status`에 사유가 나온다. 암호 서명 검증은 signing credential이 준비되는 installer 단계에서 추가한다.

## Worker protocol 1.0

한 줄에 JSON object 하나이며 최대 8 MiB다. stderr는 log용이고 stdout은 protocol 전용이다.

```text
host -> plugin  {"type":"hello","protocol":{"major":1,"minor":0},"host":{"name":"molshredder"},"capabilities":["progress",...]}
plugin -> host  {"type":"hello","protocol":{"major":1,"minor":0},"plugin":{"id":"...","version":"..."},"operations":["centroid",...]}
host -> plugin  {"type":"invoke","id":7,"operation":"centroid","arguments":{"selection":"all"}}
plugin -> host  {"type":"progress","id":7,"fraction":0.5,"message":"..."}                          (0회 이상)
plugin -> host  {"type":"host_request","id":7,"request_id":1,"method":"selection.atoms","params":{"selection":"all"}}
host -> plugin  {"type":"host_response","request_id":1,"ok":true,"result":{...}}                    (또는 "ok":false,"error":"...")
plugin -> host  {"type":"result","id":7,"summary":"...","fields":{...},"table":{"columns":[...],"rows":[[...]]}}
plugin -> host  {"type":"error","id":7,"code":"invalid_argument|not_found|unsupported|cancelled|failed","message":"...","suggestion":"..."}
host -> plugin  {"type":"cancel","id":7}   /   {"type":"shutdown"}
```

Handshake의 plugin id·version·operation 집합은 manifest와 정확히 같아야 한다. Host는 취소 요청 뒤 2초 안에 plugin이 응답하지 않으면 worker를 종료하고, timeout(기본 60초)이 지나면 종료하며,
잘못된 JSON·다른 invocation id·한도를 넘는 메시지는 protocol error로 보고 worker를 종료한다. 종료된 worker는 다음 호출 때 해시를 다시 확인한 뒤 새로 시작한다.

`selection.atoms`(capability `host.selection.atoms`)는 활성 object의 선택에서 현재 frame의 원자를 복사해 돌려준다(최대 1,000,000개, 그 이상이면 truncate하지 않고 오류):
`{"count","object","unit","frame_index","ids","names","atomic_numbers","positions":[[x,y,z],...]}`. 숫자는 JSON number이므로 정수로 읽힌 값도 받을 수 있게 구현한다(host는 실수에 `3.0`처럼 소수점을 남긴다).

## In-process C ABI 1.0

`include/molshredder/plugin/molshredder_plugin.h`가 계약이다. Library는 `molshredder_plugin_entry(host, plugin)`을 내보내고 `describe`(manifest와 같은 id·version·operations JSON),
`invoke`(JSON in/out), `free_string`, `shutdown`을 채운다. Host callback(`is_cancelled`, `report_progress`, `request`, `free_string`)은 `invoke` 동안 호출한 thread에서만 유효하다.
각 struct의 `struct_size`와 `abi_major`/`abi_minor`로 versioning하며 major 불일치, 새로운 minor, 작은 struct, 필수 함수 누락, describe 불일치는 로드 단계에서 거부한다.

## 만들기와 검사

- C 예제: `sdk/samples/inprocess_c/plugin.c`(echo, sum+progress, spin+취소, host 데이터 요청). Python worker 예제: `sdk/samples/python_worker/`와 helper `sdk/python/molshredder_plugin.py`
  (`serve()`가 protocol을 처리하고 operation 함수는 `Result`를 반환하거나 `PluginError`를 던진다).
- **compatibility kit**: `molshredder-plugin-check --manifest FILE [--allow-in-process] [--timeout-ms N] [--json]`. MolShredder가 쓰는 같은 host code로 manifest, 협상, 고정 파일, 시작과 handshake,
  manifest `examples`의 각 operation(12원자 합성 분자를 host 데이터로 제공)과 같은 입력에서의 재현성, 선언되지 않은 operation 거부를 검사한다. 모두 통과하면 종료 코드 0이다.

## 현재 한계

Windows process·library adapter는 작성했지만 검증은 SEQ-410A에서 한다. In-process plugin은 timeout을 강제할 수 없다. v1 operation은 결과를 돌려줄 뿐 Workspace를 바꾸지 못하며(변경은 후속 minor),
Desktop에는 plugin 승인 UI가 없다(CLI·Python command만). Python SDK helper는 `/usr/bin/python3` 같은 interpreter를 launcher에서 명시해야 한다(host가 환경을 비운다).
