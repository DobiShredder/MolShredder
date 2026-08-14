# Foundation session format

현재 session은 application state 전체를 dump하는 형식이 아니라 canonical command journal을 versioned
문서로 저장하고 replay하는 foundation skeleton이다. Schema version 1의 text 형식은 다음과 같다.

```text
molshredder-session 1
generator 0.1.0
invoke "load" --file-format "pdb" --name "protein" --path "input.pdb"
invoke "select" --expression "chain A" --name "chain_a" --update "false"
invoke "show" --representation "spheres" --selection "@chain_a"
invoke "traj load" --cache-mib "256" --file-format "dcd" --path "run.dcd"
invoke "traj range" --direction "forward" --first "0" --last "500" --mode "loop" --stride "5"
invoke "traj speed" --fps "30"
invoke "traj frame" --frame "250"
```

첫 줄은 session schema version, 둘째 줄은 작성한 MolShredder version이다. 이후 각 non-empty line은
`command::serialize()`가 만든 canonical invocation 하나다. Arguments는 key 순서로 결정적으로
직렬화되고 quote, backslash, newline과 tab escape를 지원한다. Alias나 생략된 default가 아니라
registry normalization 이후 invocation을 기록하는 것이 producer의 책임이다.

`parse_session()`은 schema 1만 받고 unknown version, malformed header/generator와 잘못된 canonical
line을 명시적으로 거부한다. `replay_session()`은 기존 Dispatcher를 통해 순서대로 command를 실행해
GUI/CLI/Python과 동일한 validation/kernel을 사용한다. Cancellation은 command 사이에서 확인하고
실패에는 1-based command 번호를 포함한다.

## 현재 한계

- Load command가 원본 file path를 참조하므로 session은 self-contained가 아니다. File 이동, 내용
  변경 또는 삭제 시 replay가 실패할 수 있다.
- Replay는 아직 transactional하지 않다. N번째 command가 실패하면 앞의 N-1개 mutation은 적용된
  상태로 남으므로 caller는 새 Workspace에서 replay해야 한다.
- Camera, transient center marker, UI layout, raw molecular data, external file hash와 unknown future
  field는 저장하지 않는다. Active trajectory frame은 canonical `traj frame` command가 있는 경우
  재현되지만 external topology/trajectory path가 그대로 유효해야 한다.
- Schema migration, atomic file I/O, compression, embedded/relative asset policy와 GUI save/open action은
  후속 session vertical slice에서 추가한다.

이 skeleton의 목적은 command/session round-trip과 migration boundary를 먼저 검증하는 것이다. 향후
container manifest를 추가하더라도 canonical command journal은 provenance와 재현 기록으로 유지한다.
