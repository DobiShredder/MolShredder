# License policy

MolShredder 자체 작성 코드는 GNU General Public License version 3 또는 그 이후 버전으로
배포한다. SPDX identifier는 다음과 같다.

```text
GPL-3.0-or-later
```

GPLv3 전문은 repository root의 [`LICENSE`](../LICENSE)에 있다. MolShredder는 유용하기를 바라며
배포하지만, 상품성이나 특정 목적 적합성을 포함한 어떠한 보증도 제공하지 않는다.

## 적용 범위

- 별도 표기가 없는 MolShredder source, test, script와 project documentation은
  `GPL-3.0-or-later`로 배포한다.
- `THIRD_PARTY_NOTICES.md`에 기록된 dependency와 vendored/generated third-party material에는 각각의
  upstream license가 적용된다. MolShredder의 GPL 선택이 third-party 조건을 대체하지 않는다.
- `src/io/xtc_reader.cpp`의 XTC integer decompression은 xdrfile BSD-2-Clause 및 Chemfiles
  BSD-3-Clause source에서 adapted한 부분이며 해당 copyright와 조건을 파일 provenance 및
  `THIRD_PARTY_NOTICES.md`에 보존한다.
- 제한적인 VMD main source 등 license compatibility가 확인되지 않은 코드는 repository에 포함하지
  않는다. 새 dependency는 distribution compatibility와 notice 의무를 먼저 audit한다.
- MolShredder 이름과 로고에는 GPL의 copyright permission과 별개인
  [`TRADEMARKS.md`](../TRADEMARKS.md) 정책이 적용된다.

## 배포 의미

GPL은 상업적 사용, 수정과 수정본 배포를 금지하지 않는다. Covered binary 또는 수정본을 배포할
때에는 GPLv3가 요구하는 corresponding source와 license notice 등의 의무를 따라야 한다. 개인 또는
조직 내부에서만 실행하는 수정본은 배포와 구분된다.

이 문서는 GPL 전문을 요약한 project policy이며 법률 자문이나 license 원문을 대체하지 않는다.
