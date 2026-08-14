# Contributing to MolShredder

Architecture와 public API가 아직 foundation 단계이므로 큰 변경은 구현 전에 issue에서 scope와
data/license 영향을 먼저 논의한다. Bug report에는 OS, CPU/GPU, 재현 단계, input format과 가능한
경우 최소 재현 fixture를 포함한다.

## License와 provenance

Contribution을 제출하면 본인이 제출할 권한이 있으며 해당 contribution을
`GPL-3.0-or-later`로 배포하는 데 동의한다. Commit에는 Developer Certificate of Origin 방식의
sign-off를 포함한다.

```text
Signed-off-by: Your Name <your.email@example.com>
```

`git commit -s`로 sign-off를 추가할 수 있다. 다른 프로젝트의 코드를 복사하거나 변형한 경우에는
source URL, exact version/commit, 원래 license와 변경 내용을 반드시 기록한다. VMD main source처럼
재배포 또는 파생물 조건이 호환되지 않는 자료를 contribution에 포함하지 않는다.

## 기본 검증

```bash
conda activate molshredder
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

새 기능은 GUI·CLI·Python이 공유하는 C++ operation/kernel 경계를 사용하고, public behavior와
limitation 문서를 함께 갱신한다.
