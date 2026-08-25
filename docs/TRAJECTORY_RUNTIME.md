# Trajectory runtime

`trajectory::FrameCache`는 file-backed/in-memory `CoordinateSource`를 감싸는 thread-safe,
memory-budgeted random frame cache다. 자신도 `CoordinateSource`를 구현하므로 기존
`MolecularSystem`과 consumer가 같은 lease API를 사용할 수 있다.

## Payload budget과 LRU

Cache budget은 frame의 coordinate/velocity buffers, presence bytes, per-frame atom-property payload와
metadata string payload를 합한 `frame_payload_bytes()` 기준이다. Container node, allocator bookkeeping,
`shared_ptr` control block과 외부 consumer가 보유한 lease는 `resident_payload_bytes`에 포함하지 않는다.
따라서 이는 process RSS hard limit이 아니라 decoded scientific payload의 명시적 상한이다.

- Hit는 frame을 most-recently-used로 승격한다.
- Miss decode가 성공하면 least-recently-used frame을 budget 안에 들 때까지 evict한다.
- 단일 frame payload가 budget보다 크면 frame은 caller에게 반환하지만 cache에는 넣지 않는다.
- Eviction/clear는 cache의 reference만 제거한다. Consumer가 가진 immutable frame lease는 계속 유효하다.
- Decode failure는 cache state를 변경하지 않는다.

`FrameCacheStats`는 budget/resident bytes, resident count, hit/miss, eviction, oversized bypass,
coalesced wait와 현재/max in-flight 수를 기록한다. `clear()`는 resident와 cache generation만
교체하고 telemetry는 보존한다. Clear 이전 decode가 늦게 끝나도 새 generation에는 삽입되지 않는다.

## Concurrency와 prefetch

Lookup/LRU/stat mutation은 mutex로 보호하고 실제 source decode는 lock 밖에서 수행한다. 따라서 느린
I/O가 cache hit를 막지 않는다. 같은 frame miss는 하나의 in-flight decode와 immutable lease/error를
공유한다. 서로 다른 frame의 동시 decode는 생성 시 고정한 한도(기본 8)를 넘으면
`resource_exhausted`로 거부한다. Semantic decorator는 최근 normalized frame의 weak lease를 최대 16개
기억해 sequential validation이 직전 frame을 불필요하게 다시 decode하지 않게 한다.

`prefetch_async(indices, cancellation)`은 cache를 `shared_ptr`로 유지한 worker에서 요청 순서대로
`read_frame()`을 호출한다. Shared cancellation token은 각 frame decode 전에 확인한다. Cancellation과
decode error는 partial completed count 또는 frame index를 포함한 stable error로 반환한다. 현재
`std::async` task 하나를 prefetch 호출마다 만드는 저수준 foundation이다. Application attach/interactive
seek는 bounded shared `TaskScheduler`를 사용하고 read-ahead window는 아래 `PrefetchScheduler`가 담당한다.

Application path는 `PrefetchScheduler`를 사용한다. Object당 단일 `jthread`가 최신 generation의
direction-aware timeline hint를 처리해 attach/seek/range/play/tick의 오래된 read-ahead를 supersede한다.
Snapshot은 generation, idle/queued/running/succeeded/failed, requested indices, completed count와 error를
노출한다. 새 generation은 현재 decode 이후 stale request의 남은 frame을 중단하지만 reader-level
decode cancellation은 아직 없으며 object destruction은 진행 중 read가 반환될 때까지 join한다.

## 현재 한계

- Playback 속도/cache hit-rate 기반 adaptive window와 priority
- Persistent DCD handle, batched reads와 decoder thread pool
- Cache pressure callback, pinning과 separate CPU/GPU budgets
- Reader 내부에서 이미 진행 중인 단일 file read의 강제 cancellation
- Benchmark 기반 default budget

이 계약은 전체 trajectory를 memory에 올리지 않는다는 목표를 유지하면서 cache policy와 frame lease
lifetime을 분리한다.

## Playback timeline

`PlaybackTimeline`은 available frame count와 inclusive first/last/positive stride를 실제 frame sequence로
정규화한다. `last`를 생략하면 마지막 available frame을 사용하지만 명시적 `last=0`은 0번 frame 하나를
뜻한다. Seek는 이 sequence에 속한 frame만 허용한다.

- `once`: 현재 방향 endpoint에서 자동 pause하며 endpoint를 유지한다.
- `loop`: endpoint 다음 transition에서 반대쪽 endpoint로 wrap하고 방향은 유지한다.
- `rock`: endpoint 다음 transition에서 방향을 반전해 바로 안쪽 frame으로 이동하므로 endpoint를 두
  tick 연속 중복 표시하지 않는다.
- Forward는 range의 첫 frame, reverse는 마지막 normalized frame에서 시작한다.
- Single-frame once range는 첫 tick에서 transition 없이 정지한다.

`advance(n)`은 실제 frame transition 수와 boundary crossing 수를 분리해 반환한다. `PlaybackClock`은
positive finite FPS와 elapsed seconds를 fractional transition accumulator로 변환한다. 한 tick은 최대
120 transition만 내보내고 나머지 backlog를 보존하며, backlog가 10억 transition에 도달할 비정상
입력은 거부한다. `prefetch_hint(n)`은 현재
mode/direction에서 앞으로 방문할 frame을 simulation하고 중복을 제거해 `FrameCache::prefetch_async()`에
전달할 수 있다. Hint 생성은 실제 playback state를 변경하지 않는다.

Application `traj load`와 `traj frame`은 동일한 owner-thread plan → bounded worker candidate → owner-thread
commit kernel을 사용한다. Commit은 object/system/topology/cache, render setting, visibility, named selection과
representation input snapshot이 그대로일 때만 성공한다. CLI/Python canonical operation은 이 kernel을
동기적으로 끝까지 호출하고 Desktop attach/seek는 generation 기반 background service로 예약한다. Rapid
seek는 이전 generation을 취소·폐기하고 latest candidate만 commit한다. Progress/cancel 상태는 Qt owner
thread로 전달되며 failure/cancel/stale completion은 기존 Workspace를 유지한다.

Frame commit 뒤 primitive topology, pick identity와 material layout이 같으면 Desktop은 position/normal/
instance data만 dynamic QRhi buffer에 갱신한다. Cardinality, representation 또는 material이 달라지면
검증된 full-buffer rebuild로 자동 fallback한다. `traj tick --elapsed-ms`가 UI clock adapter의 portable
core boundary이며 frame transition이 있을 때만 packet을 교체한다.
사용자-facing 계약은
[Trajectory commands](TRAJECTORY_COMMANDS.md)와
[Trajectory attachment contract](TRAJECTORY_ATTACHMENT.md)에 둔다.
