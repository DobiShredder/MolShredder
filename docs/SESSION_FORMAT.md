# MolShredder session format

MolShredder session은 schema 2의 versioned canonical-operation journal이다. 파일 확장자는 `.msess`이며 object와
trajectory/volume 원본을 복제하지 않고 경로로 참조한다. Session load는 새 `Workspace`에서 허용된 operation만 모두
replay한 뒤 성공한 candidate를 한 번에 publish한다.

```text
molshredder-session 2
generator 0.1.0
metadata invoke "session metadata" --key "ui.visible-panels" --value "analysis,views"
invoke "load" --file-format "pdb" --name "protein" --path "input.pdb"
invoke "select" --expression "chain A" --name "chain_a" --update "false"
invoke "show" --representation "spheres" --selection "@chain_a"
invoke "scene store" --name "active site"
invoke "movie configure" --fps "24" --frames "120" --loop "true"
invoke "movie keyframe" --frame "1" --scene "active site"
invoke "view set" --distance "25" --target-x "3"
invoke "stereo set" --enabled "false"
```

Arguments는 registry normalization 이후 key 순서로 직렬화되며 quote, backslash, newline과 tab escape를 지원한다.
성공한 persistent mutation만 journal에 들어간다. Query, export, session file operation, failed operation과 dynamic
Python/process launch는 저장하지 않는다. Product replay는 현재 Registry의 allowlist를 문서 전체에 먼저 적용하므로
untrusted session에 들어 있는 query/export/script/process command는 Workspace나 filesystem을 변경하기 전에 거부된다.

## 보존 상태

Journal replay와 trailing camera/stereo snapshot은 다음 상태를 재구성한다.

- object, stable identity, visibility, molecular/volume source와 active object
- representation, render setting, named/dynamic selection과 current molecular/trajectory state
- persistent measurement/analysis result, overlay visibility와 scientific provenance
- trajectory attachment/range/speed/frame, volume presentation 및 editing/build/undo/redo history
- camera, projection, clipping, stereo와 named view
- full-Workspace named scene collection과 typed scene/trajectory movie timeline
- Desktop의 안정적으로 식별되는 visible workflow panel 목록 (`ui.visible-panels` extension)

Named scene은 저장 시점의 detached `Workspace` snapshot을 보유한다. Recall은 scene collection, current journal과 movie
track을 보존하면서 snapshot을 failure-atomically publish한다. Movie keyframe은 scene name과 0-based trajectory frame만
허용하며 임의 command나 script 문자열을 실행하지 않는다. Movie frame은 1-based이고 최대 1,000,000 frame,
frame rate는 `(0, 240]` 범위다.

Schema 2 metadata는 core가 모르는 extension key/value도 deterministic map order로 lossless round-trip한다. Desktop은
현재 visible panel 목록만 해석한다. Window geometry, transient dialog, hover/focus, center marker, GPU cache와 running task는
의도적으로 저장하지 않는다.

## Migration, relink와 recovery

`parse_session()`은 schema 1 journal을 source schema version과 migration note가 있는 schema 2 document로 결정적으로
migration한다. Duplicate/malformed metadata, malformed invocation과 unknown future schema는 replay 전에 거부한다.

`session load`는 `load`, `traj load`, `volume load`의 실제로 누락된 singular `path`만 relink 대상으로 보고한다.
`--relink-original`과 `--relink-target`은 한 exact mapping pair이며 target regular file을 canonicalize한다. Prefix,
basename, directory scan 같은 heuristic은 사용하지 않는다.

Primary가 없거나 truncated/corrupt인 경우에만 사용자가 명시한 `--recovery`를 같은 byte budget으로 검사한다. 주변
directory를 자동 검색하지 않으며 response에는 source path, primary error, recovery 여부, source/current schema,
migration note와 relink count가 포함된다.

## File transaction과 autosave

`session save`와 `session autosave`는 기본 64 MiB, 최대 1024 MiB의 bounded serialization을 사용한다. Manual save는
같은 directory에 complete candidate를 만든다. 기존 regular file이 있으면 고유 backup으로 stage하고 candidate를
publish하며, publish 실패 시 이전 파일을 복원한다.

Autosave는 explicit primary/recovery 두 generation을 같은 existing directory에서 관리한다. 새 candidate가 완전히
기록된 뒤 이전 primary를 recovery로 rotate하고 candidate를 primary로 publish한다. 어느 단계든 실패하면 이전
generation을 복원한다. Desktop은 session path가 설정된 동안 2분 간격으로 autosave하며 현재 visible panel 목록도 함께
저장한다.

## Canonical operations

```text
scene list|store|recall|delete|clear
movie status|configure|keyframe|seek|play|pause|step|clear
session save --path FILE [--ui-visible-panels IDS] [--maximum-mib 64]
session autosave --path FILE --recovery FILE [--ui-visible-panels IDS] [--maximum-mib 64]
session load --path FILE [--recovery FILE]
             [--relink-original OLD --relink-target NEW] [--maximum-mib 64]
```

GUI, CLI와 Python `invoke()`는 위 operation과 structured result/error를 공유한다. `.msess`는 MolShredder native format이며
PyMOL PSE/PSW와 호환된다고 주장하지 않는다.

## 명시적 한계

- Session은 self-contained container가 아니다. External file content hash, embedded asset, compression과 relative asset
  manifest는 아직 없다.
- Exact relink는 한 pair만 지원하며 batch mapping UI는 아직 없다.
- PyMOL의 independent scene channel flag, thumbnail/message, arbitrary movie command 및 aligned object-matrix animation 전체와
  parity를 주장하지 않는다.
- Schema 1→2 migration만 현재 지원한다.
