# VMD Tcl script adapter (subset)

상태: Tcl 파서·치환 + typed translation + sequential run(local macOS 검증)

VMD script 중 문서화된 의미가 native operation과 일대일로 맞는 부분만 canonical operation으로 번역한다. VMD 자체는 reference로 쓸 수 없고 main source도
쓰지 않으므로 모든 대응은 공개 User's Guide에 근거한다. 의미를 검증할 수 없는 command는 근사하지 않고 사유가 있는 diagnostic으로 거부한다.

```text
molshredder tcl translate --path SCRIPT.tcl
molshredder tcl run --path SCRIPT.tcl [--on-error stop|continue]
```

`--text "..."`도 쓸 수 있다(`--path`와 정확히 하나). Python은 `molshredder.invoke("tcl translate"|"tcl run", ...)`다.

## Tcl 문법(Tcl 8.5와 같음)

단어 분리, 중괄호(치환 없음)·따옴표(치환 있음), `\` escape와 줄 연속, `;`와 줄바꿈, command 처음의 `#` comment, `$name`·`${name}`·`$name(index)`, `[...]` command
치환을 `tclsh` 8.5.9에서 관찰한 결과와 같게 처리한다. 변수는 `set`/`unset`만, 값은 `list`와 아래 command의 반환값(`mol new`는 molid, `atomselect`는 handle)이다.
`if`/`for`/`foreach`/`while`/`proc`/`switch`/`catch`/`eval`/`uplevel`과 `expr`은 해석하지 않고 `unsupported_control_flow` 또는 `unsupported_command`로 보고한다.
`exec`, `source`, `open` 등 코드 실행·파일 접근 command는 번역하지 않고 `requires_code_execution`으로 거부한다. Tcl 오류(정의되지 않은 변수, 문법 오류)는 Tcl처럼 script를 멈춘다.

## 번역되는 command

| VMD | native |
|---|---|
| `mol new FILE ?type T? ?waitfor W?`, `mol load TYPE FILE` | `load` 또는 `volume load`(항상 `--name`을 명시, 같은 stem이면 `_2`) |
| `mol addfile FILE ?type T? ?molid M?` | `traj load --mapping index`(active object에 붙음) |
| `mol delete ID|top|all` | `object delete` |
| `mol rename ID NAME` | `object rename` |
| `display projection Orthographic|Perspective`, `display resetview` | `view projection`, `view reset` |
| `color Display Background NAME`, `color change rgb NAME R G B` | `background set`, `color define` |
| `animate goto N|start`, `animate pause` | `traj frame`, `traj pause` |
| `set sel [atomselect MOL "TEXT"]`, `measure center $sel ?weight mass?` | `analyze center --mode centroid|com` |

`mol new`의 `first`/`last`/`step`은 기본값만, `filebonds`/`autobonds`는 켜진 상태만 받는다. `waitfor`는 loading이 동기식이라 영향이 없다. 파일 형식은 `type` 또는 확장자로
정하고 trajectory 전용 파일로 `mol new`하면 거부한다. `puts`/`echo`는 효과 없음 info, `exit`/`quit`는 이후 command를 번역·실행하지 않는다.

## 번역하지 않는 것(사유 포함)

- `mol representation/color/selection/material/addrep/delrep/modstyle/modcolor/modselect/modmaterial/showrep`: VMD의 representation 목록과 native `show`/`hide`/`color`가 일대일로
  대응하지 않는다. `mol on/off/top`은 native object 조작이 id 기준이라 대응하지 않는다.
- `rotate`/`scale`/`translate`, `display depthcue` 등 나머지 `display`, `animate forward/reverse/speed`, `render`, `measure`의 나머지 subcommand, `$sel`의 method(`num`, `get`, `set`, `moveby` 등):
  회전 방향 규약·결과 값 반환 같은 의미를 VMD 없이 검증할 수 없거나 native가 값을 Tcl로 돌려주지 못한다.
- `atomselect`의 선택 문자열은 native selection 문법으로 그대로 전달하며(`selection_syntax_passed_through`) VMD 고유 keyword(`protein` 등)는 의미가 다를 수 있다.

## `tcl run`

`pml run`과 같은 방식으로 `application::Dispatcher`로 실행하고 command별 결과를 돌려준다. 기본 `--on-error stop`은 번역하지 못한 command나 실패한 operation에서 멈추고 실패 줄,
실행된 operation 수, commit된 변경 수를 오류 details에 담는다. `--on-error continue`는 그런 command를 건너뛰되 Tcl 오류는 항상 script를 중단한다.

## 현재 한계

사용 빈도(Lane E diary)가 없어 subset은 "native operation이 이미 있고 의미가 문서로 확정되는 command" 기준이다. VMD 동작과 pixel·좌표 수준으로 대조하지 않았다(VMD를 실행할 수 없음).
Desktop에는 선택기가 없다. 변수 치환은 scalar와 `name(index)` 문자열 key만 지원하고 배열 명령(`array`)과 `expr` 계산은 지원하지 않는다.
