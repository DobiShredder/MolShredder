# Rigid alignment, RMSD and RMSF

MolShredder의 `analysis::fit_rigid()`는 mobile coordinate를 reference coordinate에 mapping하는
proper rigid transform을 계산한다. Transform은 row-major 3×3 rotation, translation과 input-to-reference
length-unit scale을 보존하며 `apply()`로 임의 point에 재사용할 수 있다. Scene/UI type에 의존하지 않는
C++ core API이므로 향후 structure alignment, trajectory fit, coordinate export와 morphing이 같은
수치 계약을 공유할 수 있다.

## Fit contract

Fit은 paired atom의 weighted centroids를 제거하고 3×3 cross-covariance에서 symmetric 4×4 quaternion
eigenproblem을 구성한다. Jacobi diagonalization의 largest eigenvector로 determinant +1 rotation을 만들고
translation을 복원한다. Reflection과 scale fitting은 하지 않는다. Å/nm가 다른 frame은 mobile을
reference unit으로 정확히 변환한 뒤 fit하며 결과 RMSD unit은 reference unit이다.

- Uniform weight는 atom마다 1, mass weight는 topology의 explicit mass property를 우선한다.
- Explicit mass가 없으면 versioned estimated element mass를 사용하고 source/unit/estimated provenance를
  command result에 기록한다.
- Zero weight atom은 pair count에는 남지만 solve와 RMSD denominator에는 기여하지 않는다.
- Rigid rotation이 유일하려면 positive-weight paired atom 세 개 이상과 reference/mobile 양쪽의
  non-collinear geometry가 필요하다. Collinear/duplicate selection은 임의 회전을 만들지 않고 실패한다.
- Missing=`error`는 첫 missing pair에서 실패한다. `skip`은 양쪽 frame에 존재하는 pair만 사용하며
  selected/paired/skipped/effective count와 weight sum을 결과에 기록한다.

RMSD는 `sqrt(sum(weight * squared_distance) / sum(weight))`의 population-style weighted norm이다.
Fit selection과 RMSD score selection은 다를 수 있다. `fit=none`은 unit conversion만 적용하고 한 atom
selection도 허용한다. `rmsd_before_fit`은 translation/rotation 적용 전 score selection의 값이다.

## Trajectory commands

```text
analyze trajectory rmsd
  [--selection all] [--fit-selection EXPR]
  [--reference 0] [--first 0] [--last LAST] [--stride 1]
  [--fit rigid|none] [--weight uniform|mass]
  [--missing error|skip] [--precision 0..15]
  [--unit angstrom|nanometer]

analyze trajectory rmsf
  [--selection all] [--fit-selection EXPR]
  [--reference 0] [--first 0] [--last LAST] [--stride 1]
  [--fit rigid|none] [--weight uniform|mass]
  [--missing error|skip] [--precision 0..15]
  [--unit angstrom|nanometer]
```

`fit-selection`을 생략하면 score/output selection과 같다. RMSD는 frame별 pre-fit/final value와 pair
telemetry table을 반환한다. RMSF는 requested frame을 optional fit한 뒤 atom별 transformed coordinate의
online vector mean과 population mean-squared displacement를 계산한다. RMSF table은 stable atom index,
source serial, atom/residue/chain identity, observation count, value와 unit을 가진다. Mass weight는 RMSF
값 자체가 아니라 frame fit에 사용된다. Missing=`skip`이면 atom마다 observation count가 다를 수 있고
관측이 0인 atom의 RMSF는 null이다.

두 operation은 current playback frame, representation과 measurement state를 변경하지 않는다. Frame
read 전 cancellation을 확인하고 partial success를 반환하지 않는다. 현재 raw coordinate만 사용하며
PBC molecule reconstruction/unwrap을 암묵 수행하지 않는다. Frame 내부 parallel/SIMD, robust outlier
rejection, sequence-based atom matching, transform application/export, pair-fit constraint, RMSD matrix 및
plot/result persistence는 후속 범위다.
