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
invoke "view set" --distance "25" --target-x "3"
invoke "view store" --name "active site close-up"
invoke "stereo set" --anaglyph-mode "optimized" --angle-scale "2.1" --enabled "true" --mode "side_by_side" --shift-percent "2" --swap-eyes "false"
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
- Camera와 named-view state는 journal에 canonical `view set/store/recall` command가 명시된 경우 재현된다.
  `view import-pymol`의 18-value text와 optional duration/hand도 canonical invocation으로 replay된다. Replay는
  wall-clock transition을 기다리지 않고 command가 즉시 commit한 endpoint를 복원한다.
  Product-facing `serialize_session(document, workspace)` overload는 현재 camera 17개 field와 stereo 6개 field를
  trailing `view set`/`stereo set` pair로 자동 추가한다. 기존 complete pair는 교체해 반복 저장 시 중복하지 않는다.
  Partial `view set`과 앞선 journal은 보존한다.
  Transient center marker, UI layout, raw molecular data, external file hash와 unknown future field는 저장하지 않는다.
  Active trajectory frame은 canonical `traj frame` command가 있는 경우
  재현되지만 external topology/trajectory path가 그대로 유효해야 한다.
- Schema migration, atomic file I/O, compression, embedded/relative asset policy와 GUI save/open action은
  후속 session vertical slice에서 추가한다.

이 skeleton의 목적은 command/session round-trip과 migration boundary를 먼저 검증하는 것이다. 향후
container manifest를 추가하더라도 canonical command journal은 provenance와 재현 기록으로 유지한다.

Final view snapshot은 의도적으로 camera/stereo-only다. Workspace의 object, representation, measurement, named-view inventory,
trajectory frame 또는 PyMOL scene channel을 검사해 암묵적으로 직렬화하지 않는다. 이들 상태는 각자의 canonical journal
command 또는 향후 명시적인 full-scene/container schema가 담당한다. 따라서 camera snapshot 성공을 full scene 저장 지원으로
해석하면 안 된다.
