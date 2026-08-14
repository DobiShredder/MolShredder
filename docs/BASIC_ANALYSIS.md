# Basic analysis kernels

Foundation analysis API는 `include/molshredder/analysis/basic.hpp`에 있다. COM, geometric centroid와
atom-to-atom distance는 GUI/CLI/Python adapter가 공유할 UI-independent C++ kernel이다. 현재 API는
한 `CoordinateFrame`의 raw 또는 triclinic minimum-image distance를 계산한다. 같은 kernel을 사용하는
trajectory center/distance reduction은 [Time-series analysis](TIME_SERIES_ANALYSIS.md)에 둔다.
Rigid fitting, RMSD와 RMSF는 [Alignment and fluctuation](ALIGNMENT_AND_FLUCTUATION.md)에 둔다.

## Centroid와 center of mass

`calculate_center()`는 optional 0/1 atom selection을 받는다. 빈 mask는 모든 atom을 의미하며
명시한 mask는 frame atom 수와 일치해야 한다.

- Centroid는 사용한 atom coordinate의 동일 가중 산술 평균이다.
- COM은 atom 수와 같은 explicit non-negative mass vector를 사용하며 selected/present atom의
  total mass가 양수여야 한다.
- COM은 비어 있지 않은 mass source를 요구하고 optional mass unit 및 estimated flag를 결과에
  보존한다. 따라서 추정 질량을 exact mass처럼 표시하지 않는다.
- `masses_from_property()`는 topology의 float32/float64 atom property를 double vector로 변환하고
  metadata의 unit, source 및 `estimated=true|1|yes` annotation을 보존한다. Mass property가 없거나
  non-numeric, non-finite 또는 negative이면 실패한다.
- Workspace COM은 explicit `mass` property를 우선하고, property가 없을 때만
  `estimated_element_masses()`로 fallback한다. 현재 fallback은 생체분자에서 흔한
  H/C/N/O/F/Na/Mg/P/S/Cl/K/Ca/Fe/Zn/Br/I를 지원하며
  [CIAAW Abridged Standard Atomic Weights 2024](https://www.ciaaw.org/abridged-atomic-weights.htm)의
  abridged value를 사용한다. 결과는 항상 `estimated=true`와 table version을 보존한다.
- Standard atomic weight는 물질의 동위원소 조성에 따라 달라질 수 있으므로 fallback은
  isotope-specific exact mass가 아니다. Unsupported/unknown element는 값을 만들어내지 않고 explicit
  mass property를 요구한다.

Missing atom 기본 정책은 `error`다. Selection에 presence=0 atom이 있으면 계산을 중단하므로 frame별
atom 누락 때문에 값의 의미가 조용히 변하지 않는다. Caller가 `skip`을 명시하면 missing atom을
제외하며 결과의 selected/used/skipped count로 이를 기록한다. 사용 가능한 atom이 없으면 실패한다.

Result coordinate는 input frame의 coordinate unit을 그대로 사용한다. 이 kernel은 요청 unit으로
암묵 변환하지 않는다. Frontend의 `--unit` 변환은 typed operation adapter가 provenance와 함께
수행해야 한다.

## Atom distance

`atom_distance()`는 두 stable zero-based `AtomIndex` endpoint를 받고 displacement와 Euclidean norm을
반환한다. 결과 unit은 frame coordinate unit이다. Out-of-range 또는 missing endpoint는 실패하며
같은 atom의 distance는 0이다.

`DistanceBoundary::raw`는 `second - first`를 그대로 사용한다.
`DistanceBoundary::minimum_image`는 frame unit cell과 shared exact triclinic closest-lattice kernel을
사용하며 cell이 없으면 실패한다. 상세 tie/search/wrap/unwrap 의미는 [PBC contract](PBC.md)에 둔다.
Workspace/CLI/GUI action/Python command는 `--pbc raw|minimum-image`를 같은 kernel에 연결한다.
Selection-to-selection min/max/mean/closest와 centroid/COM distance endpoint는 후속 API다.

## Numerical behavior와 limitation

Center numerator와 denominator는 compensated summation을 사용해 큰 좌표의 cancellation에서 작은
항을 가능한 한 보존한다. Float32 coordinate는 source precision 그대로 읽어 double accumulator에
더하고, frame 크기의 임시 coordinate copy를 만들지 않는다. 모든 public model coordinate는 생성
시 finite임이 보장된다.

단일-frame kernel은 coordinate frame 하나를 동기식으로 순회하며 time-series coordinator가 frame별
progress/cancellation을 제공한다. Frame 내부 parallel/SIMD, uncertainty와 isotope-specific mass는
아직 없다. Periodic-table fallback은
위의 제한된 CIAAW 2024 subset만 제공하며 full-element coverage, uncertainty propagation과
isotope/standard 선택은 후속 계약이다.
