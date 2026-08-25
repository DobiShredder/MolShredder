# Bounded task execution

MolShredder의 reader, analysis, mesh 생성과 application background work는 Qt에 의존하지 않는
`operation::TaskScheduler` 계약을 공유한다.

## Resource contract

- 생성 시 worker 수, waiting queue 수, memory budget, priority bypass 한도와 완료 history 한도를 고정한다.
- Task submit은 queued와 running task의 memory를 즉시 예약한다. Queue 또는 memory budget을 넘으면
  `resource_exhausted`를 반환하며 task를 일부 생성하지 않는다.
- 예약은 success, failure, cancellation과 stale discard에서 모두 반환된다.
- Priority는 `interactive`, `normal`, `background` 순이다. 낮은 priority task는 설정된 bypass 횟수 뒤
  반드시 선택되어 starvation되지 않는다.
- Cancellation은 shared token을 사용한다. 실행 중 kernel/reader는 token을 주기적으로 확인해야 한다.
  Progress fraction은 0..1로 정규화되어 같은 callback으로 frontend에 전달된다.

## Two-phase completion

Worker는 Workspace를 직접 변경하지 않는다. Work가 성공하면 task는 `ready_to_commit`이 되고 memory
reservation을 계속 보유한다. Workspace/UI owner thread가 `commit_ready(task_id)`를 호출할 때 generation을
다시 확인한 뒤 candidate를 한 번만 publish한다. Generation이 달라졌으면 `stale_result`로 폐기하고 commit
closure를 실행하지 않는다.

Structure batch service는 이 경계를 사용해 worker에서 모든 input을 parse하고, owner thread에서 모든 name,
object ID, MolecularSystem과 Scene candidate를 검증한 뒤 한 번에 commit한다. Parse/build/cancel/name/ID 오류가
발생하면 기존 object 목록, active object와 Scene pointer를 유지한다.

Trajectory service도 같은 경계를 사용한다. Attach는 reader open/index, mandatory atom mapping,
Å/ps canonical semantic validation, cache와 frame-0 representation candidate를 worker에서 만들며 seek는
frame decode와 representation candidate를 만든다. Owner commit은 계획 당시의 object/system/topology/cache,
setting/visibility/selection/representation input을 재검증한다. Desktop은 generation으로 rapid request를
coalesce하고 queued/ready task는 즉시 취소해 예약 memory를 반환한다. 실행 중 decoder는 cooperative
cancellation 지점에서 중단하며 이미 시작한 단일 file read는 반환 후 stale discard한다.

이 service는 내부 C++ orchestration API다. CLI와 Python의 synchronous canonical operation 및 Desktop의
asynchronous UI가 동일한 Workspace plan/build/commit kernel을 호출한다. 사용자-facing batch operation은
아래와 같다.

```text
load batch --paths "first.pdb;second.cif" [--names "protein;ligand"]
```

현재 delimiter는 semicolon이며 semicolon이 포함된 path는 batch command에서 지원하지 않는다. Input별 다른
format은 auto detection을 사용한다. 명시한 `--file-format`은 모든 input에 공통 적용된다.
