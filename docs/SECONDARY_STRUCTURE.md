# Secondary-structure assignment

MolShredder uses an independently authored, paper-method implementation inspired
by STRIDE. It does not contain, link, download, or execute the original STRIDE
program. The current method identifier is `molshredder-stride-method-v0`; it is
not an exact STRIDE compatibility claim.

```text
analyze secondary-structure --selection protein
ss --selection "chain A" --precision 3
```

The result is a residue table with one-based residue index, source residue
identity, STRIDE-style code, normalized state, phi/psi angles, and backbone
completeness. The supported state vocabulary is:

| Code | State |
|---|---|
| H | alpha helix |
| G | 3-10 helix |
| I | pi helix |
| E | extended beta strand |
| B | isolated beta bridge |
| T | turn |
| C | coil/unassigned |

## Method v0

The core extracts N, CA, C, O and optional amide H atoms per residue, respecting
chain and segment boundaries. Phi and psi are calculated from consecutive
backbone atoms. If amide H is absent, its direction is reconstructed from the
previous carbonyl C, current N, and current CA.

Hydrogen bonds use an independently written empirical score with an 8-6 radial
term centered at 3 Å and donor/acceptor angular factors. Helix and sheet gates
combine accepted H-bond patterns with smooth phi/psi propensities. Two
consecutive i-to-i+3, i-to-i+4, or i-to-i+5 bonds seed G, H, or I helices.
Reciprocal/offset inter-residue H-bond patterns seed beta bridges; neighboring
bridges become extended strands. Remaining 3/4/5-turn H-bond interiors become
turns. The priority is H, I, G, E, B, T, C.

Defaults are `energy-cutoff=-0.5 kcal/mol`, `helix-propensity=0.05`, and
`beta-propensity=0.02`. They are exposed for controlled validation, not as a
claim that they reproduce original STRIDE's unpublished or inaccessible
empirical tables.
Å and nm frames are normalized to Å before energy scoring; torsion angles are
unit invariant.

H-bond assignment does not scan every donor/acceptor pair. The radial score and
requested energy cutoff define a conservative outer distance used as the cell
width for a deterministic spatial index. Candidate pairs still receive the
exact energy and angular calculation. Beta-bridge candidates are derived from
the sparse accepted H-bond graph and checked with the same local pattern
predicates, avoiding a residue-pair quadratic scan in normal molecular
geometries.

## Provenance and limitations

The original papers describe STRIDE as combining empirical hydrogen-bond
energy and statistically derived backbone torsion probabilities. The original
program has distribution restrictions for packages sold for money, so it is
not a MolShredder dependency. See the [1995 method paper](https://doi.org/10.1002/prot.340230412),
the [2004 STRIDE server paper](https://pmc.ncbi.nlm.nih.gov/articles/PMC441567/),
and the [VMD third-party notice](https://tcbg.illinois.edu/Research/vmd/allversions/disclaimer.html).

Method v0 still needs legal black-box conformance fixtures for exact empirical
thresholds, beta bulges/bridge precedence, turn edge cases, alternate-location
policy, chain-break geometry, and comparison on a versioned PDB corpus. Until
those pass, downstream cartoon rendering must preserve the method identifier
and `exact_stride_parity=false` provenance.

The regression suite compares indexed H-bond results with exhaustive evaluation
on a synthetic fixture. A 5,000-residue/25,000-atom fixture must finish the
assignment in under five seconds in the warning-as-error debug test build. This
is a regression ceiling, not a cross-platform performance guarantee.

`show --representation cartoon`은 현재 frame에서 이 assignment를 실행하고 H/G/I에는 넓은 helix,
E/B에는 terminal arrow profile, T/C에는 좁은 coil profile을 적용한다. `ribbon`은 state와 무관한 일정
폭을 사용한다. 생성 packet은 `secondary_structure_method=molshredder-stride-method-v0`와
`exact_stride_parity=false`를 보존한다. 이는 original STRIDE geometry나 PyMOL cartoon의 exact parity
주장이 아니다.
