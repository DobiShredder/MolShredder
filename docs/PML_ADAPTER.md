# PyMOL command script (PML) adapter

상태: parser + typed translation + sequential run(local macOS 검증)

PML adapter는 core grammar가 아니라 PyMOL 명령 script를 MolShredder의 canonical operation 목록으로 바꾸는 호환
계층이다. 번역 결과는 frontend가 `invoke`로 호출할 operation과 같고 adapter는 별도 state 경로를 두지 않는다.
번역하지 못하는 문장은 조용히 버리지 않고 사유가 있는 diagnostic으로 돌려준다.

```text
molshredder pml translate --path SCRIPT.pml
molshredder pml run --path SCRIPT.pml [--on-error stop|continue]
```

`--text "..."`로 script 본문을 직접 줄 수 있다(`--path`와 정확히 하나). Python은 `molshredder.invoke("pml translate"|"pml run", ...)`다.
Script 원본은 8 MiB로 제한하고 SHA-256을 결과에 남긴다.

## 파싱(PyMOL 3.1.0 관찰과 같음)

PyMOL 3.1.0을 black-box로 실행해 관찰한 규칙을 따른다. 인자는 따옴표·짝이 맞는 `(...)`/`[...]` 밖의 comma로 나뉘고 따옴표는 값에 남는다.
`name=value`는 이름이 식별자일 때만 keyword 인자이고 빈 값은 문장을 무효로 만든다. `#`는 줄 처음에서만 comment이며 `;`는 한 줄에서 command를
나누고 줄 끝 `\`는 다음 줄과 잇는다. Command 이름은 대소문자를 구분하고 고유한 접두 약어를 허용하며 모호하거나 알 수 없는 이름은 diagnostic이다.
`abort`는 script를 멈추고 `skip`…`skip end`는 줄을 건너뛴다. `python`…`python end`와 `/` 줄, `import`/`for` 같은 Python 문장, `run`, `system`, `@file`은 임의
코드 실행이므로 번역하지 않고 `requires_python_trust` 계열 diagnostic으로 거부한다(Python은 `script run --trust true`로 명시 실행). `embed` 블록 안의 줄은
PyMOL 3.1.0이 command로 실행했지만 데이터이므로 실행하지 않고 건너뛴다(의도적 차이).

## 번역되는 PyMOL command

`load`, `load_traj`, `save`, `show`, `hide`, `as`(`show_as`), `toggle`, `color`, `select`, `zoom`, `center`, `orient`, `reset`, `turn`, `move`, `clip`, `origin`, `set`, `unset`,
`bg_color`(`bg_colour`), `png`, `label`, `delete`, `spectrum`, `distance`, `angle`, `dihedral`, `cartoon`, `scene`, `view`, `set_view`, `get_view`, `frame`, `mplay`,
`mstop`, `refresh`, `recolor`, `set_color`, `quit`(`exit`). 인자는 PyMOL의 positional 순서와 keyword 이름으로 받는다. PyMOL의 기본 효과와 같게 하려고
`show`/`hide`/`color`는 `--scope workspace`로, `zoom`/`center`/`orient`는 `--state 0`으로, `save`는 `--overwrite true`로 번역한다. `load`는 항상 `--name`을
명시한다(`object` 인자 또는 파일 stem). Native 기본 이름은 파일 내용(PDB entry 등)에서 오므로 stem과 다를 수 있어 이후 문장의 object 이름이 어긋나기 때문이다.

- 번역 결과가 native와 의미가 다르면 문장 전체를 거부한다(`unsupported_argument`, `unsupported_representation`, `unsupported_format`). 예: 숫자 state, `save`의 selection,
  `distance`의 cutoff와 mode 4 이외, `scene next`, mesh·dots 등 아직 없는 representation, `.gz` 입력, PyMOL session(`.pse`).
- 효과가 없는 인자(`quiet`, `log`, `zoom` 힌트 등)는 `warning`으로 무시하고 계속한다.
- 선택식은 그대로 전달하되 script 안에서 `select`로 만든 이름은 `@name`으로, 현재 Workspace나 script의 object 이름은 `object name`으로 바꾼다(단독 operand일 때만).
  Native 문법은 bare 이름을 named selection으로 추측하지 않는다.
- `delete`는 object만 지운다. Named selection 삭제는 native 연산이 없어 `selection_delete_not_applied` warning으로 알리고 아무것도 바꾸지 않는다.
- 번역하지 않는 command(`alter`/`iterate`, `fetch`, `create`/`copy`/`group`, `align`/`fit`/`super`, `ray`, movie 스크립트 등)는 사유가 있는 `unsupported_command`다.
- `quit`와 `abort`는 이후 문장을 번역·실행하지 않는다.

## `pml run`

문장을 순서대로 번역해 실행하고(`application::Dispatcher`) 문장별 결과를 돌려준다. 기본 `--on-error stop`은 첫 실패에서 멈추고 실패 줄, 실행된 operation 수,
이미 commit된 변경 수를 오류 details에 담는다. PyMOL처럼 계속하려면 `--on-error continue`다. 실행은 failure-atomic하지 않다. 앞선 operation의 변경은
남고 각 operation은 자신의 undo policy를 그대로 따른다. 문장 사이에서 취소를 확인한다.

## 현재 한계

Desktop의 Run Script 선택기는 아직 `.pml`을 받지 않는다. 한 문장이 여러 operation으로 번역될 때의 부분 실패, selection 이름이 script 밖에서 만들어진 경우의 `@` 변환,
PyMOL object 이름과 selection 이름이 같을 때의 우선순위는 검증하지 않았다. 번역 결과를 PyMOL의 pixel·좌표 결과와 대조한 것이 아니라 operation 수준 번역만 검증했다.
